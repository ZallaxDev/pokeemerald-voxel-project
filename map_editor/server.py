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
    compile_data,
    load_json,
    parse_behaviors,
)
from export_editor_map import build_editor_document  # noqa: E402
from catalog import CatalogError, load_tilesets, load_world  # noqa: E402
from emerald_compositor import (EmeraldCompositorError, METATILE_SIZE,  # noqa: E402
                                 TilesetComposer, encode_rgba_png)
from occupancy_model import editor_geometry  # noqa: E402
from terrain_volumes import resolve_volumes  # noqa: E402

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
    sources = map_sources()
    maps, _ = load_world(REPO_ROOT)
    result = []
    for item in maps:
        path = sources.get(item["symbol"])
        data = load_json(path) if path else None
        interior = item["mapType"] in ("MAP_TYPE_INDOOR", "MAP_TYPE_SECRET_BASE")
        result.append({
            "symbol": item["symbol"],
            "layout": item["layout"],
            "file": path.relative_to(REPO_ROOT).as_posix() if path else None,
            "camera": data.get("camera") if data else {
                "profile": "interior" if interior else "exterior"},
        })
    return sorted(result, key=lambda item: item["symbol"])


def normalize_rule(rule: dict) -> dict:
    archetype = rule.get("archetype")
    shape = {
        "ground": "flat", "void": "hidden", "ledge": "ledge",
        "water": "water", "shallow-water": "water", "waterfall": "water",
        "current": "water", "hot-spring": "water",
        "cliff": "cliff", "mound": "cliff", "wall-volume": "cliff",
        "bridge": "bridge", "deck": "bridge", "rail": "bridge", "support": "bridge",
        "stairs-n": "stairs", "stairs-s": "stairs", "stairs-e": "stairs",
        "stairs-w": "stairs", "stairs-down-n": "stairs",
        "stairs-down-s": "stairs", "stairs-down-e": "stairs",
        "stairs-down-w": "stairs",
    }.get(archetype, "cutout" if archetype else rule.get("shape", "flat"))
    faces = {
        face: {"metatile": "self", "layer": "foreground" if face == "plane" else "full"}
        for face in ("top", "north", "east", "south", "west", "plane")
    }
    for face, material in rule.get("faces", {}).items():
        faces[face] = material
    return {
        "shape": shape,
        "archetype": archetype,
        "profile": rule.get("profile", "none"),
        "semanticProfile": rule.get("profile"),
        "axis": rule.get("axis", "x"),
        "baseMetatile": rule.get("baseMetatile", "self"),
        "groundHeight": rule.get("groundOffset", rule.get("groundHeight", 0)),
        "height": rule.get("height", 0),
        "faces": faces,
    }


def load_templates() -> dict:
    templates = {}
    for path in sorted((REPO_ROOT / "data/diorama/buildings").glob("*.json")):
        templates.update(load_json(path).get("templates", {}))
    compiled = compile_data(REPO_ROOT)
    profiles = compiled["roof_profiles"]
    for name, row in zip(sorted(templates), compiled["templates"]):
        offset, count = row[12], row[13]
        templates[name]["compiledRoofProfile"] = profiles[offset:offset + count]
    return templates


def tileset_rule_document(symbol: str) -> dict:
    for path in sorted((REPO_ROOT / "data/diorama/tilesets").glob("*.json")):
        data = load_json(path)
        if data.get("tileset") == symbol:
            return data
    return {"pins": []}


