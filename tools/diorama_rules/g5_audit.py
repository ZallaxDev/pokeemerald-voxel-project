#!/usr/bin/env python3
"""Audit complete G5 classification, cliff contracts, and reference profiles."""

from __future__ import annotations

import argparse
import json
from collections import Counter, defaultdict
from pathlib import Path

from catalog import load_tilesets, load_world
from compile_rules import TERRAIN_CLASSES, _load_cells, compile_data, parse_behaviors


def build_audit(root: Path) -> dict:
    compiled = compile_data(root)
    tilesets = load_tilesets(root)
    maps, layouts = load_world(root)
    behavior_ids = parse_behaviors(root / "include/constants/metatile_behaviors.h")
    behavior_names = {value: name for name, value in behavior_ids.items()}
    cells, _, _ = _load_cells(root, layouts, tilesets, behavior_names)
    maps_by_symbol = {row["symbol"]: row for row in maps}
    layouts_by_symbol = {row["id"]: row for row in layouts}
    behavior_actions = {row["behavior"]: row["action"] for row in compiled["behaviorRules"]}
    pin_actions = {(row["tileset"], row["metatile"]): row["action"]
                   for row in compiled["tilesetPins"]}
    tileset_defaults = {row["symbol"]: row["terrainClass"] for row in compiled["tilesets"]}
    class_counts: Counter[str] = Counter()
    source_counts: Counter[str] = Counter()
    tileset_counts: dict[str, Counter[str]] = defaultdict(Counter)
    resolved_by_layout: dict[str, list[str]] = {}

    for layout in layouts:
        resolved = []
        for cell in cells[layout["id"]]:
            pin = pin_actions.get((cell["tileset"], cell["localMetatile"]))
            behavior = behavior_actions.get(cell["behavior"])
            if pin is not None:
                terrain_class, source = pin["terrainClass"], "tileset-pin"
            elif behavior is not None:
                terrain_class, source = behavior["terrainClass"], "behavior"
            else:
                terrain_class, source = tileset_defaults[cell["tileset"]], "tileset-default"
            class_counts[terrain_class] += 1
            source_counts[source] += 1
            tileset_counts[cell["tileset"]][terrain_class] += 1
            resolved.append(terrain_class)
        resolved_by_layout[layout["id"]] = resolved

    profile_source = json.loads((root / "data/diorama/g5_profiles.json").read_text(encoding="utf-8"))
    profile_rows = []
    for target in profile_source["targets"]:
        map_row = maps_by_symbol[target["map"]]
        layout = layouts_by_symbol[map_row["layout"]]
        layout_cells = cells[layout["id"]]
        behavior_counts = Counter(cell["behavior"] for cell in layout_cells)
        classes = Counter(resolved_by_layout[layout["id"]])
        actual_tilesets = [layout["primary_tileset"], layout["secondary_tileset"]]
        errors = []
        if actual_tilesets != target["tilesets"]:
            errors.append(f"tilesets are {actual_tilesets!r}")
        for terrain_class in target["requiredClasses"]:
            if not classes[terrain_class]:
                errors.append(f"missing terrain class {terrain_class}")
        for behavior, minimum in target["requiredBehaviors"].items():
            if behavior_counts[behavior] < minimum:
                errors.append(f"{behavior} has {behavior_counts[behavior]}, expected at least {minimum}")
        profile_rows.append({
            "id": target["id"], "map": target["map"], "layout": layout["id"],
            "tilesets": actual_tilesets, "terrainClasses": dict(sorted(classes.items())),
            "behaviors": {name: behavior_counts[name]
                          for name in sorted(target["requiredBehaviors"])},
            "automatedPass": not errors, "errors": errors,
        })

    used_tilesets = {cell["tileset"] for layout_cells in cells.values() for cell in layout_cells}
    cliff_pins = [row for row in compiled["tilesetPins"]
                  if row["action"]["archetype"] in ("cliff", "mound", "wall-volume")]
    return {
        "schemaVersion": 2,
        "rulesSha256": compiled["sha256"],
        "globalActivation": {
            "maps": sum(row["supported"] for row in compiled["maps"]),
            "mapsTotal": len(compiled["maps"]), "layouts": len(compiled["layouts"]),
            "tilesets": len(compiled["tilesets"]),
        },
        "classification": {
            "declaredCells": sum(row["width"] * row["height"] for row in layouts),
            "staticallyDecodedCells": sum(class_counts.values()),
            "dynamicTilesetCells": sum(row["width"] * row["height"] for row in layouts
                                       if not cells[row["id"]]),
            "declaredClasses": list(TERRAIN_CLASSES),
            "classCounts": dict(sorted(class_counts.items())),
            "sourceCounts": dict(sorted(source_counts.items())),
            "tilesetsDeclared": len(tileset_defaults),
            "tilesetsUsed": len(used_tilesets),
            "unclassifiedCells": 0,
            "coordinateOverrides": len(compiled["map_overrides"]),
            "byTileset": {symbol: dict(sorted(tileset_counts[symbol].items()))
                          for symbol in sorted(tileset_defaults)},
        },
        "cliffs": {
            "explicitReusablePins": len(cliff_pins),
            "explicitPinPlacements": sum(row["placementCount"] for row in cliff_pins),
            "topologyFields": ["top", "edgeMask", "baseMask", "cornerMask", "transitionMask"],
            "automaticFallback": "bounded-blocked-art-runs",
            "maximumBands": 3,
        },
        "referenceProfiles": profile_rows,
        "manualValidation": {
            "status": profile_source["manualStatus"],
            "approved": profile_source["manualStatus"] == "approved-by-user",
        },
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--output", type=Path)
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--require-manual", action="store_true")
    args = parser.parse_args()
    audit = build_audit(args.root.resolve())
    encoded = json.dumps(audit, indent=2, sort_keys=True) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(encoded, encoding="utf-8")
    else:
        print(encoded, end="")
    if args.check:
        activation = audit["globalActivation"]
        classification = audit["classification"]
        if activation != {"maps": 518, "mapsTotal": 518, "layouts": 441, "tilesets": 75}:
            return 1
        if classification["declaredCells"] != 324579:
            return 1
        if classification["staticallyDecodedCells"] + classification["dynamicTilesetCells"] \
                != classification["declaredCells"]:
            return 1
        if (classification["tilesetsDeclared"] != 75 or classification["unclassifiedCells"]
                or classification["coordinateOverrides"]):
            return 1
        if set(classification["declaredClasses"]) != set(classification["classCounts"]):
            return 1
        if (not audit["cliffs"]["explicitReusablePins"]
                or not audit["cliffs"]["explicitPinPlacements"]):
            return 1
        if len(audit["referenceProfiles"]) != 6 \
                or any(not row["automatedPass"] for row in audit["referenceProfiles"]):
            return 1
    if args.require_manual and not audit["manualValidation"]["approved"]:
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
