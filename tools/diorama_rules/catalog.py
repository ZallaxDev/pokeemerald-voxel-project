#!/usr/bin/env python3
"""Generate the authoritative global map, layout, tileset and usage catalogs."""

from __future__ import annotations

import argparse
import hashlib
import html
import json
import re
import shutil
import struct
import zlib
from collections import Counter, defaultdict
from dataclasses import asdict, dataclass
from pathlib import Path

from emerald_compositor import (METATILE_SIZE, TilesetComposer, decode_indexed_png,
                                encode_rgba_png, load_animation_slots)


LAYER_TYPES = {0: "normal", 1: "covered", 2: "split"}
PATTERN_SIZES = ((2, 2), (3, 2), (2, 3), (3, 3))
MAX_STRUCTURE_CANDIDATES = 5000


class CatalogError(ValueError):
    pass


@dataclass(frozen=True)
class TilesetInfo:
    symbol: str
    role: str
    compressed: bool
    tiles_symbol: str
    palettes_symbol: str
    metatiles_symbol: str
    attributes_symbol: str
    callback: str
    root: str
    metatiles_root: str
    metatile_count: int
    graphics_key: str
    metatiles_key: str


def _json(path: Path) -> dict:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise CatalogError(f"{path}: {error}") from error


def _incbin_symbols(path: Path) -> dict[str, str]:
    text = path.read_text(encoding="utf-8")
    return dict(re.findall(
        r"\b(?:const\s+)?(?:u8|u16|u32)\s+(g[A-Za-z0-9_]+)(?:\[\])?(?:\[16\])?\s*=.*?"
        r"INCBIN_(?:U8|U16|U32)\(\"([^\"]+)\"\)", text, re.S))


def load_tilesets(root: Path) -> dict[str, TilesetInfo]:
    resources = {}
    resources.update(_incbin_symbols(root / "src/data/tilesets/graphics.h"))
    resources.update(_incbin_symbols(root / "src/data/tilesets/metatiles.h"))
    # General graphics are declared with the rest of the global graphics data.
    resources.update(_incbin_symbols(root / "src/graphics.c"))
    text = (root / "src/data/tilesets/headers.h").read_text(encoding="utf-8")
    blocks = re.findall(r"const struct Tileset\s+(gTileset_[A-Za-z0-9_]+)\s*=\s*\{(.*?)\};", text, re.S)
    result = {}
    for symbol, body in blocks:
        fields = dict(re.findall(r"\.(\w+)\s*=\s*([A-Za-z0-9_]+)", body))
        required = ("isCompressed", "isSecondary", "tiles", "palettes", "metatiles",
                    "metatileAttributes", "callback")
        missing = [field for field in required if field not in fields]
        if missing:
            raise CatalogError(f"{symbol}: missing header fields: {', '.join(missing)}")
        try:
            metatile_path = resources[fields["metatiles"]]
            tiles_path = resources[fields["tiles"]]
            palette_path = resources[fields["palettes"]]
            attributes_path = resources[fields["metatileAttributes"]]
        except KeyError as error:
            raise CatalogError(f"{symbol}: unresolved resource {error.args[0]}") from error
        asset_root = Path(tiles_path).parent
        metatiles_root = Path(metatile_path).parent
        palette_root = Path(palette_path).parent.parent
        if asset_root != palette_root:
            raise CatalogError(f"{symbol}: tiles and palettes use different asset roots")
        for expected in (metatiles_root / "metatiles.bin",
                         Path(attributes_path), asset_root / "tiles.png",
                         asset_root / "palettes/00.gbapal"):
            if not (root / expected).is_file():
                raise CatalogError(f"{symbol}: missing {expected}")
        metatile_size = (root / metatile_path).stat().st_size
        attribute_size = (root / attributes_path).stat().st_size
        if metatile_size % 16 or attribute_size * 8 != metatile_size:
            raise CatalogError(f"{symbol}: metatile and attribute table sizes disagree")
        result[symbol] = TilesetInfo(
            symbol=symbol,
            role="secondary" if fields["isSecondary"] == "TRUE" else "primary",
            compressed=fields["isCompressed"] == "TRUE",
            tiles_symbol=fields["tiles"],
            palettes_symbol=fields["palettes"],
            metatiles_symbol=fields["metatiles"],
            attributes_symbol=fields["metatileAttributes"],
            callback=fields["callback"],
            root=asset_root.as_posix(),
            metatiles_root=metatiles_root.as_posix(),
            metatile_count=metatile_size // 16,
            graphics_key=f"{tiles_path}|{palette_path}",
            metatiles_key=f"{metatile_path}|{attributes_path}",
        )
    if len(result) != 75:
        raise CatalogError(f"expected 75 logical tilesets, found {len(result)}")
    return result


def load_world(root: Path) -> tuple[list[dict], list[dict]]:
    layouts_source = _json(root / "data/layouts/layouts.json")["layouts"]
    layouts = []
    by_symbol = {}
    for layout_id, item in enumerate(layouts_source, 1):
        entry = dict(item)
        entry["numericId"] = layout_id
        entry["maps"] = []
        layouts.append(entry)
        by_symbol[item["id"]] = entry
    groups = _json(root / "data/maps/map_groups.json")
    maps = []
    for group_id, group_name in enumerate(groups["group_order"]):
        for map_number, directory in enumerate(groups[group_name]):
            path = root / "data/maps" / directory / "map.json"
            source = _json(path)
            if source["layout"] not in by_symbol:
                raise CatalogError(f"{path}: unknown layout {source['layout']}")
            events = {
                "objects": len(source.get("object_events", [])),
                "warps": len(source.get("warp_events", [])),
                "coordinates": len(source.get("coord_events", [])),
                "background": len(source.get("bg_events", [])),
                "signs": sum(event.get("type") == "sign" for event in source.get("bg_events", [])),
            }
            entry = {
                "symbol": source["id"], "name": source.get("name", directory),
                "directory": directory, "group": group_id, "number": map_number,
                "groupName": group_name, "layout": source["layout"],
                "mapType": source.get("map_type"), "weather": source.get("weather"),
                "sharedEventsMap": source.get("shared_events_map"),
                "events": events,
            }
            maps.append(entry)
            by_symbol[source["layout"]]["maps"].append(source["id"])
    return maps, layouts