def enrich_document(document: dict) -> dict:
    defaults = load_json(REPO_ROOT / "data/diorama/defaults.json")
    behavior_ids = parse_behaviors(REPO_ROOT / "include/constants/metatile_behaviors.h")
    behavior_names = {value: key for key, value in behavior_ids.items()}
    templates = load_templates()
    if document["rules"] is None:
        interior = document["map"]["mapType"] in ("MAP_TYPE_INDOOR", "MAP_TYPE_SECRET_BASE")
        document["rules"] = {
            "schemaVersion": 2,
            "kind": "map",
            "map": document["map"]["symbol"],
            "layout": document["map"]["layout"],
            "status": {"supported": True},
            "camera": {"profile": "interior" if interior else "exterior"},
            "groundPolicy": {"mode": "automatic"},
            "contextualRules": [],
            "exactPatterns": [],
        }
    rules = document["rules"]
    behavior_rules = {item["behavior"]: item["action"]
                      for item in defaults.get("behaviorRules", [])}
    tileset_docs = {
        role: tileset_rule_document(info["symbol"])
        for role, info in document["tilesets"].items()
    }
    resolved = []
    for cell in document["cells"]:
        source = "fallback"
        rule = defaults["default"]
        pin = next((item for item in tileset_docs[cell["tilesetRole"]].get("pins", [])
                    if item["metatile"] == cell["localMetatile"]), None)
        if pin is not None:
            source, rule = "tileset pin", pin["action"]
        else:
            behavior = behavior_names.get(cell["behavior"])
            if behavior in behavior_rules:
                source, rule = f"behavior:{behavior}", behavior_rules[behavior]
            elif cell["collision"] or cell["elevation"] not in (0, 3, 15):
                source = "collision/elevation"
            elif cell["layerType"]:
                source = "visual heuristic"
        resolved.append({"source": source, "rule": normalize_rule(rule)})

    resolve_volumes(document["cells"], resolved,
                    document["map"]["width"], document["map"]["height"])

    tilesets = load_tilesets(REPO_ROOT)
    primary_info = tilesets[document["tilesets"]["primary"]["symbol"]]
    composers = {}
    for role, info in document["tilesets"].items():
        selected = tilesets[info["symbol"]]
        composers[role] = TilesetComposer(
            REPO_ROOT / primary_info.root, REPO_ROOT / selected.root,
            REPO_ROOT / primary_info.metatiles_root,
            REPO_ROOT / selected.metatiles_root)
    alpha_masks = {}
    for cell in document["cells"]:
        key = (cell["tilesetRole"], cell["metatile"])
        if key not in alpha_masks:
            foreground = composers[cell["tilesetRole"]].compose_metatile(cell["metatile"])[1]
            alpha_masks[key] = [sum((1 << x) for x in range(16)
                                    if foreground[y * 16 + x].rgba[3])
                                for y in range(16)]
        cell["foregroundAlpha"] = alpha_masks[key]
    for role, info in document["tilesets"].items():
        relative = tilesets[info["symbol"]].metatiles_root
        count = (REPO_ROOT / relative / "metatile_attributes.bin").stat().st_size // 2
        info["count"] = count
        attributes = struct.unpack(
            f"<{count}H",
            (REPO_ROOT / relative / "metatile_attributes.bin").read_bytes(),
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
        "revision": digest((REPO_ROOT / document["ruleSource"]).read_bytes())
                    if document["ruleSource"] else digest(json_bytes(rules)),
        "readOnly": document["ruleSource"] is None,
        "behaviorNames": {str(key): value for key, value in behavior_names.items()},
        "templates": templates,
        "resolved": resolved,
    }
    document["geometry"] = editor_geometry(document["cells"], resolved,
                                           defaults.get("profiles", []))
    return document


def build_atlas(symbol: str, primary_symbol: str | None = None, layer_name: str = "full") -> bytes:
    tilesets = load_tilesets(REPO_ROOT)
    if symbol not in tilesets:
        raise RuleError("unknown tileset")
    if layer_name not in ("full", "base", "foreground"):
        raise RuleError("unknown atlas layer")
    info = tilesets[symbol]
    if info.role == "secondary":
        if primary_symbol not in tilesets or tilesets[primary_symbol].role != "primary":
            raise RuleError("secondary atlas requires its primary tileset")
        primary_info = tilesets[primary_symbol]
    else:
        primary_info = info
    primary_root = REPO_ROOT / primary_info.root
    selected_root = REPO_ROOT / info.root
    try:
        composer = TilesetComposer(
            primary_root, selected_root,
            REPO_ROOT / primary_info.metatiles_root,
            REPO_ROOT / info.metatiles_root)
    except EmeraldCompositorError as error:
        raise RuleError(str(error)) from error
    secondary = info.role == "secondary"
    count = composer.metatile_count(secondary)
    columns, size = 16, METATILE_SIZE
    width = columns * size
    height = ((count + columns - 1) // columns) * size
    output = bytearray(width * height * 4)
    layer_index = {"base": 0, "foreground": 1, "full": 2}[layer_name]
    for metatile in range(count):
        origin_x = (metatile % columns) * size
        origin_y = (metatile // columns) * size
        global_id = metatile + (0x200 if secondary else 0)
        layer = composer.compose_metatile(global_id)[layer_index]
        for py in range(size):
            for px in range(size):
                target = ((origin_y + py) * width + origin_x + px) * 4
                output[target:target + 4] = bytes(layer[py * size + px].rgba)
    return encode_rgba_png(width, height, bytes(output))


def validate_candidate(destination: Path, candidate: bytes) -> None:
    with tempfile.TemporaryDirectory(prefix="diorama-editor-") as temporary:
        root = Path(temporary)
        (root / "data").mkdir()
        shutil.copytree(REPO_ROOT / "data/diorama", root / "data/diorama")
        for name in ("maps", "layouts", "tilesets"):
            os.symlink(REPO_ROOT / "data" / name, root / "data" / name, target_is_directory=True)
        os.symlink(REPO_ROOT / "src", root / "src", target_is_directory=True)
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
                if symbol not in {item["symbol"] for item in map_listing()}:
                    raise RuleError("unknown map")
                self.send_json(HTTPStatus.OK, enrich_document(build_editor_document(REPO_ROOT, symbol)))
            elif parsed.path == "/api/atlas":
                query = parse_qs(parsed.query)
                symbol = query.get("symbol", [None])[0]
                role = query.get("role", [None])[0]
                tilesets = load_tilesets(REPO_ROOT)
                if symbol not in tilesets or role not in ("primary", "secondary") or tilesets[symbol].role != role:
                    raise RuleError("invalid tileset atlas request")
                primary = query.get("primary", [None])[0]
                layer = query.get("layer", ["full"])[0]
                self.send_bytes(HTTPStatus.OK, build_atlas(symbol, primary, layer), "image/png")
            else:
                self.send_json(HTTPStatus.NOT_FOUND, {"error": "not found"})
        except (RuleError, CatalogError, OSError, KeyError, IndexError, struct.error) as error:
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
