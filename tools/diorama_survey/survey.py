#!/usr/bin/env python3
"""Generate the global R1 survey plan and record independently captured views."""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
BMP_SIGNATURE = b"BM"


class SurveyError(ValueError):
    pass


def _canonical(value: object) -> bytes:
    return json.dumps(value, sort_keys=True, separators=(",", ":"),
                      ensure_ascii=True).encode("ascii")


def _sha256_bytes(value: bytes) -> str:
    return hashlib.sha256(value).hexdigest()


def _sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def _spot(source: dict, layout: dict, cells: list[dict]) -> tuple[int, int]:
    occupied = set()
    for event in source.get("object_events", []):
        x, y = event.get("x"), event.get("y")
        if isinstance(x, int) and isinstance(y, int):
            occupied.add((x, y))
    center = (layout["width"] // 2, layout["height"] // 2)

    def usable(cell: dict) -> bool:
        return "warp" not in cell.get("behaviorFamilies", [])

    tiers = (
        [(cell["x"], cell["y"]) for cell in cells
         if cell["collision"] == 0 and (cell["x"], cell["y"]) not in occupied
         and usable(cell)],
        [(cell["x"], cell["y"]) for cell in cells
         if cell["collision"] == 0 and usable(cell)],
        [(cell["x"], cell["y"]) for cell in cells
         if (cell["x"], cell["y"]) not in occupied and usable(cell)],
        [(cell["x"], cell["y"]) for cell in cells if usable(cell)],
        [center],
    )
    for candidates in tiers:
        candidates.sort(key=lambda point: (
            abs(point[0] - center[0]) + abs(point[1] - center[1]), point[1], point[0]))
        if candidates:
            return candidates[0]
    raise SurveyError(f"{layout['id']}: empty layout")


def _facing(value: str) -> str:
    aliases = {"sur": "south", "norte": "north", "oeste": "west", "este": "east"}
    return aliases.get(value, value)


def _layout_hash(cells: list[dict]) -> str:
    semantic = [{key: cell[key] for key in
                 ("x", "y", "metatile", "collision", "elevation", "tileset",
                  "localId", "behaviorId", "layerType")}
                for cell in cells]
    return _sha256_bytes(_canonical(semantic))


def build_plan(root: Path = ROOT, catalog: dict | None = None) -> dict:
    if catalog is None:
        import sys
        sys.path.insert(0, str(root / "tools/diorama_rules"))
        from catalog import build_catalog
        catalog = build_catalog(root)
    settings = json.loads((root / "data/diorama/red_parity_survey.json").read_text(
        encoding="utf-8"))
    layouts = {layout["id"]: layout for layout in catalog["layouts"]}
    layout_hashes = {layout_id: _layout_hash(cells)
                     for layout_id, cells in catalog["cells"].items()}
    views = settings["views"]
    scenario_document = json.loads((root / "data/diorama/red_parity_scenarios.json").read_text(
        encoding="utf-8"))
    scenario_by_map = {scenario["map"]: scenario for scenario in scenario_document["scenarios"]
                       if scenario["phase"] == "R1"}
    entries = []
    maps_by_directory = {item["directory"]: item for item in catalog["maps"]}
    covered_layouts = set()
    covered_tilesets = set()
    for map_row in catalog["maps"]:
        layout = layouts[map_row["layout"]]
        source = json.loads((root / "data/maps" / map_row["directory"] / "map.json").read_text(
            encoding="utf-8"))
        if map_row["sharedEventsMap"]:
            shared = maps_by_directory[map_row["sharedEventsMap"]]
            source = json.loads((root / "data/maps" / shared["directory"] / "map.json").read_text(
                encoding="utf-8"))
        scenario = scenario_by_map.get(map_row["symbol"])
        if scenario:
            x, y = scenario["position"]["x"], scenario["position"]["y"]
            facing = _facing(scenario["facing"])
        else:
            x, y = _spot(source, layout, catalog["cells"][layout["id"]])
            facing = "south"
        covered_layouts.add(layout["id"])
        covered_tilesets.update(symbol for symbol in
                                (layout["primary_tileset"], layout["secondary_tileset"])
                                if symbol != "0")
        selected_cell = catalog["cells"][layout["id"]][y * layout["width"] + x]
        stem = f"pokeemerald-survey-{map_row['group']}-{map_row['number']}_{x}_{y}_{facing}"
        entries.append({
            "id": map_row["symbol"], "kind": "map", "map": map_row["symbol"],
            "mapGroup": map_row["group"], "mapNum": map_row["number"],
            "layout": layout["id"], "layoutId": layout["numericId"],
            "position": {"x": x, "y": y}, "facing": facing,
            "staticCandidateWalkable": selected_cell["collision"] == 0,
            "reachability": ("candidate-walkable" if selected_cell["collision"] == 0
                             else "offline-only-unverified"),
            "layoutSnapshotSha256": layout_hashes[layout["id"]],
            "captures": [{"view": view["id"], "renderer": view["renderer"],
                          "pitch": view["pitch"],
                          "file": f"{stem}_run-1_{view['id']}.bmp"}
                         for view in views],
        })
    for layout in catalog["layouts"]:
        if layout["id"] in covered_layouts:
            continue
        covered_layouts.add(layout["id"])
        covered_tilesets.update(symbol for symbol in
                                (layout["primary_tileset"], layout["secondary_tileset"])
                                if symbol != "0")
        entries.append({
            "id": layout["id"], "kind": "layout-only", "map": None,
            "layout": layout["id"],
            "reason": "No default map references this layout; R1 inventories it offline.",
            "layoutSnapshotSha256": layout_hashes[layout["id"]], "captures": [],
        })
    for tileset in sorted(item["symbol"] for item in catalog["tilesets"]
                          if item["symbol"] not in covered_tilesets):
        covered_tilesets.add(tileset)
        entries.append({
            "id": tileset, "kind": "tileset-only", "map": None, "layout": None,
            "reason": "No layout references this logical tileset; R1 inventories it offline.",
            "captures": [],
        })
    plan = {
        "schemaVersion": 1,
        "settings": settings,
        "assets": {
            "corpusSha256": _sha256_bytes(_canonical({
                "maps": catalog["maps"], "layouts": catalog["layouts"],
                "tilesets": catalog["tilesets"], "layoutHashes": layout_hashes,
            })),
            "metatileCatalogSha256": _sha256_bytes(_canonical(catalog["metatiles"])),
            "animationCatalogSha256": _sha256_bytes(_canonical(catalog["animations"])),
            "rulesArtifactSha256": _sha256_file(
                root / "src/data/diorama/diorama_rules.generated.c"),
        },
        "summary": {
            "maps": len(catalog["maps"]), "layouts": len(covered_layouts),
            "tilesets": len(covered_tilesets), "mapSpots": len(catalog["maps"]),
            "layoutOnly": sum(entry["kind"] == "layout-only" for entry in entries),
            "tilesetOnly": sum(entry["kind"] == "tileset-only" for entry in entries),
            "captureNames": sum(len(entry["captures"]) for entry in entries),
        },
        "entries": entries,
    }
    plan["semanticSha256"] = _sha256_bytes(_canonical(plan))
    return plan


def write_plan(root: Path, output: Path) -> dict:
    plan = build_plan(root)
    output.mkdir(parents=True, exist_ok=True)
    (output / "plan.json").write_text(json.dumps(plan, indent=2) + "\n", encoding="utf-8")
    return plan


def _git_commit(root: Path) -> str:
    return subprocess.run(["git", "rev-parse", "HEAD"], cwd=root, check=True,
                          text=True, stdout=subprocess.PIPE).stdout.strip()


def record_captures(root: Path, plan: dict, captures: Path, output: Path,
                    selected_maps: set[str], save: Path | None = None,
                    config: Path | None = None, commit: str | None = None,
                    current_plan: dict | None = None, run: int = 1) -> dict:
    if run < 1:
        raise SurveyError("capture run must be positive")
    encoded_plan = {key: value for key, value in plan.items() if key != "semanticSha256"}
    if plan.get("semanticSha256") != _sha256_bytes(_canonical(encoded_plan)):
        raise SurveyError("survey plan semantic hash is invalid")
    current_plan = current_plan or build_plan(root)
    if plan["semanticSha256"] != current_plan["semanticSha256"]:
        raise SurveyError("survey plan does not match the current corpus and assets")
    entries = [entry for entry in plan["entries"]
               if entry["kind"] == "map" and entry["map"] in selected_maps]
    missing_maps = selected_maps - {entry["map"] for entry in entries}
    if missing_maps:
        raise SurveyError("unknown survey maps: " + ", ".join(sorted(missing_maps)))
    records = []
    for entry in entries:
        stable_state = None
        for capture in entry["captures"]:
            filename = capture["file"].replace("_run-1_", f"_run-{run}_")
            path = captures / filename
            if not path.is_file():
                raise SurveyError(f"missing capture {path}")
            header = path.read_bytes()[:54]
            if len(header) != 54 or header[:2] != BMP_SIGNATURE:
                raise SurveyError(f"{path}: not a BMP")
            declared_size = struct.unpack_from("<I", header, 2)[0]
            pixel_offset = struct.unpack_from("<I", header, 10)[0]
            dib_size = struct.unpack_from("<I", header, 14)[0]
            width, height = struct.unpack_from("<ii", header, 18)
            planes, bits_per_pixel = struct.unpack_from("<HH", header, 26)
            compression = struct.unpack_from("<I", header, 30)[0]
            expected_size = plan["settings"]["framebuffer"]
            if (width, abs(height)) != (expected_size["width"], expected_size["height"]):
                raise SurveyError(f"{path}: expected 960x640, found {width}x{abs(height)}")
            row_size = ((width * bits_per_pixel + 31) // 32) * 4
            required_size = pixel_offset + row_size * abs(height)
            actual_size = path.stat().st_size
            if (dib_size < 40 or planes != 1 or bits_per_pixel not in (24, 32)
                    or compression != 0 or height <= 0 or pixel_offset < 54
                    or declared_size != actual_size or actual_size != required_size):
                raise SurveyError(f"{path}: truncated or unsupported BMP")
            metadata_path = path.with_suffix(path.suffix + ".json")
            try:
                state = json.loads(metadata_path.read_text(encoding="utf-8"))
            except (OSError, json.JSONDecodeError) as error:
                raise SurveyError(f"{metadata_path}: missing or invalid capture metadata") from error
            expected_state = {
                "mapGroup": entry["mapGroup"], "mapNum": entry["mapNum"],
                "layoutId": entry["layoutId"], "x": entry["position"]["x"],
                "y": entry["position"]["y"], "facing": entry["facing"],
                "view": capture["view"], "width": expected_size["width"],
                "height": expected_size["height"],
                "captureRun": run, "sceneKind": 1, "fallbackReasons": 0,
            }
            if any(state.get(key) != value for key, value in expected_state.items()):
                raise SurveyError(f"{metadata_path}: runtime state does not match the plan")
            required_runtime = ("snapshotSequence", "mapGeneration", "mapEditGeneration",
                                "paletteGeneration", "objPaletteGeneration",
                                "animationGeneration")
            if not all(isinstance(state.get(key), int) for key in required_runtime):
                raise SurveyError(f"{metadata_path}: runtime generations are incomplete")
            identity = tuple(state[key] for key in required_runtime)
            if stable_state is None:
                stable_state = identity
            elif stable_state != identity:
                raise SurveyError(f"{metadata_path}: five views do not share one snapshot")
            camera = {"renderer": capture["renderer"], "pitch": capture["pitch"],
                      "framebuffer": plan["settings"]["framebuffer"],
                      "postprocessing": False}
            camera_hash = _sha256_bytes(_canonical(camera))
            semantic_hash = _sha256_bytes(_canonical({
                "layoutSnapshotSha256": entry["layoutSnapshotSha256"],
                "map": entry["map"], "layout": entry["layout"],
                "position": entry["position"], "facing": entry["facing"],
                "cameraSha256": camera_hash,
            }))
            records.append({**capture, "file": filename,
                            "map": entry["map"], "layout": entry["layout"],
                            "position": entry["position"], "facing": entry["facing"],
                            "layoutSnapshotSha256": entry["layoutSnapshotSha256"],
                            "cameraSha256": camera_hash, "semanticSha256": semantic_hash,
                            "runtimeState": state, "imageSha256": _sha256_file(path)})
    manifest = {
        "schemaVersion": 1, "commit": commit or _git_commit(root),
        "captureRun": run,
        "globalPlanSha256": plan["semanticSha256"],
        "assets": plan["assets"],
        "saveSha256": _sha256_file(save) if save else None,
        "configSha256": _sha256_file(config) if config else None,
        "driverTolerance": plan["settings"]["driverTolerance"],
        "captures": records,
    }
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    return manifest


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=ROOT)
    subparsers = parser.add_subparsers(dest="command", required=True)
    plan_parser = subparsers.add_parser("plan")
    plan_parser.add_argument("--output", type=Path)
    record_parser = subparsers.add_parser("record")
    record_parser.add_argument("--plan", type=Path)
    record_parser.add_argument("--captures", type=Path, required=True)
    record_parser.add_argument("--output", type=Path, required=True)
    record_parser.add_argument("--maps", required=True,
                               help="comma-separated MAP_* symbols already visited by the user")
    record_parser.add_argument("--save", type=Path)
    record_parser.add_argument("--config", type=Path)
    record_parser.add_argument("--run", type=int, default=1)
    args = parser.parse_args()
    root = args.root.resolve()
    try:
        if args.command == "plan":
            write_plan(root, args.output or root / "build/diorama_survey")
        else:
            plan_path = args.plan or root / "build/diorama_survey/plan.json"
            plan = json.loads(plan_path.read_text(encoding="utf-8"))
            record_captures(root, plan, args.captures, args.output,
                            set(args.maps.split(",")), args.save, args.config, run=args.run)
    except (OSError, json.JSONDecodeError, SurveyError, subprocess.CalledProcessError) as error:
        parser.error(str(error))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