def _layout_cells(root: Path, layout: dict) -> tuple[tuple[int, int, int], ...]:
    width, height = layout["width"], layout["height"]
    path = root / layout["blockdata_filepath"]
    raw = path.read_bytes()
    expected_size = width * height * 2
    if len(raw) < expected_size or len(raw) % 2:
        raise CatalogError(f"{path}: expected at least {expected_size} even bytes, found {len(raw)}")
    layout["sourceBlockCount"] = len(raw) // 2
    if len(raw) != expected_size:
        layout["sourceSizeMismatch"] = True
    return tuple((value & 0x3FF, (value >> 10) & 3, (value >> 12) & 15)
                 for (value,) in struct.iter_unpack("<H", raw[:expected_size]))


def _layout_usage(root: Path, layout: dict,
                  cells: tuple[tuple[int, int, int], ...] | None = None) -> dict:
    width, height = layout["width"], layout["height"]
    cells = cells or _layout_cells(root, layout)
    metatiles = tuple(cell[0] for cell in cells)
    frequencies = Counter(metatiles)
    neighbors: dict[int, Counter] = defaultdict(Counter)
    coordinates: dict[int, list[list[int]]] = defaultdict(list)
    for y in range(height):
        for x in range(width):
            value = metatiles[y * width + x]
            coordinates[value].append([x, y])
            for dx, dy, direction in ((0, -1, "n"), (1, 0, "e"), (0, 1, "s"), (-1, 0, "w")):
                nx, ny = x + dx, y + dy
                if 0 <= nx < width and 0 <= ny < height:
                    neighbors[value][f"{direction}:{metatiles[ny * width + nx]}"] += 1
    return {
        str(value): {
            "count": frequencies[value],
            "coordinates": coordinates[value],
            "neighbors": dict(sorted(neighbors[value].items())),
            "collision": dict(sorted(Counter(str(cells[y * width + x][1])
                                                for x, y in coordinates[value]).items())),
            "elevation": dict(sorted(Counter(str(cells[y * width + x][2])
                                                for x, y in coordinates[value]).items())),
            "contexts": dict(sorted(Counter(
                f"{cells[y * width + x][1]}:{cells[y * width + x][2]}"
                for x, y in coordinates[value]).items())),
        }
        for value in sorted(frequencies)
    }


def _parse_behaviors(path: Path) -> tuple[dict[str, int], dict[int, str]]:
    text = re.sub(r"//[^\n]*|/\*.*?\*/", "", path.read_text(encoding="utf-8"), flags=re.S)
    match = re.search(r"enum\s*\{(.*?)\};", text, flags=re.S)
    if match is None:
        raise CatalogError(f"{path}: behavior enum not found")
    by_name: dict[str, int] = {}
    value = -1
    for entry in match.group(1).split(","):
        item = re.search(r"\b(MB_[A-Z0-9_]+)\b(?:\s*=\s*(0x[0-9A-Fa-f]+|[0-9]+))?", entry)
        if item is None:
            continue
        value = int(item.group(2), 0) if item.group(2) else value + 1
        by_name[item.group(1)] = value
    return by_name, {number: name for name, number in by_name.items()}


def _behavior_families(name: str) -> list[str]:
    families = []
    if any(token in name for token in ("WATER", "OCEAN", "SEAWEED", "PUDDLE",
                                        "HOT_SPRINGS", "CURRENT")):
        families.append("water")
    if "GRASS" in name:
        families.append("grass")
    if name.startswith("MB_JUMP_"):
        families.append("ledge")
    if "BRIDGE" in name or "PACIFIDLOG_" in name and "LOG" in name:
        families.append("bridge")
    if any(token in name for token in ("STAIRS", "LADDER", "ESCALATOR")):
        families.append("stairs")
    if any(token in name for token in ("WARP", "DOOR", "LADDER", "ESCALATOR", "HOLE")):
        families.append("warp")
    if any(token in name for token in ("ICE", "SAND", "SLOPE", "RAIL", "COUNTER",
                                        "SECRET_BASE", "PC", "SHELF", "TELEVISION")):
        families.append("special")
    return families


def _attributes(root: Path, tilesets: dict[str, TilesetInfo]) -> dict[str, tuple[tuple[int, int], ...]]:
    result = {}
    for symbol, info in tilesets.items():
        raw = (root / info.metatiles_root / "metatile_attributes.bin").read_bytes()
        result[symbol] = tuple((value & 0xFF, (value >> 12) & 0xF)
                               for (value,) in struct.iter_unpack("<H", raw))
    return result


def _cell_context(layout: dict, cells: tuple[tuple[int, int, int], ...], x: int, y: int,
                  attributes: dict[str, tuple[tuple[int, int], ...]],
                  behavior_names: dict[int, str]) -> dict | None:
    if not (0 <= x < layout["width"] and 0 <= y < layout["height"]):
        return None
    metatile, collision, elevation = cells[y * layout["width"] + x]
    symbol = layout["primary_tileset"] if metatile < 0x200 else layout["secondary_tileset"]
    local_id = metatile if metatile < 0x200 else metatile - 0x200
    behavior = layer_type = None
    if symbol in attributes and local_id < len(attributes[symbol]):
        behavior, layer_type = attributes[symbol][local_id]
    return {"metatile": metatile, "localId": local_id, "tileset": symbol,
            "behavior": behavior_names.get(behavior, f"UNKNOWN_{behavior}") if behavior is not None else None,
            "behaviorId": behavior, "layerType": LAYER_TYPES.get(layer_type, f"unknown-{layer_type}"),
            "collision": collision, "elevation": elevation}


