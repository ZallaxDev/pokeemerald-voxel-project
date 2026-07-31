#!/usr/bin/env python3
"""Export one authoritative Emerald map as input for a visual diorama editor."""

from __future__ import annotations

import argparse
import json
import struct
import sys
from pathlib import Path

from catalog import load_tilesets
from compile_rules import RuleError, compile_data, load_json


def find_map(root: Path, symbol: str) -> tuple[Path, dict]:
    for path in sorted((root / "data/maps").glob("*/map.json")):
        data = json.loads(path.read_text(encoding="utf-8"))
        if data.get("id") == symbol:
            return path, data
    raise RuleError(f"unknown map {symbol}")


def find_layout(root: Path, symbol: str) -> dict:
    catalog = json.loads((root / "data/layouts/layouts.json").read_text(encoding="utf-8"))
    for index, layout in enumerate(catalog["layouts"], start=1):
        if layout.get("id") == symbol:
            return {**layout, "numeric_id": index}
    raise RuleError(f"unknown layout {symbol}")


def find_rule_source(root: Path, symbol: str) -> tuple[Path | None, dict | None]:
    for path in sorted((root / "data/diorama/maps").glob("*.json")):
        data = load_json(path)
        if data.get("map") == symbol:
            return path, data
    return None, None


def read_attributes(root: Path, tileset: str) -> tuple[str, list[int]]:
    tilesets = load_tilesets(root)
    if tileset not in tilesets:
        raise RuleError(f"tileset {tileset} is not registered for diorama editing")
    relative = tilesets[tileset].metatiles_root
    path = root / relative / "metatile_attributes.bin"
    raw = path.read_bytes()
    if len(raw) % 2:
        raise RuleError(f"{path}: invalid attribute table size")
    return relative, list(struct.unpack(f"<{len(raw) // 2}H", raw))


def build_editor_document(root: Path, symbol: str) -> dict:
    compile_data(root)
    map_path, map_data = find_map(root, symbol)
    layout = find_layout(root, map_data["layout"])
    rule_path, rule_data = find_rule_source(root, symbol)
    primary_path, primary_attributes = read_attributes(root, layout["primary_tileset"])
    secondary_path, secondary_attributes = read_attributes(root, layout["secondary_tileset"])
    raw = (root / layout["blockdata_filepath"]).read_bytes()
    expected_size = layout["width"] * layout["height"] * 2
    if len(raw) != expected_size:
        raise RuleError(f"{layout['blockdata_filepath']}: expected {expected_size} bytes, got {len(raw)}")

    cells = []
    for index, (entry,) in enumerate(struct.iter_unpack("<H", raw)):
        metatile = entry & 0x3FF
        secondary = metatile >= 512
        local_id = metatile - 512 if secondary else metatile
        attributes = secondary_attributes[local_id] if secondary else primary_attributes[local_id]
        cells.append({
            "id": f"{index % layout['width']},{index // layout['width']}",
            "x": index % layout["width"],
            "y": index // layout["width"],
            "metatile": metatile,
            "tilesetRole": "secondary" if secondary else "primary",
            "localMetatile": local_id,
            "behavior": attributes & 0xFF,
            "layerType": attributes >> 12,
            "collision": (entry >> 10) & 3,
            "elevation": entry >> 12,
        })

    def relative(path: Path) -> str:
        return path.relative_to(root).as_posix()

    return {
        "schemaVersion": 2,
        "map": {
            "symbol": symbol,
            "source": relative(map_path),
            "layout": layout["id"],
            "layoutId": layout["numeric_id"],
            "width": layout["width"],
            "height": layout["height"],
            "mapType": map_data.get("map_type"),
        },
        "tilesets": {
            "primary": {
                "symbol": layout["primary_tileset"],
                "path": f"data/tilesets/{primary_path}",
                "tilesImage": f"data/tilesets/{primary_path}/tiles.png",
                "metatiles": f"data/tilesets/{primary_path}/metatiles.bin",
                "attributes": f"data/tilesets/{primary_path}/metatile_attributes.bin",
                "palettes": f"data/tilesets/{primary_path}/palettes",
            },
            "secondary": {
                "symbol": layout["secondary_tileset"],
                "path": f"data/tilesets/{secondary_path}",
                "tilesImage": f"data/tilesets/{secondary_path}/tiles.png",
                "metatiles": f"data/tilesets/{secondary_path}/metatiles.bin",
                "attributes": f"data/tilesets/{secondary_path}/metatile_attributes.bin",
                "palettes": f"data/tilesets/{secondary_path}/palettes",
            },
        },
        "ruleSource": relative(rule_path) if rule_path else None,
        "rules": rule_data,
        "cells": cells,
        "events": {
            "objects": map_data.get("object_events", []),
            "warps": map_data.get("warp_events", []),
            "coordinates": map_data.get("coord_events", []),
            "background": map_data.get("bg_events", []),
        },
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("map", help="map symbol, for example MAP_LITTLEROOT_TOWN")
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    try:
        document = build_editor_document(args.root.resolve(), args.map)
        output = json.dumps(document, indent=2, ensure_ascii=True) + "\n"
        if args.output:
            args.output.write_text(output, encoding="utf-8")
        else:
            sys.stdout.write(output)
    except (RuleError, OSError, KeyError, IndexError, struct.error) as error:
        print(f"diorama editor export: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
