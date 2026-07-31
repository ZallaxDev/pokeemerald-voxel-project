#!/usr/bin/env python3
"""Audit global G5 activation and terrain-family coverage."""

from __future__ import annotations

import argparse
import json
from collections import Counter
from pathlib import Path

from catalog import load_tilesets, load_world
from compile_rules import _load_cells, compile_data, parse_behaviors


REFERENCE_MAPS = {
    "fortree": "MAP_FORTREE_CITY",
    "sootopolis": "MAP_SOOTOPOLIS_CITY",
    "cave": "MAP_GRANITE_CAVE_B1F",
    "mt-chimney": "MAP_MT_CHIMNEY",
    "jagged-pass": "MAP_JAGGED_PASS",
    "opening": "MAP_MT_PYRE_2F",
}


def build_audit(root: Path) -> dict:
    compiled = compile_data(root)
    tilesets = load_tilesets(root)
    maps, layouts = load_world(root)
    behavior_ids = parse_behaviors(root / "include/constants/metatile_behaviors.h")
    behavior_names = {value: name for name, value in behavior_ids.items()}
    cells, _, _ = _load_cells(root, layouts, tilesets, behavior_names)
    specialized = {row["behavior"] for row in compiled["behaviorRules"]}
    maps_by_symbol = {row["symbol"]: row for row in maps}
    reference_rows = {}
    for family, symbol in REFERENCE_MAPS.items():
        map_row = maps_by_symbol[symbol]
        counts = Counter(cell["behavior"] for cell in cells[map_row["layout"]]
                         if cell["behavior"] in specialized)
        reference_rows[family] = {
            "map": symbol,
            "layout": map_row["layout"],
            "specializedCells": sum(counts.values()),
            "behaviors": dict(sorted(counts.items())),
        }
    static_cells = sum(len(row) for row in cells.values())
    specialized_cells = sum(row["placementCount"] for row in compiled["behaviorRules"])
    terrain_classes = {compiled["default"]["terrainClass"]}
    terrain_classes.update(row["action"]["terrainClass"] for row in compiled["behaviorRules"])
    terrain_classes.update(row["action"]["terrainClass"] for row in compiled["tilesetPins"])
    return {
        "schemaVersion": 1,
        "rulesSha256": compiled["sha256"],
        "globalActivation": {
            "maps": sum(row["supported"] for row in compiled["maps"]),
            "mapsTotal": len(compiled["maps"]),
            "layouts": len(compiled["layouts"]),
            "tilesets": len(compiled["tilesets"]),
            "allMapsSupported": all(row["supported"] for row in compiled["maps"]),
        },
        "terrain": {
            "staticCells": static_cells,
            "baselineCells": static_cells,
            "specializedBehaviorCells": specialized_cells,
            "behaviorRules": len(compiled["behaviorRules"]),
            "terrainClasses": sorted(terrain_classes),
            "ambiguousPolicy": "flat-self-art",
        },
        "referenceFamilies": reference_rows,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--output", type=Path)
    parser.add_argument("--check", action="store_true")
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
        if activation != {"maps": 518, "mapsTotal": 518, "layouts": 441,
                          "tilesets": 75, "allMapsSupported": True}:
            return 1
        if any(not row["specializedCells"] for row in audit["referenceFamilies"].values()):
            return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