def _classify_object(event: dict) -> list[str]:
    graphics = event.get("graphics_id", "")
    script = event.get("script", "")
    classes = ["object"]
    signatures = {
        "cutObstacle": ("OBJ_EVENT_GFX_CUTTABLE_TREE", "EventScript_CutTree"),
        "rockSmashObstacle": ("OBJ_EVENT_GFX_BREAKABLE_ROCK", "EventScript_RockSmash"),
        "berryTree": ("OBJ_EVENT_GFX_BERRY_TREE", "BerryTreeScript"),
        "strengthBoulder": ("OBJ_EVENT_GFX_PUSHABLE_BOULDER", "EventScript_StrengthBoulder"),
    }
    for name, (expected_graphics, expected_script) in signatures.items():
        if graphics == expected_graphics or script == expected_script:
            classes.append(name)
    if event.get("flag", "0") != "0" or classes[-1] != "object":
        classes.append("dynamicObstacle")
    return classes


def _event_inventory(root: Path, maps: list[dict], layouts: list[dict],
                     cells_by_layout: dict[str, tuple[tuple[int, int, int], ...]],
                     attributes: dict[str, tuple[tuple[int, int], ...]],
                     behavior_names: dict[int, str]) -> dict:
    layout_by_id = {layout["id"]: layout for layout in layouts}
    events = []
    counts = Counter()
    for map_row in maps:
        source = _json(root / "data/maps" / map_row["directory"] / "map.json")
        layout = layout_by_id[map_row["layout"]]
        cells = cells_by_layout[layout["id"]]
        groups = (("object", source.get("object_events", [])),
                  ("warp", source.get("warp_events", [])),
                  ("coordinate", source.get("coord_events", [])),
                  ("background", source.get("bg_events", [])))
        for kind, records in groups:
            for index, record in enumerate(records):
                item = {"id": f"{map_row['symbol']}:{kind}:{index + 1}", "map": map_row["symbol"],
                        "layout": layout["id"], "kind": kind, "x": record.get("x"),
                        "y": record.get("y"), "elevation": record.get("elevation"),
                        "source": record}
                if kind == "object":
                    item["classes"] = _classify_object(record)
                elif kind == "background":
                    item["classes"] = [record.get("type", "background")]
                elif kind == "coordinate":
                    item["classes"] = [record.get("type", "coordinate")]
                else:
                    item["classes"] = ["warp"]
                if isinstance(item["x"], int) and isinstance(item["y"], int):
                    item["cell"] = _cell_context(layout, cells, item["x"], item["y"],
                                                  attributes, behavior_names)
                    if kind == "warp" and item["cell"] and "DOOR" in (item["cell"]["behavior"] or ""):
                        item["classes"].append("door")
                for classification in item["classes"]:
                    counts[classification] += 1
                events.append(item)
    decorations = []
    text = (root / "src/data/decoration/header.h").read_text(encoding="utf-8")
    for match in re.finditer(r"\[(DECOR_[A-Z0-9_]+)\]\s*=\s*\{(.*?)\n\s*\},", text, re.S):
        fields = dict(re.findall(r"\.(id|permission|shape|category|tiles)\s*=\s*([A-Za-z0-9_]+)", match.group(2)))
        name = re.search(r'\.name\s*=\s*_\("([^"]*)"\)', match.group(2))
        shape = fields.get("shape", "")
        dimensions = re.search(r"_(\d)x(\d)$", shape)
        decorations.append({"id": fields.get("id", match.group(1)),
                            "name": name.group(1) if name else None,
                            "permission": fields.get("permission"), "shape": shape,
                            "width": int(dimensions.group(1)) if dimensions else None,
                            "height": int(dimensions.group(2)) if dimensions else None,
                            "category": fields.get("category"), "tiles": fields.get("tiles")})
    return {"events": events, "counts": dict(sorted(counts.items())),
            "decorations": decorations,
            "runtimePlacementNote": "Decoration placements are save-backed and must be read from the live map grid."}


def _runtime_changes(root: Path, maps: list[dict], layouts: list[dict]) -> dict:
    map_by_directory = {item["directory"]: item["symbol"] for item in maps}
    layout_targets: dict[str, set[str]] = defaultdict(set)
    metatile_changes = []
    for path in sorted((root / "data/maps").glob("*/scripts.inc")):
        text = path.read_text(encoding="utf-8")
        map_symbol = map_by_directory.get(path.parent.name)
        for target in re.findall(r"\bsetmaplayoutindex\s+(LAYOUT_[A-Z0-9_]+)", text):
            if map_symbol:
                layout_targets[target].add(map_symbol)
        for line, match in enumerate(re.finditer(
                r"\bsetmetatile\s+([^,\n]+),\s*([^,\n]+),\s*([^,\n]+),\s*([^\s\n]+)", text), 1):
            metatile_changes.append({"map": map_symbol, "file": path.relative_to(root).as_posix(),
                                     "ordinal": line, "x": match.group(1).strip(),
                                     "y": match.group(2).strip(), "metatile": match.group(3).strip(),
                                     "collision": match.group(4).strip()})
    rows = []
    default_maps = defaultdict(list)
    for item in maps:
        default_maps[item["layout"]].append(item["symbol"])
    for layout in layouts:
        runtime_maps = sorted(layout_targets.get(layout["id"], set()))
        if default_maps[layout["id"]]:
            classification = "default"
        elif runtime_maps:
            classification = "runtime-alternate"
        elif layout.get("dynamicTilesets"):
            classification = "dynamic-tileset"
        else:
            classification = "unreferenced"
        layout["runtimeMaps"] = runtime_maps
        layout["runtimeClassification"] = classification
        rows.append({"layout": layout["id"], "numericId": layout["numericId"],
                     "classification": classification, "defaultMaps": sorted(default_maps[layout["id"]]),
                     "runtimeMaps": runtime_maps, "sourceSizeMismatch": layout.get("sourceSizeMismatch", False)})
    return {"layouts": rows, "setMapLayoutCalls": sum(len(value) for value in layout_targets.values()),
            "metatileChanges": metatile_changes,
            "generatedMaps": [
                {"system": "Battle Pyramid", "source": "src/battle_pyramid.c"},
                {"system": "Trainer Hill", "source": "src/trainer_hill.c"},
            ]}


