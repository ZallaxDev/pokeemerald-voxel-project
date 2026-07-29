#!/usr/bin/env python3
"""Local, dependency-free server for the diorama visual map editor."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import threading
import webbrowser
import zlib
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlparse

EDITOR_ROOT = Path(__file__).resolve().parent
REPO_ROOT = EDITOR_ROOT.parent
TOOLS_ROOT = REPO_ROOT / "tools/diorama_rules"
sys.path.insert(0, str(TOOLS_ROOT))

from compile_rules import (  # noqa: E402
    RuleError,
    TILESETS,
    compile_data,
    load_json,
    parse_behaviors,
)
from export_editor_map import build_editor_document  # noqa: E402

STATIC_FILES = {
    "/": ("index.html", "text/html; charset=utf-8"),
    "/index.html": ("index.html", "text/html; charset=utf-8"),
    "/editor.css": ("editor.css", "text/css; charset=utf-8"),
    "/editor.js": ("editor.js", "text/javascript; charset=utf-8"),
}
WRITE_LOCK = threading.Lock()


def json_bytes(value: object) -> bytes:
    return (json.dumps(value, indent=2, ensure_ascii=True) + "\n").encode("utf-8")


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def map_sources() -> dict[str, Path]:
    result = {}
    for path in sorted((REPO_ROOT / "data/diorama/maps").glob("*.json")):
        data = load_json(path)
        symbol = data.get("map")
        if isinstance(symbol, str):
            result[symbol] = path.resolve()
    return result


def map_listing() -> list[dict]:
    result = []
    for symbol, path in map_sources().items():
        data = load_json(path)
        result.append({
            "symbol": symbol,
            "layout": data.get("layout"),
            "file": path.relative_to(REPO_ROOT).as_posix(),
            "camera": data.get("camera", {"profile": "exterior"}),
        })
    return result


def normalize_rule(rule: dict) -> dict:
    faces = {
        face: {"metatile": "self", "layer": "foreground" if face == "plane" else "full"}
        for face in ("top", "north", "east", "south", "west", "plane")
    }
    for face, material in rule.get("faces", {}).items():
        faces[face] = material
    return {
        "shape": rule.get("shape", "flat"),
        "profile": rule.get("profile", "none"),
        "axis": rule.get("axis", "x"),
        "baseMetatile": rule.get("baseMetatile", "self"),
        "groundHeight": rule.get("groundHeight", 0),
        "height": rule.get("height", 0),
        "faces": faces,
    }


def load_templates() -> dict:
    templates = {}
    for path in sorted((REPO_ROOT / "data/diorama/buildings").glob("*.json")):
        templates.update(load_json(path).get("templates", {}))
    return templates


def tileset_rule_document(symbol: str) -> dict:
    for path in sorted((REPO_ROOT / "data/diorama/tilesets").glob("*.json")):
        data = load_json(path)
        if data.get("tileset") == symbol:
            return data
    return {"metatiles": {}}


def enrich_document(document: dict) -> dict:
    defaults = load_json(REPO_ROOT / "data/diorama/defaults.json")
    behavior_ids = parse_behaviors(REPO_ROOT / "include/constants/metatile_behaviors.h")
    behavior_names = {value: key for key, value in behavior_ids.items()}
    templates = load_templates()
    rules = document["rules"]
    overrides = {(item["x"], item["y"]): item for item in rules.get("overrides", [])}
    sign_coordinates = {
        (item["x"], item["y"])
        for item in document["events"]["background"]
        if item.get("type") == "sign"
    }
    tileset_docs = {
        role: tileset_rule_document(info["symbol"])
        for role, info in document["tilesets"].items()
    }
    resolved = []
    for cell in document["cells"]:
        coordinate = (cell["x"], cell["y"])
        source = "fallback"
        rule = defaults["default"]
        if coordinate in overrides:
            source, rule = "map override", overrides[coordinate]
        elif coordinate in sign_coordinates and "sign" in rules.get("eventRules", {}):
            source, rule = "event sign", rules["eventRules"]["sign"]
        else:
            local_rules = tileset_docs[cell["tilesetRole"]].get("metatiles", {})
            tileset_rule = next(
                (value for key, value in local_rules.items() if int(key, 0) == cell["localMetatile"]),
                None,
            )
            if tileset_rule is not None:
                source, rule = "tileset", tileset_rule
            else:
                placement = next((item for item in rules.get("buildings", [])
                    if item["x"] <= cell["x"] < item["x"] + templates[item["template"]]["width"]
                    and item["y"] <= cell["y"] < item["y"] + templates[item["template"]]["height"]), None)
                if placement:
                    template = templates[placement["template"]]
                    local_y = cell["y"] - placement["y"]
                    shape = "roof" if local_y < template["roofRows"] else "building-part"
                    rule = {
                        "shape": shape,
                        "profile": template["profile"],
                        "height": template["bodyHeight"] + (template["roofHeight"] if shape == "roof" else 0),
                        "faces": template.get("faces", {}),
                    }
                    source = f"building:{placement['template']}"
                else:
                    behavior = behavior_names.get(cell["behavior"])
                    if behavior in defaults.get("behaviors", {}):
                        source, rule = f"behavior:{behavior}", defaults["behaviors"][behavior]
                    elif cell["collision"] or cell["elevation"] not in (0, 3, 15):
                        source = "collision/elevation"
                    elif cell["layerType"]:
                        source = "visual heuristic"
        resolved.append({"source": source, "rule": normalize_rule(rule)})

    for role, info in document["tilesets"].items():
        relative = TILESETS[info["symbol"]][1]
        count = (REPO_ROOT / "data/tilesets" / relative / "metatile_attributes.bin").stat().st_size // 2
        info["count"] = count
        attributes = struct.unpack(
            f"<{count}H",
            (REPO_ROOT / "data/tilesets" / relative / "metatile_attributes.bin").read_bytes(),
        )
        info["catalog"] = [
            {
                "local": index,
                "global": index + (512 if role == "secondary" else 0),
                "behavior": behavior_names.get(value & 0xFF, str(value & 0xFF)),
                "layerType": value >> 12,
            }
            for index, value in enumerate(attributes)
        ]
        primary = document["tilesets"]["primary"]["symbol"]
        info["atlas"] = f"/api/atlas?role={role}&symbol={info['symbol']}&primary={primary}"
        info["atlasLayers"] = {
            layer: f"/api/atlas?role={role}&symbol={info['symbol']}&primary={primary}&layer={layer}"
            for layer in ("base", "foreground")
        }
    document["editor"] = {
        "revision": digest((REPO_ROOT / document["ruleSource"]).read_bytes()),
        "behaviorNames": {str(key): value for key, value in behavior_names.items()},
        "templates": templates,
        "resolved": resolved,
    }
    return document


def decode_indexed_png(path: Path) -> tuple[int, int, list[int]]:
    raw = path.read_bytes()
    if raw[:8] != b"\x89PNG\r\n\x1a\n":
        raise RuleError(f"{path}: not a PNG")
    offset, width, height, depth, color_type = 8, 0, 0, 0, 0
    compressed = bytearray()
    while offset < len(raw):
        size = struct.unpack_from(">I", raw, offset)[0]
        kind = raw[offset + 4:offset + 8]
        chunk = raw[offset + 8:offset + 8 + size]
        offset += size + 12
        if kind == b"IHDR":
            width, height, depth, color_type, _, _, interlace = struct.unpack(">IIBBBBB", chunk)
            if color_type != 3 or depth not in (4, 8) or interlace:
                raise RuleError(f"{path}: unsupported tiles PNG format")
        elif kind == b"IDAT":
            compressed.extend(chunk)
        elif kind == b"IEND":
            break
    packed = zlib.decompress(bytes(compressed))
    row_bytes = (width * depth + 7) // 8
    stride = row_bytes + 1
    rows: list[bytearray] = []
    previous = bytearray(row_bytes)
    for y in range(height):
        mode = packed[y * stride]
        source = packed[y * stride + 1:(y + 1) * stride]
        row = bytearray(row_bytes)
        for x, value in enumerate(source):
            left = row[x - 1] if x else 0
            up = previous[x]
            upper_left = previous[x - 1] if x else 0
            if mode == 0:
                prediction = 0
            elif mode == 1:
                prediction = left
            elif mode == 2:
                prediction = up
            elif mode == 3:
                prediction = (left + up) // 2
            elif mode == 4:
                p = left + up - upper_left
                distances = (abs(p - left), abs(p - up), abs(p - upper_left))
                prediction = (left, up, upper_left)[distances.index(min(distances))]
            else:
                raise RuleError(f"{path}: unsupported PNG filter")
            row[x] = (value + prediction) & 0xFF
        rows.append(row)
        previous = row
    pixels = []
    for row in rows:
        if depth == 8:
            pixels.extend(row[:width])
        else:
            expanded = []
            for value in row:
                expanded.extend((value >> 4, value & 15))
            pixels.extend(expanded[:width])
    return width, height, pixels


def read_palettes(path: Path) -> list[list[tuple[int, int, int]]]:
    palettes = []
    for index in range(16):
        raw = (path / f"{index:02}.gbapal").read_bytes()
        colors = []
        for value, in struct.iter_unpack("<H", raw[:32]):
            colors.append(((value & 31) * 255 // 31, ((value >> 5) & 31) * 255 // 31,
                           ((value >> 10) & 31) * 255 // 31))
        palettes.append(colors)
    return palettes


def png_rgba(width: int, height: int, pixels: bytes) -> bytes:
    def chunk(kind: bytes, data: bytes) -> bytes:
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)
    rows = b"".join(b"\0" + pixels[y * width * 4:(y + 1) * width * 4] for y in range(height))
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(rows, 9)) + chunk(b"IEND", b""))


def build_atlas(symbol: str, primary_symbol: str | None = None, layer_name: str = "full") -> bytes:
    if symbol not in TILESETS:
        raise RuleError("unknown tileset")
    if layer_name not in ("full", "base", "foreground"):
        raise RuleError("unknown atlas layer")
    relative = TILESETS[symbol][1]
    root = REPO_ROOT / "data/tilesets" / relative
    tile_width, _, tile_pixels = decode_indexed_png(root / "tiles.png")
    palettes = read_palettes(root / "palettes")
    primary_pixels = tile_pixels
    primary_palettes = palettes
    if relative.startswith("secondary/"):
        if primary_symbol not in TILESETS or not TILESETS[primary_symbol][1].startswith("primary/"):
            raise RuleError("secondary atlas requires its primary tileset")
        primary_root = REPO_ROOT / "data/tilesets" / TILESETS[primary_symbol][1]
        primary_width, _, primary_pixels = decode_indexed_png(primary_root / "tiles.png")
        if primary_width != tile_width:
            raise RuleError("tilesets use incompatible tile widths")
        primary_palettes = read_palettes(primary_root / "palettes")
    entries = list(struct.iter_unpack("<8H", (root / "metatiles.bin").read_bytes()))
    columns, size = 16, 16
    width = columns * size
    height = ((len(entries) + columns - 1) // columns) * size
    output = bytearray(width * height * 4)
    tiles_per_row = tile_width // 8
    for metatile, parts in enumerate(entries):
        origin_x = (metatile % columns) * size
        origin_y = (metatile // columns) * size
        layers = {"full": range(2), "base": (0,), "foreground": (1,)}[layer_name]
        for layer in layers:
            for quadrant in range(4):
                value = parts[layer * 4 + quadrant]
                tile, hflip, vflip, palette = value & 0x3FF, value & 0x400, value & 0x800, value >> 12
                for py in range(8):
                    for px in range(8):
                        source_tile = tile
                        source_pixels = primary_pixels
                        if relative.startswith("secondary/") and tile >= 512:
                            source_tile = tile - 512
                            source_pixels = tile_pixels
                        source_x = (source_tile % tiles_per_row) * 8
                        source_y = (source_tile // tiles_per_row) * 8
                        sx = source_x + (7 - px if hflip else px)
                        sy = source_y + (7 - py if vflip else py)
                        source_index = sy * tile_width + sx
                        if source_index >= len(source_pixels):
                            # Animated VRAM slots are not present in the static tiles.png.
                            red, green, blue = ((72, 58, 78) if (px + py) % 2 else (43, 35, 48))
                            color_index = None
                        else:
                            color_index = source_pixels[source_index]
                        if color_index == 0:
                            continue
                        if color_index is not None:
                            selected_palettes = primary_palettes if palette < 6 else palettes
                            red, green, blue = selected_palettes[palette][color_index]
                        dx = origin_x + (quadrant % 2) * 8 + px
                        dy = origin_y + (quadrant // 2) * 8 + py
                        target = (dy * width + dx) * 4
                        output[target:target + 4] = bytes((red, green, blue, 255))
    return png_rgba(width, height, bytes(output))


def validate_candidate(destination: Path, candidate: bytes) -> None:
    with tempfile.TemporaryDirectory(prefix="diorama-editor-") as temporary:
        root = Path(temporary)
        (root / "data").mkdir()
        shutil.copytree(REPO_ROOT / "data/diorama", root / "data/diorama")
        for name in ("maps", "layouts", "tilesets"):
            os.symlink(REPO_ROOT / "data" / name, root / "data" / name, target_is_directory=True)
        (root / "include").mkdir()
        os.symlink(REPO_ROOT / "include/constants", root / "include/constants", target_is_directory=True)
        relative = destination.relative_to(REPO_ROOT / "data/diorama")
        (root / "data/diorama" / relative).write_bytes(candidate)
        compile_data(root)


def run_rule_commands() -> list[str]:
    commands = [
        [sys.executable, "tools/diorama_rules/validate_rules.py"],
        [sys.executable, "tools/diorama_rules/compile_rules.py"],
        [sys.executable, "tools/diorama_rules/compile_rules.py", "--check"],
    ]
    output = []
    for command in commands:
        result = subprocess.run(command, cwd=REPO_ROOT, text=True, capture_output=True, timeout=60)
        output.append((result.stdout + result.stderr).strip())
        if result.returncode:
            raise RuleError(output[-1] or f"command failed: {' '.join(command)}")
    return output


def validate_payload(payload: dict) -> tuple[Path, bytes]:
    if set(payload) - {"symbol", "revision", "rules"}:
        raise RuleError("request contains unknown fields")
    symbol = payload.get("symbol")
    sources = map_sources()
    if symbol not in sources:
        raise RuleError("unknown or non-editable map")
    rules = payload.get("rules")
    if not isinstance(rules, dict) or rules.get("map") != symbol:
        raise RuleError("rules do not match the selected map")
    return sources[symbol], json_bytes(rules)


class EditorHandler(BaseHTTPRequestHandler):
    server_version = "DioramaEditor/1"

    def log_message(self, format_string: str, *args: object) -> None:
        sys.stderr.write(f"{self.address_string()} - {format_string % args}\n")

    def send_bytes(self, status: int, data: bytes, content_type: str) -> None:
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        self.send_header("Content-Security-Policy", "default-src 'self'; img-src 'self' data:; style-src 'self'; script-src 'self'; connect-src 'self'")
        self.end_headers()
        self.wfile.write(data)

    def send_json(self, status: int, value: object) -> None:
        self.send_bytes(status, json_bytes(value), "application/json; charset=utf-8")

    def do_GET(self) -> None:  # noqa: N802
        parsed = urlparse(self.path)
        try:
            if parsed.path in STATIC_FILES:
                filename, content_type = STATIC_FILES[parsed.path]
                self.send_bytes(HTTPStatus.OK, (EDITOR_ROOT / filename).read_bytes(), content_type)
            elif parsed.path == "/api/maps":
                self.send_json(HTTPStatus.OK, {"maps": map_listing()})
            elif parsed.path == "/api/map":
                symbol = parse_qs(parsed.query).get("symbol", [None])[0]
                if symbol not in map_sources():
                    raise RuleError("unknown or non-editable map")
                self.send_json(HTTPStatus.OK, enrich_document(build_editor_document(REPO_ROOT, symbol)))
            elif parsed.path == "/api/atlas":
                query = parse_qs(parsed.query)
                symbol = query.get("symbol", [None])[0]
                role = query.get("role", [None])[0]
                if symbol not in TILESETS or role not in ("primary", "secondary") or not TILESETS[symbol][1].startswith(role + "/"):
                    raise RuleError("invalid tileset atlas request")
                primary = query.get("primary", [None])[0]
                layer = query.get("layer", ["full"])[0]
                self.send_bytes(HTTPStatus.OK, build_atlas(symbol, primary, layer), "image/png")
            else:
                self.send_json(HTTPStatus.NOT_FOUND, {"error": "not found"})
        except (RuleError, OSError, KeyError, IndexError, struct.error, zlib.error) as error:
            self.send_json(HTTPStatus.BAD_REQUEST, {"error": str(error)})

    def do_POST(self) -> None:  # noqa: N802
        try:
            length = int(self.headers.get("Content-Length", "0"))
            if length <= 0 or length > 2_000_000:
                raise RuleError("invalid request size")
            payload = json.loads(self.rfile.read(length))
            if not isinstance(payload, dict):
                raise RuleError("request must be an object")
            if self.path == "/api/regenerate":
                if payload:
                    raise RuleError("regenerate request must be empty")
                with WRITE_LOCK:
                    command_output = run_rule_commands()
                self.send_json(HTTPStatus.OK, {
                    "ok": True,
                    "message": "Rules validated and regenerated",
                    "commands": command_output,
                })
                return
            destination, candidate = validate_payload(payload)
            if self.path == "/api/validate":
                validate_candidate(destination, candidate)
                self.send_json(HTTPStatus.OK, {"ok": True, "message": "Rules are valid"})
                return
            if self.path != "/api/save":
                self.send_json(HTTPStatus.NOT_FOUND, {"error": "not found"})
                return
            with WRITE_LOCK:
                current = destination.read_bytes()
                if payload.get("revision") != digest(current):
                    self.send_json(HTTPStatus.CONFLICT, {"error": "The rules file changed outside the editor; reopen the map"})
                    return
                validate_candidate(destination, candidate)
                descriptor, temporary_name = tempfile.mkstemp(prefix=destination.name + ".", suffix=".tmp", dir=destination.parent)
                try:
                    with os.fdopen(descriptor, "wb") as temporary:
                        temporary.write(candidate)
                        temporary.flush()
                        os.fsync(temporary.fileno())
                    os.replace(temporary_name, destination)
                finally:
                    if os.path.exists(temporary_name):
                        os.unlink(temporary_name)
                command_output = run_rule_commands()
            self.send_json(HTTPStatus.OK, {
                "ok": True,
                "revision": digest(candidate),
                "message": "Saved, validated and regenerated",
                "commands": command_output,
            })
        except json.JSONDecodeError as error:
            self.send_json(HTTPStatus.BAD_REQUEST, {"error": f"invalid JSON: {error}"})
        except (RuleError, OSError, KeyError, subprocess.SubprocessError) as error:
            self.send_json(HTTPStatus.BAD_REQUEST, {"error": str(error)})


def main() -> int:
    parser = argparse.ArgumentParser(description="Run the local diorama visual editor")
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--no-browser", action="store_true")
    args = parser.parse_args()
    if not 1 <= args.port <= 65535:
        parser.error("port must be between 1 and 65535")
    address = ("127.0.0.1", args.port)
    server = ThreadingHTTPServer(address, EditorHandler)
    url = f"http://{address[0]}:{address[1]}/"
    print(f"Diorama editor: {url}")
    if not args.no_browser:
        threading.Timer(0.25, webbrowser.open, args=(url,)).start()
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