def _animation_inventory(root: Path, tilesets: dict[str, TilesetInfo]) -> dict:
    slots_by_callback = load_animation_slots(root)
    slots = []
    for symbol, info in sorted(tilesets.items()):
        static_tile_count = (root / info.root / "tiles.4bpp").stat().st_size // 32
        png_width, png_height, _ = decode_indexed_png(root / info.root / "tiles.png")
        png_tile_capacity = png_width * png_height // 64
        for slot in slots_by_callback.get(info.callback, ()):
            slot_tiles = range(slot.local_start, slot.local_start + slot.tile_count)
            slots.append({
                "source": slot.source,
                "callback": slot.callback,
                "role": slot.role,
                "localStart": slot.local_start,
                "globalStart": slot.local_start + (0x200 if slot.role == "secondary" else 0),
                "tileCount": slot.tile_count,
                "tilesets": [symbol],
                "staticTileCount": static_tile_count,
                "pngTileCapacity": png_tile_capacity,
                "absentStaticTiles": [tile for tile in slot_tiles if tile >= static_tile_count],
                "absentPngTiles": [tile for tile in slot_tiles if tile >= png_tile_capacity],
                "frames": [[path.relative_to(root).as_posix() for path in frame]
                           for frame in slot.frames],
            })
    text = (root / "src/tileset_anims.c").read_text(encoding="utf-8")
    palette = [{"tileset": symbol, "callback": info.callback, "palette": 8}
               for symbol, info in tilesets.items() if info.callback == "InitTilesetAnim_BattleDome"]
    return {"tileSlots": slots, "paletteSlots": palette}


def _subtiles(root: Path, info: TilesetInfo, local_id: int,
               animation_slots: list[dict], primary: str, secondary: str) -> list[dict]:
    raw = (root / info.metatiles_root / "metatiles.bin").read_bytes()
    values = struct.unpack_from("<8H", raw, local_id * 16)
    result = []
    for index, value in enumerate(values):
        tile = value & 0x3FF
        role = "secondary" if tile >= 0x200 else "primary"
        graphics_tileset = secondary if role == "secondary" else primary
        local_tile = tile - 0x200 if role == "secondary" else tile
        animations = [slot["source"] for slot in animation_slots
                      if graphics_tileset in slot["tilesets"] and slot["role"] == role
                      and slot["localStart"] <= local_tile
                      < slot["localStart"] + slot["tileCount"]]
        result.append({"layer": index // 4, "subtile": index % 4, "tile": tile,
                       "sourceRole": role, "sourceLocalTile": local_tile,
                       "palette": value >> 12, "hflip": bool(value & 0x400),
                       "vflip": bool(value & 0x800), "animationSlots": animations})
    return result


def _behavior_inventory(layouts: list[dict], maps: list[dict], usage: dict[str, dict],
                        attributes: dict[str, tuple[tuple[int, int], ...]],
                        behavior_names: dict[int, str]) -> list[dict]:
    rows: dict[int, dict] = {}
    map_types_by_layout = {layout["id"]: Counter(item["mapType"] for item in maps
                                                  if item["layout"] == layout["id"] and item["mapType"])
                           for layout in layouts}
    for layout in layouts:
        for metatile_text, item in usage[layout["id"]].items():
            metatile = int(metatile_text)
            symbol = layout["primary_tileset"] if metatile < 0x200 else layout["secondary_tileset"]
            local_id = metatile if metatile < 0x200 else metatile - 0x200
            if symbol not in attributes or local_id >= len(attributes[symbol]):
                continue
            behavior, layer_type = attributes[symbol][local_id]
            name = behavior_names.get(behavior, f"UNKNOWN_{behavior}")
            row = rows.setdefault(behavior, {"id": behavior, "name": name,
                                  "families": _behavior_families(name), "occurrences": 0,
                                  "tilesets": set(), "layouts": set(), "mapTypes": Counter(),
                                  "collision": Counter(), "elevation": Counter(),
                                  "layerTypes": Counter()})
            row["occurrences"] += item["count"]
            row["tilesets"].add(symbol)
            row["layouts"].add(layout["id"])
            row["layerTypes"][LAYER_TYPES.get(layer_type, f"unknown-{layer_type}")] += item["count"]
            for map_type in map_types_by_layout[layout["id"]]:
                row["mapTypes"][map_type] += item["count"]
            row["collision"].update(item["collision"])
            row["elevation"].update(item["elevation"])
    output = []
    for behavior in sorted(rows):
        row = rows[behavior]
        row["tilesets"] = sorted(row["tilesets"])
        row["layouts"] = sorted(row["layouts"])
        for field in ("mapTypes", "collision", "elevation", "layerTypes"):
            row[field] = dict(sorted(row[field].items()))
        output.append(row)
    return output


def _metatile_catalog(root: Path, tilesets: dict[str, TilesetInfo], layouts: list[dict],
                      usage: dict[str, dict], attributes: dict[str, tuple[tuple[int, int], ...]],
                      behavior_names: dict[int, str], animations: dict) -> list[dict]:
    pair_layouts: dict[tuple[str, str], list[dict]] = defaultdict(list)
    for layout in layouts:
        pair = (layout["primary_tileset"], layout["secondary_tileset"])
        if pair[0] in tilesets and pair[1] in tilesets:
            pair_layouts[pair].append(layout)
    rows = []
    for primary, secondary in sorted(pair_layouts):
        records = []
        for symbol, role in ((primary, "primary"), (secondary, "secondary")):
            info = tilesets[symbol]
            offset = 0x200 if role == "secondary" else 0
            for local_id in range(info.metatile_count):
                global_id = local_id + offset
                count = sum(usage[layout["id"]].get(str(global_id), {}).get("count", 0)
                            for layout in pair_layouts[(primary, secondary)])
                behavior, layer_type = attributes[symbol][local_id]
                records.append({"role": role, "tileset": symbol, "localId": local_id,
                                "globalId": global_id, "behaviorId": behavior,
                                "behavior": behavior_names.get(behavior, f"UNKNOWN_{behavior}"),
                                "layerTypeId": layer_type,
                                "layerType": LAYER_TYPES.get(layer_type, f"unknown-{layer_type}"),
                                "usageCount": count,
                                "images": {layer: f"contact_sheets/{primary}__{symbol}__{layer}.png"
                                           for layer in ("base", "foreground", "full")},
                                "masks": None,
                                "subtiles": _subtiles(root, info, local_id, animations["tileSlots"],
                                                      primary, secondary)})
        rows.append({"id": f"{primary}__{secondary}", "primary": primary,
                     "secondary": secondary,
                     "layouts": [layout["id"] for layout in pair_layouts[(primary, secondary)]],
                     "metatiles": records})
    return rows


def _structure_candidates(layouts: list[dict],
                          cells_by_layout: dict[str, tuple[tuple[int, int, int], ...]]) -> dict:
    hash_counts = Counter()
    valid_layouts = [layout for layout in layouts if layout["primary_tileset"] != "0"
                     and layout["secondary_tileset"] != "0"]
    for layout in valid_layouts:
        ids = [cell[0] for cell in cells_by_layout[layout["id"]]]
        width, height = layout["width"], layout["height"]
        pair = f"{layout['primary_tileset']}__{layout['secondary_tileset']}"
        for candidate_width, candidate_height in PATTERN_SIZES:
            for y in range(height - candidate_height + 1):
                for x in range(width - candidate_width + 1):
                    matrix = tuple(ids[(y + row) * width + x + column]
                                   for row in range(candidate_height)
                                   for column in range(candidate_width))
                    if len(set(matrix)) < 3:
                        continue
                    packed = struct.pack(f"<{len(matrix)}H", *matrix)
                    hash_counts[(pair, candidate_width, candidate_height,
                                 zlib.crc32(packed))] += 1
    repeated_hashes = {key for key, count in hash_counts.items() if count >= 2}
    exact: dict[tuple, list[dict]] = defaultdict(list)
    for layout in valid_layouts:
        cells = cells_by_layout[layout["id"]]
        ids = [cell[0] for cell in cells]
        width, height = layout["width"], layout["height"]
        pair = f"{layout['primary_tileset']}__{layout['secondary_tileset']}"
        for candidate_width, candidate_height in PATTERN_SIZES:
            for y in range(height - candidate_height + 1):
                for x in range(width - candidate_width + 1):
                    matrix = tuple(ids[(y + row) * width + x + column]
                                   for row in range(candidate_height)
                                   for column in range(candidate_width))
                    if len(set(matrix)) < 3:
                        continue
                    digest = zlib.crc32(struct.pack(f"<{len(matrix)}H", *matrix))
                    if (pair, candidate_width, candidate_height, digest) not in repeated_hashes:
                        continue
                    collision = tuple(cells[(y + row) * width + x + column][1]
                                      for row in range(candidate_height)
                                      for column in range(candidate_width))
                    elevation = tuple(cells[(y + row) * width + x + column][2]
                                      for row in range(candidate_height)
                                      for column in range(candidate_width))
                    exact[(pair, candidate_width, candidate_height, matrix)].append(
                        {"layout": layout["id"], "x": x, "y": y,
                         "collision": collision, "elevation": elevation})
    candidates = []
    for key, occurrences in exact.items():
        if len(occurrences) < 2:
            continue
        occurrences.sort(key=lambda item: (item["layout"], item["y"], item["x"]))
        pair, width, height, matrix = key
        signature = hashlib.sha256((pair + ":" + ",".join(map(str, matrix))).encode()).hexdigest()[:16]
        contexts = {(item["collision"], item["elevation"]) for item in occurrences}
        candidates.append({"id": signature, "pair": pair, "width": width, "height": height,
                           "matrix": [list(matrix[row * width:(row + 1) * width]) for row in range(height)],
                           "occurrenceCount": len(occurrences), "contextVariantCount": len(contexts),
                           "occurrences": [{**item, "collision": list(item["collision"]),
                                            "elevation": list(item["elevation"])}
                                           for item in occurrences[:64]],
                           "occurrencesTruncated": max(0, len(occurrences) - 64)})
    candidates.sort(key=lambda item: (-item["occurrenceCount"], -item["width"] * item["height"],
                                      item["pair"], item["id"]))
    total = len(candidates)
    return {"detector": {"sizes": [list(size) for size in PATTERN_SIZES],
                         "minimumDistinctMetatiles": 3, "identity": "logical pair + exact matrix"},
            "candidateCount": total, "publishedCount": min(total, MAX_STRUCTURE_CANDIDATES),
            "candidates": candidates[:MAX_STRUCTURE_CANDIDATES]}


def _ambiguities(layouts: list[dict], usage: dict[str, dict], maps: list[dict],
                 attributes: dict[str, tuple[tuple[int, int], ...]],
                 behavior_names: dict[int, str]) -> list[dict]:
    map_types = {layout["id"]: sorted({item["mapType"] for item in maps
                                       if item["layout"] == layout["id"] and item["mapType"]})
                 for layout in layouts}
    contexts: dict[tuple[str, int], set[tuple]] = defaultdict(set)
    for layout in layouts:
        pair = f"{layout['primary_tileset']}__{layout['secondary_tileset']}"
        for metatile_text, item in usage[layout["id"]].items():
            metatile = int(metatile_text)
            for context in item["contexts"]:
                collision, elevation = context.split(":")
                for map_type in map_types[layout["id"]] or [None]:
                    contexts[(pair, metatile)].add((collision, elevation, map_type))
    rows = []
    for (pair, metatile), values in sorted(contexts.items()):
        if len(values) < 2:
            continue
        rows.append({"pair": pair, "globalId": metatile,
                     "contexts": [{"collision": int(collision), "elevation": int(elevation),
                                   "mapType": map_type}
                                  for collision, elevation, map_type in sorted(values, key=str)]})
    return rows


def _rule_coverage(root: Path, maps: list[dict], layouts: list[dict], usage: dict[str, dict],
                   behavior_rows: list[dict]) -> dict:
    map_by_symbol = {item["symbol"]: item for item in maps}
    layout_by_id = {item["id"]: item for item in layouts}
    behavior_counts = {item["name"]: item["occurrences"] for item in behavior_rows}
    rules = []
    for path in sorted((root / "data/diorama").glob("**/*.json")):
        source = _json(path)
        relative = path.relative_to(root).as_posix()
        for behavior in source.get("behaviors", {}):
            rules.append({"source": relative, "kind": "behavior", "id": behavior,
                          "matches": behavior_counts.get(behavior, 0)})
        tileset = source.get("tileset")
        for metatile in source.get("metatiles", {}):
            try:
                local_id = int(metatile, 0)
            except ValueError:
                local_id = -1
            matches = 0
            for layout in layouts:
                if tileset not in (layout["primary_tileset"], layout["secondary_tileset"]):
                    continue
                global_id = local_id + (0x200 if layout["secondary_tileset"] == tileset else 0)
                matches += usage[layout["id"]].get(str(global_id), {}).get("count", 0)
            rules.append({"source": relative, "kind": "metatile", "id": metatile,
                          "tileset": tileset, "matches": matches})
        map_symbol = source.get("map")
        for index, override in enumerate(source.get("overrides", [])):
            matches = 0
            if map_symbol in map_by_symbol:
                layout = layout_by_id[map_by_symbol[map_symbol]["layout"]]
                if 0 <= override.get("x", -1) < layout["width"] and 0 <= override.get("y", -1) < layout["height"]:
                    matches = 1
            rules.append({"source": relative, "kind": "mapOverride", "id": index,
                          "map": map_symbol, "matches": matches})
    return {"rules": rules, "unusedRules": [rule for rule in rules if rule["matches"] == 0]}


def build_catalog(root: Path) -> dict[str, object]:
    tilesets = load_tilesets(root)
    maps, layouts = load_world(root)
    used_layouts = {item["layout"] for item in maps}
    pairs = Counter((layout["primary_tileset"], layout["secondary_tileset"])
                    for layout in layouts
                    if layout["primary_tileset"] in tilesets
                    and layout["secondary_tileset"] in tilesets)
    for layout in layouts:
        for symbol, role in ((layout["primary_tileset"], "primary"),
                             (layout["secondary_tileset"], "secondary")):
            if symbol not in tilesets:
                if symbol == "0":
                    layout["dynamicTilesets"] = True
                    continue
                raise CatalogError(f"{layout['id']}: unknown {role} tileset {symbol}")
            if tilesets[symbol].role != role:
                raise CatalogError(f"{layout['id']}: {symbol} is not {role}")
        layout["referenced"] = layout["id"] in used_layouts
        layout["blockCount"] = layout["width"] * layout["height"]
    cells_by_layout = {layout["id"]: _layout_cells(root, layout) for layout in layouts}
    usage = {layout["id"]: _layout_usage(root, layout, cells_by_layout[layout["id"]])
             for layout in layouts}
    _, behavior_names = _parse_behaviors(root / "include/constants/metatile_behaviors.h")
    attributes = _attributes(root, tilesets)
    runtime = _runtime_changes(root, maps, layouts)
    behavior_rows = _behavior_inventory(layouts, maps, usage, attributes, behavior_names)
    animations = _animation_inventory(root, tilesets)
    events = _event_inventory(root, maps, layouts, cells_by_layout, attributes, behavior_names)
    structures = _structure_candidates(layouts, cells_by_layout)
    tileset_rows = []
    for symbol, info in tilesets.items():
        row = asdict(info)
        row["layouts"] = [layout["id"] for layout in layouts
                          if symbol in (layout["primary_tileset"], layout["secondary_tileset"])]
        row["pairs"] = [{"primary": primary, "secondary": secondary, "layouts": count}
                        for (primary, secondary), count in sorted(pairs.items())
                        if symbol in (primary, secondary)]
        tileset_rows.append(row)
    return {
        "maps": maps,
        "layouts": layouts,
        "tilesets": tileset_rows,
        "usage": usage,
        "behaviors": behavior_rows,
        "events": events,
        "animations": animations,
        "runtime": runtime,
        "metatiles": _metatile_catalog(root, tilesets, layouts, usage, attributes,
                                         behavior_names, animations),
        "structures": structures,
        "ambiguities": _ambiguities(layouts, usage, maps, attributes, behavior_names),
        "ruleCoverage": _rule_coverage(root, maps, layouts, usage, behavior_rows),
        "summary": {
            "maps": len(maps), "layouts": len(layouts),
            "referencedLayouts": len(used_layouts),
            "unreferencedLayouts": len(layouts) - len(used_layouts),
            "tilesets": len(tilesets),
            "primaryTilesets": sum(item.role == "primary" for item in tilesets.values()),
            "secondaryTilesets": sum(item.role == "secondary" for item in tilesets.values()),
            "blocks": sum(layout["blockCount"] for layout in layouts),
            "signEvents": sum(item["events"]["signs"] for item in maps),
            "usedBehaviors": len(behavior_rows),
            "events": len(events["events"]),
            "decorations": len(events["decorations"]),
            "runtimeAlternateLayouts": sum(item["classification"] == "runtime-alternate"
                                            for item in runtime["layouts"]),
            "structureCandidates": structures["candidateCount"],
        },
    }


def write_catalog(root: Path, output: Path, contact_sheets: bool = False,
                  catalog: dict[str, object] | None = None) -> dict[str, object]:
    catalog = catalog or build_catalog(root)
    output.mkdir(parents=True, exist_ok=True)
    if contact_sheets:
        _write_contact_sheets(root, output / "contact_sheets", catalog)
    for name in ("maps", "layouts", "tilesets"):
        (output / f"{name}.json").write_text(
            json.dumps({"schemaVersion": 1, name: catalog[name]}, indent=2) + "\n", encoding="utf-8")
    (output / "metatile_usage.json").write_text(
        json.dumps({"schemaVersion": 1, "layouts": catalog["usage"]}, separators=(",", ":")) + "\n",
        encoding="utf-8")
    for filename, key in (("metatiles.json", "metatiles"), ("behaviors.json", "behaviors"),
                          ("events.json", "events"), ("animations.json", "animations"),
                          ("structures.json", "structures"), ("ambiguities.json", "ambiguities"),
                          ("runtime_layouts.json", "runtime")):
        (output / filename).write_text(
            json.dumps({"schemaVersion": 1, key: catalog[key]}, separators=(",", ":")) + "\n",
            encoding="utf-8")
    coverage = {"summary": catalog["summary"],
                "resourceCoverage": {
                    "maps": {"cataloged": catalog["summary"]["maps"], "total": 518},
                    "layouts": {"cataloged": catalog["summary"]["layouts"], "total": 441},
                    "tilesets": {"cataloged": catalog["summary"]["tilesets"], "total": 75},
                    "blocks": {"cataloged": catalog["summary"]["blocks"], "total": 324579}},
                **catalog["ruleCoverage"]}
    (output / "coverage.json").write_text(
        json.dumps({"schemaVersion": 1, **coverage}, indent=2) + "\n",
        encoding="utf-8")
    _write_html(output, catalog)
    return catalog


def _write_contact_sheets(root: Path, output: Path, catalog: dict[str, object]) -> None:
    by_symbol = {item["symbol"]: item for item in catalog["tilesets"]}
    pairs = sorted({(layout["primary_tileset"], layout["secondary_tileset"])
                    for layout in catalog["layouts"]
                    if layout["primary_tileset"] in by_symbol
                    and layout["secondary_tileset"] in by_symbol})
    if output.exists():
        shutil.rmtree(output)
    output.mkdir(parents=True)
    diagnostics = []
    written_primary = set()
    primary_masks: dict[str, dict[int, dict]] = {}
    slots_by_callback = load_animation_slots(root)
    metadata_by_pair = {(pair["primary"], pair["secondary"]):
                        {item["globalId"]: item for item in pair["metatiles"]}
                        for pair in catalog["metatiles"]}
    for primary, secondary in pairs:
        composer = TilesetComposer(
            root / by_symbol[primary]["root"], root / by_symbol[secondary]["root"],
            root / by_symbol[primary]["metatiles_root"],
            root / by_symbol[secondary]["metatiles_root"],
            slots_by_callback.get(by_symbol[primary]["callback"], ()),
            slots_by_callback.get(by_symbol[secondary]["callback"], ()))
        for symbol, is_secondary in ((primary, False), (secondary, True)):
            if not is_secondary and symbol in written_primary:
                for global_id, masks in primary_masks[symbol].items():
                    metadata_by_pair[(primary, secondary)][global_id]["masks"] = masks
                continue
            count = composer.metatile_count(is_secondary)
            columns = 16
            width = columns * METATILE_SIZE
            height = ((count + columns - 1) // columns) * METATILE_SIZE
            layer_pixels = [bytearray(width * height * 4) for _ in range(3)]
            for local_id in range(count):
                global_id = local_id + (0x200 if is_secondary else 0)
                layers = composer.compose_metatile(global_id)
                metadata_by_pair[(primary, secondary)][global_id]["masks"] = {
                    layer_name: [f"{sum((1 << x) for x in range(METATILE_SIZE)
                                         if layer[y * METATILE_SIZE + x].visible):04x}"
                                 for y in range(METATILE_SIZE)]
                    for layer_name, layer in zip(("base", "foreground", "full"), layers)
                }
                origin_x = (local_id % columns) * METATILE_SIZE
                origin_y = (local_id // columns) * METATILE_SIZE
                for layer_index, layer in enumerate(layers):
                    origin_x = (local_id % columns) * METATILE_SIZE
                    origin_y = (local_id // columns) * METATILE_SIZE
                    for y in range(METATILE_SIZE):
                        for x in range(METATILE_SIZE):
                            target = ((origin_y + y) * width + origin_x + x) * 4
                            layer_pixels[layer_index][target:target + 4] = bytes(
                                layer[y * METATILE_SIZE + x].rgba)
            for layer_index, layer_name in enumerate(("base", "foreground", "full")):
                name = f"{primary}__{symbol}__{layer_name}.png"
                (output / name).write_bytes(encode_rgba_png(width, height,
                                                            bytes(layer_pixels[layer_index])))
            if not is_secondary:
                written_primary.add(symbol)
                primary_masks[symbol] = {
                    global_id: metadata_by_pair[(primary, secondary)][global_id]["masks"]
                    for global_id in range(count)
                }
        usage_by_metatile = {item["globalId"]: item["usageCount"]
                             for pair_row in catalog["metatiles"]
                             if pair_row["primary"] == primary and pair_row["secondary"] == secondary
                             for item in pair_row["metatiles"]}
        missing = []
        for path, tile, metatile, status in sorted(composer.missing_tile_details):
            symbol = primary if Path(path).as_posix() == (root / by_symbol[primary]["root"]).as_posix() else secondary
            declared = sorted({slot["source"] for slot in catalog["animations"]["tileSlots"]
                                if symbol in slot["tilesets"]
                                and slot["localStart"] <= tile < slot["localStart"] + slot["tileCount"]})
            missing.append({"tileset": symbol, "tile": tile, "metatile": metatile,
                            "metatileUsageCount": usage_by_metatile.get(metatile, 0),
                            "declaredAnimationSlots": declared,
                            "status": status})
        diagnostics.append({"primary": primary, "secondary": secondary,
                            "missingStaticTiles": missing,
                            "unresolvedCount": sum(item["status"] in ("unresolved", "static-padding") for item in missing),
                            "unresolvedUsedCount": sum(item["status"] in ("unresolved", "static-padding")
                                                       and item["metatileUsageCount"] > 0 for item in missing)})
    (output / "diagnostics.json").write_text(json.dumps(diagnostics, indent=2) + "\n", encoding="utf-8")


def _write_html(output: Path, catalog: dict[str, object]) -> None:
    options = "".join(f'<option value="{html.escape(pair["id"])}">{html.escape(pair["id"])}</option>'
                      for pair in catalog["metatiles"])
    document = f"""<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width">
<title>Emerald Diorama Catalog</title><style>
body{{font:14px system-ui;background:#101916;color:#e7f0e9;margin:0}}header{{position:sticky;top:0;background:#183027;padding:16px;z-index:2}}
main{{padding:16px}}select,input{{background:#0b120f;color:inherit;border:1px solid #52705f;padding:8px}}.grid{{display:grid;grid-template-columns:repeat(auto-fill,minmax(190px,1fr));gap:10px;margin-top:16px}}
.card{{background:#17211d;border:1px solid #30493c;padding:10px}}.tile{{width:64px;height:64px;image-rendering:pixelated;background:#597063;background-size:1024px auto}}
.meta{{color:#abc0b2;line-height:1.5}}a{{color:#9ed8b5}}</style></head><body>
<header><strong>Emerald Diorama global catalog</strong> <select id="pair">{options}</select>
<input id="query" placeholder="ID or behavior"><span id="count"></span></header>
<main><p>Inspect local/global IDs, layer type, behavior, usage, palette, flips, tile ownership and animation slots. Raw reports: <a href="events.json">events</a>, <a href="behaviors.json">behaviors</a>, <a href="structures.json">structures</a>, <a href="ambiguities.json">ambiguities</a>, <a href="runtime_layouts.json">runtime layouts</a>, <a href="coverage.json">coverage</a>.</p><div id="grid" class="grid"></div></main>
<script>
let data;const pair=document.querySelector('#pair'),query=document.querySelector('#query'),grid=document.querySelector('#grid'),count=document.querySelector('#count');
fetch('metatiles.json').then(r=>r.json()).then(j=>{{data=j.metatiles;render()}});pair.onchange=query.oninput=render;
function render(){{if(!data)return;const p=data.find(x=>x.id===pair.value)||data[0],q=query.value.toLowerCase();grid.textContent='';let shown=0;
for(const m of p.metatiles){{const text=`${{m.localId}} ${{m.globalId}} ${{m.behavior}} ${{m.layerType}}`;if(q&&!text.toLowerCase().includes(q))continue;
const card=document.createElement('div');card.className='card';const image=`contact_sheets/${{p.primary}}__${{m.tileset}}__full.png`;
const x=(m.localId%16)*16,y=Math.floor(m.localId/16)*16;card.innerHTML=`<div class="tile" style="background-image:url('${{image}}');background-size:auto;background-position:-${{x*4}}px -${{y*4}}px"></div><b>${{m.role}} local ${{m.localId}} / global ${{m.globalId}}</b><div class="meta">${{m.behavior}}<br>${{m.layerType}} · uses ${{m.usageCount}}<br>${{m.subtiles.map(s=>`${{s.sourceRole[0]}}:${{s.sourceLocalTile}} p${{s.palette}}${{s.hflip?' H':''}}${{s.vflip?' V':''}}${{s.animationSlots.length?' anim':''}}`).join(' · ')}}</div>`;grid.append(card);shown++;}}count.textContent=` ${{shown}} metatiles`;}}
</script></body></html>"""
    (output / "index.html").write_text(document, encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--output", type=Path)
    parser.add_argument("--contact-sheets", action="store_true")
    args = parser.parse_args()
    output = args.output or args.root / "build/diorama_catalog"
    try:
        catalog = write_catalog(args.root, output, args.contact_sheets)
    except (CatalogError, OSError) as error:
        parser.error(str(error))
    summary = catalog["summary"]
    print(f"Cataloged {summary['maps']} maps, {summary['layouts']} layouts, "
          f"{summary['tilesets']} tilesets and {summary['blocks']} blocks")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
