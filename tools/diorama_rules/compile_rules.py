#!/usr/bin/env python3
"""Validate diorama JSON and compile it into immutable C tables."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import re
import struct
import sys
from dataclasses import dataclass
from pathlib import Path


SHAPES = {
    "flat": "DIORAMA_SHAPE_FLAT",
    "extruded": "DIORAMA_SHAPE_EXTRUDED",
    "ledge": "DIORAMA_SHAPE_LEDGE",
    "water": "DIORAMA_SHAPE_WATER",
    "cutout": "DIORAMA_SHAPE_CUTOUT",
    "roof": "DIORAMA_SHAPE_ROOF",
    "building-part": "DIORAMA_SHAPE_BUILDING_PART",
    "hidden": "DIORAMA_SHAPE_HIDDEN",
}

PROFILES = {
    None: "DIORAMA_ROOF_NONE",
    "none": "DIORAMA_ROOF_NONE",
    "flat": "DIORAMA_ROOF_FLAT",
    "gable-x": "DIORAMA_ROOF_GABLE_X",
    "gable-z": "DIORAMA_ROOF_GABLE_Z",
}

MATERIAL_LAYERS = {
    "full": "DIORAMA_MATERIAL_FULL",
    "base": "DIORAMA_MATERIAL_BASE",
    "foreground": "DIORAMA_MATERIAL_FOREGROUND",
    "none": "DIORAMA_MATERIAL_NONE",
}

MATERIAL_FACES = ("top", "north", "east", "south", "west", "plane")

PLANE_AXES = {
    None: "DIORAMA_PLANE_AXIS_X",
    "x": "DIORAMA_PLANE_AXIS_X",
    "z": "DIORAMA_PLANE_AXIS_Z",
    "cross": "DIORAMA_PLANE_AXIS_CROSS",
}

TILESETS = {
    "gTileset_General": ("DIORAMA_TILESET_GENERAL", "primary/general"),
    "gTileset_Petalburg": ("DIORAMA_TILESET_PETALBURG", "secondary/petalburg"),
    "gTileset_Building": ("DIORAMA_TILESET_BUILDING", "primary/building"),
    "gTileset_BrendansMaysHouse": ("DIORAMA_TILESET_BRENDANS_MAYS_HOUSE", "secondary/brendans_mays_house"),
    "gTileset_Lab": ("DIORAMA_TILESET_LAB", "secondary/lab"),
}

CAMERA_PROFILES = {
    "exterior": ("DIORAMA_CAMERA_EXTERIOR", 40.542, 130.0),
    "interior": ("DIORAMA_CAMERA_INTERIOR", 60.0, 145.0),
}


class RuleError(ValueError):
    pass


@dataclass(frozen=True)
class MapInfo:
    symbol: str
    group: int
    number: int
    layout_symbol: str
    layout_id: int
    width: int
    height: int
    sign_coordinates: tuple[tuple[int, int], ...]


def load_json(path: Path) -> dict:
    def reject_duplicates(pairs):
        result = {}
        for key, value in pairs:
            if key in result:
                raise RuleError(f"{path}: duplicate key {key!r}")
            result[key] = value
        return result

    try:
        value = json.loads(path.read_text(encoding="utf-8"), object_pairs_hook=reject_duplicates)
    except (OSError, json.JSONDecodeError) as error:
        raise RuleError(f"{path}: {error}") from error
    if not isinstance(value, dict):
        raise RuleError(f"{path}: root must be an object")
    if value.get("version") != 1:
        raise RuleError(f"{path}: unsupported or missing version")
    return value


def parse_behaviors(path: Path) -> dict[str, int]:
    text = re.sub(r"//[^\n]*|/\*.*?\*/", "", path.read_text(encoding="utf-8"), flags=re.S)
    body_match = re.search(r"enum\s*\{(.*?)\};", text, flags=re.S)
    if body_match is None:
        raise RuleError(f"{path}: behavior enum not found")
    result: dict[str, int] = {}
    value = -1
    for entry in body_match.group(1).split(","):
        match = re.search(r"\b(MB_[A-Z0-9_]+)\b(?:\s*=\s*(0x[0-9A-Fa-f]+|[0-9]+))?", entry)
        if match is None:
            continue
        value = int(match.group(2), 0) if match.group(2) else value + 1
        result[match.group(1)] = value
    if not result:
        raise RuleError(f"{path}: no behavior symbols found")
    return result


def load_catalog(root: Path) -> tuple[dict[str, MapInfo], dict[str, tuple[int, dict]]]:
    layouts_data = json.loads((root / "data/layouts/layouts.json").read_text(encoding="utf-8"))
    layouts = {
        item["id"]: (index, item)
        for index, item in enumerate(layouts_data["layouts"], start=1)
    }
    groups = json.loads((root / "data/maps/map_groups.json").read_text(encoding="utf-8"))
    maps: dict[str, MapInfo] = {}
    for group_id, group_name in enumerate(groups["group_order"]):
        for map_number, directory in enumerate(groups[group_name]):
            map_path = root / "data/maps" / directory / "map.json"
            map_data = json.loads(map_path.read_text(encoding="utf-8"))
            layout_symbol = map_data["layout"]
            if layout_symbol not in layouts:
                raise RuleError(f"{map_path}: unknown layout {layout_symbol}")
            layout_id, layout = layouts[layout_symbol]
            maps[map_data["id"]] = MapInfo(
                map_data["id"], group_id, map_number, layout_symbol, layout_id,
                layout["width"], layout["height"],
                tuple((event["x"], event["y"])
                      for event in map_data.get("bg_events", [])
                      if event.get("type") == "sign"))
    return maps, layouts


def number(value: object, path: str, minimum: float = -1.0, maximum: float = 8.0) -> float:
    if not isinstance(value, (int, float)) or isinstance(value, bool):
        raise RuleError(f"{path}: expected a number")
    result = float(value)
    if not math.isfinite(result) or result < minimum or result > maximum:
        raise RuleError(f"{path}: value must be between {minimum} and {maximum}")
    return result


def array(value: object, path: str) -> list:
    if not isinstance(value, list):
        raise RuleError(f"{path}: must be an array")
    return value


def default_materials() -> tuple[tuple[int, str], ...]:
    return tuple((0xFFFF, "DIORAMA_MATERIAL_FOREGROUND" if face == "plane"
                  else "DIORAMA_MATERIAL_FULL") for face in MATERIAL_FACES)


def parse_material(value: object, path: str) -> tuple[int, str]:
    if value == "none":
        return 0xFFFF, "DIORAMA_MATERIAL_NONE"
    if not isinstance(value, dict):
        raise RuleError(f"{path}: material must be an object or 'none'")
    unknown = set(value) - {"metatile", "layer"}
    if unknown:
        raise RuleError(f"{path}: unknown fields: {', '.join(sorted(unknown))}")
    source = value.get("metatile", "self")
    if source == "self":
        metatile = 0xFFFF
    else:
        try:
            metatile = int(source, 0) if isinstance(source, str) else source
        except ValueError as error:
            raise RuleError(f"{path}: invalid metatile {source!r}") from error
        if not isinstance(metatile, int) or isinstance(metatile, bool) or not 0 <= metatile < 1024:
            raise RuleError(f"{path}: metatile must be 'self' or an ID from 0 through 1023")
    layer = value.get("layer", "full")
    if layer not in MATERIAL_LAYERS:
        raise RuleError(f"{path}: unknown material layer {layer!r}")
    return metatile, MATERIAL_LAYERS[layer]


def parse_materials(value: object, path: str) -> tuple[tuple[int, str], ...]:
    materials = list(default_materials())
    if value is None:
        return tuple(materials)
    if not isinstance(value, dict):
        raise RuleError(f"{path}: faces must be an object")
    unknown = set(value) - set(MATERIAL_FACES)
    if unknown:
        raise RuleError(f"{path}: unknown faces: {', '.join(sorted(unknown))}")
    for face, material in value.items():
        materials[MATERIAL_FACES.index(face)] = parse_material(material, f"{path}.{face}")
    return tuple(materials)


def parse_definition(value: object, path: str) -> tuple:
    if not isinstance(value, dict):
        raise RuleError(f"{path}: rule must be an object")
    unknown = set(value) - {"shape", "profile", "axis", "baseMetatile", "groundHeight", "height", "faces"}
    if unknown:
        raise RuleError(f"{path}: unknown fields: {', '.join(sorted(unknown))}")
    shape = value.get("shape")
    if shape not in SHAPES:
        raise RuleError(f"{path}: unknown shape {shape!r}")
    profile = value.get("profile")
    if profile not in PROFILES:
        raise RuleError(f"{path}: unknown profile {profile!r}")
    axis = value.get("axis")
    if axis not in PLANE_AXES:
        raise RuleError(f"{path}: unknown plane axis {axis!r}")
    base_metatile = value.get("baseMetatile", 0xFFFF)
    if not isinstance(base_metatile, int) or isinstance(base_metatile, bool) \
       or base_metatile < 0 or (base_metatile >= 1024 and base_metatile != 0xFFFF):
        raise RuleError(f"{path}: baseMetatile must be a metatile ID from 0 through 1023")
    if shape == "cutout" and base_metatile == 0xFFFF:
        raise RuleError(f"{path}: cutout requires baseMetatile")
    ground = number(value.get("groundHeight", 0.0), path + ".groundHeight")
    height = number(value.get("height", 0.0), path + ".height", 0.0, 8.0)
    if shape in ("extruded", "cutout") and height <= 0.0:
        raise RuleError(f"{path}: {shape} requires a positive height")
    return (SHAPES[shape], PROFILES[profile], PLANE_AXES[axis], base_metatile,
            ground, height, parse_materials(value.get("faces"), path + ".faces"))


def fmt_float(value: float) -> str:
    return f"{value:.6f}f"


def parse_camera(value: object, path: str) -> tuple[str, float, float]:
    if value is None:
        value = {"profile": "exterior"}
    if not isinstance(value, dict):
        raise RuleError(f"{path}: camera must be an object")
    unknown = set(value) - {"profile", "pitch", "focalLength"}
    if unknown:
        raise RuleError(f"{path}: unknown fields: {', '.join(sorted(unknown))}")
    name = value.get("profile")
    if name not in CAMERA_PROFILES:
        raise RuleError(f"{path}: unknown camera profile {name!r}")
    profile, default_pitch, default_focal = CAMERA_PROFILES[name]
    pitch_degrees = number(value.get("pitch", default_pitch), path + ".pitch", 20.0, 70.0)
    focal_length = number(value.get("focalLength", default_focal), path + ".focalLength", 80.0, 200.0)
    return profile, math.radians(pitch_degrees), focal_length


def fmt_rule(rule: tuple) -> str:
    shape, profile, axis, base_metatile, ground, height, materials = rule
    return (f"{{ {shape}, {profile}, {axis}, {base_metatile}, "
            f"{fmt_float(ground)}, {fmt_float(height)}, {fmt_materials(materials)} }}")


def fmt_materials(materials: tuple[tuple[int, str], ...]) -> str:
    return "{ " + ", ".join(f"{{ {metatile}, {layer} }}" for metatile, layer in materials) + " }"


def validate_tileset_count(root: Path, symbol: str, role: str) -> int:
    if symbol not in TILESETS:
        raise RuleError(f"unknown tileset {symbol}")
    expected_role, relative = TILESETS[symbol][1].split("/", 1)
    if role != expected_role:
        raise RuleError(f"{symbol}: role must be {expected_role}")
    attributes = root / "data/tilesets" / expected_role / relative / "metatile_attributes.bin"
    size = attributes.stat().st_size
    if size % 2:
        raise RuleError(f"{attributes}: invalid attribute table size")
    return size // 2


def infer_base_metatile(root: Path, symbol: str, role: str, metatile_id: int, path: str) -> int:
    relative = TILESETS[symbol][1].split("/", 1)[1]
    metatiles_path = root / "data/tilesets" / role / relative / "metatiles.bin"
    raw = metatiles_path.read_bytes()
    entries = [struct.unpack_from("<8H", raw, offset)
               for offset in range(0, len(raw), 16)]
    source = entries[metatile_id]
    candidates = [index for index, candidate in enumerate(entries)
                  if candidate[:4] == source[:4]
                  and all(entry == 0 for entry in candidate[4:])]
    if len(candidates) != 1:
        raise RuleError(f"{path}: cannot infer one unambiguous base metatile")
    return candidates[0] + (512 if role == "secondary" else 0)


def compile_data(root: Path) -> dict:
    rules_root = root / "data/diorama"
    defaults = load_json(rules_root / "defaults.json")
    maps, layouts = load_catalog(root)
    behaviors = parse_behaviors(root / "include/constants/metatile_behaviors.h")
    default_rule = parse_definition(defaults.get("default", {}), "defaults.default")

    behavior_rules = []
    for symbol, value in sorted(defaults.get("behaviors", {}).items()):
        if symbol not in behaviors:
            raise RuleError(f"defaults.behaviors: unknown behavior {symbol}")
        behavior_rules.append((behaviors[symbol], parse_definition(value, f"defaults.behaviors.{symbol}")))

    tileset_rules = []
    configured_tilesets = set()
    for path in sorted((rules_root / "tilesets").glob("*.json")):
        data = load_json(path)
        symbol = data.get("tileset")
        role = data.get("role")
        count = validate_tileset_count(root, symbol, role)
        if symbol in configured_tilesets:
            raise RuleError(f"{path}: duplicate tileset {symbol}")
        configured_tilesets.add(symbol)
        tileset_enum = TILESETS[symbol][0]
        seen = set()
        for raw_id, value in data.get("metatiles", {}).items():
            try:
                metatile_id = int(raw_id, 0)
            except (TypeError, ValueError) as error:
                raise RuleError(f"{path}: invalid metatile id {raw_id!r}") from error
            if metatile_id in seen:
                raise RuleError(f"{path}: duplicate metatile id {raw_id}")
            if metatile_id < 0 or metatile_id >= count:
                raise RuleError(f"{path}: metatile {raw_id} outside 0..{count - 1}")
            seen.add(metatile_id)
            if not isinstance(value, dict):
                raise RuleError(f"{path}:{raw_id}: rule must be an object")
            value = dict(value)
            infer_base = value.pop("inferBase", False)
            if not isinstance(infer_base, bool):
                raise RuleError(f"{path}:{raw_id}.inferBase: expected a boolean")
            if infer_base:
                if "baseMetatile" in value:
                    raise RuleError(f"{path}:{raw_id}: inferBase and baseMetatile are mutually exclusive")
                value["baseMetatile"] = infer_base_metatile(
                    root, symbol, role, metatile_id, f"{path}:{raw_id}")
            tileset_rules.append((tileset_enum, metatile_id,
                                  parse_definition(value, f"{path}:{raw_id}")))
    tileset_rules.sort(key=lambda item: (item[0], item[1]))

    templates = {}
    for path in sorted((rules_root / "buildings").glob("*.json")):
        data = load_json(path)
        for name, value in data.get("templates", {}).items():
            if name in templates:
                raise RuleError(f"{path}: duplicate building template {name}")
            if not isinstance(value, dict):
                raise RuleError(f"{path}: template {name} must be an object")
            unknown = set(value) - {"width", "height", "roofRows", "bodyHeight", "roofHeight", "profile", "faces"}
            if unknown:
                raise RuleError(f"{path}: template {name} has unknown fields: {', '.join(sorted(unknown))}")
            width = value.get("width")
            height = value.get("height")
            roof_rows = value.get("roofRows")
            if not all(isinstance(item, int) and not isinstance(item, bool)
                       for item in (width, height, roof_rows)):
                raise RuleError(f"{path}: template {name} dimensions must be integers")
            if width < 1 or width > 32 or height < 1 or height > 32 or roof_rows < 0 or roof_rows > height:
                raise RuleError(f"{path}: template {name} dimensions are out of range")
            profile = value.get("profile")
            if profile not in PROFILES or profile in (None, "none"):
                raise RuleError(f"{path}: template {name} requires a roof profile")
            templates[name] = (width, height, roof_rows, PROFILES[profile],
                               number(value.get("bodyHeight"), f"{path}:{name}.bodyHeight", 0.0),
                               number(value.get("roofHeight"), f"{path}:{name}.roofHeight", 0.0),
                               parse_materials(value.get("faces"), f"{path}:{name}.faces"))
    template_ids = {name: index for index, name in enumerate(sorted(templates), start=1)}

    map_rules = []
    map_overrides = []
    placements = []
    used_layouts = set()
    seen_maps = set()
    for path in sorted((rules_root / "maps").glob("*.json")):
        data = load_json(path)
        unknown = set(data) - {"version", "map", "layout", "supported", "camera",
                               "buildings", "eventRules", "overrides"}
        if unknown:
            raise RuleError(f"{path}: unknown fields: {', '.join(sorted(unknown))}")
        symbol = data.get("map")
        if symbol not in maps:
            raise RuleError(f"{path}: unknown map {symbol}")
        info = maps[symbol]
        if data.get("layout") != info.layout_symbol:
            raise RuleError(f"{path}: layout does not match {info.layout_symbol}")
        if symbol in seen_maps:
            raise RuleError(f"{path}: duplicate map rules for {symbol}")
        seen_maps.add(symbol)
        if data.get("supported") is not True:
            raise RuleError(f"{path}: map rule files must explicitly set supported to true")
        camera = parse_camera(data.get("camera"), f"{path}:camera")
        map_rules.append((info.group, info.number, info.layout_id, camera))
        used_layouts.add(info.layout_symbol)
        overrides = array(data.get("overrides", []), f"{path}: overrides")
        coordinates = set()
        for index, override in enumerate(overrides):
            if not isinstance(override, dict):
                raise RuleError(f"{path}: override {index} must be an object")
            x, y = override.get("x"), override.get("y")
            if not isinstance(x, int) or not isinstance(y, int) or isinstance(x, bool) or isinstance(y, bool):
                raise RuleError(f"{path}: override {index} coordinates must be integers")
            if x < 0 or y < 0 or x >= info.width or y >= info.height:
                raise RuleError(f"{path}: override ({x}, {y}) is outside the map")
            if (x, y) in coordinates:
                raise RuleError(f"{path}: duplicate override ({x}, {y})")
            coordinates.add((x, y))
            definition = {key: value for key, value in override.items() if key not in ("x", "y")}
            map_overrides.append((info.group, info.number, x, y,
                                  parse_definition(definition, f"{path}:override {index}")))
        event_rules = data.get("eventRules", {})
        if not isinstance(event_rules, dict) or set(event_rules) - {"sign"}:
            raise RuleError(f"{path}: eventRules may only define sign")
        if "sign" in event_rules:
            sign_rule = parse_definition(event_rules["sign"], f"{path}:eventRules.sign")
            for x, y in info.sign_coordinates:
                if (x, y) in coordinates:
                    raise RuleError(f"{path}: generated sign overlaps override ({x}, {y})")
                coordinates.add((x, y))
                map_overrides.append((info.group, info.number, x, y, sign_rule))
        buildings = array(data.get("buildings", []), f"{path}: buildings")
        occupied = set()
        for index, placement in enumerate(buildings):
            if not isinstance(placement, dict) or set(placement) != {"template", "x", "y"}:
                raise RuleError(f"{path}: building {index} must contain template, x and y")
            template_name = placement["template"]
            if template_name not in templates:
                raise RuleError(f"{path}: unknown building template {template_name}")
            x, y = placement["x"], placement["y"]
            if not isinstance(x, int) or not isinstance(y, int):
                raise RuleError(f"{path}: building {index} coordinates must be integers")
            width, height = templates[template_name][0:2]
            if x < 0 or y < 0 or x + width > info.width or y + height > info.height:
                raise RuleError(f"{path}: building {index} is outside the map")
            cells = {(cell_x, cell_y) for cell_y in range(y, y + height)
                     for cell_x in range(x, x + width)}
            if occupied & cells:
                raise RuleError(f"{path}: building {index} overlaps another building")
            occupied |= cells
            placements.append((info.group, info.number, template_ids[template_name], x, y))

    layout_rules = []
    for symbol in sorted(used_layouts, key=lambda item: layouts[item][0]):
        layout_id, layout = layouts[symbol]
        primary = layout["primary_tileset"]
        secondary = layout["secondary_tileset"]
        if primary not in TILESETS or secondary not in TILESETS:
            raise RuleError(f"{symbol}: tilesets are not configured for diorama rules")
        layout_rules.append((layout_id, TILESETS[primary][0], TILESETS[secondary][0]))

    source_hash = hashlib.sha256()
    for path in sorted(rules_root.rglob("*.json")):
        source_hash.update(path.relative_to(root).as_posix().encode("ascii"))
        source_hash.update(path.read_bytes())
    generation = int.from_bytes(source_hash.digest()[:4], "little") or 1
    return {
        "generation": generation,
        "default_rule": default_rule,
        "layout_rules": layout_rules,
        "map_rules": sorted(map_rules),
        "behavior_rules": sorted(behavior_rules),
        "tileset_rules": tileset_rules,
        "map_overrides": sorted(map_overrides, key=lambda item: item[:4]),
        "templates": [(template_ids[name], *templates[name]) for name in sorted(templates)],
        "placements": sorted(placements),
    }


def render_header() -> str:
    return """/* Auto-generated by tools/diorama_rules/compile_rules.py. Do not edit. */
#ifndef GUARD_DIORAMA_RULES_GENERATED_H
#define GUARD_DIORAMA_RULES_GENERATED_H

#include <stddef.h>
#include <stdint.h>
#include "diorama/rules.h"

extern const uint32_t gDioramaRulesGeneration;
extern const struct DioramaRuleDefinition gDioramaDefaultRule;
extern const struct DioramaGeneratedLayoutRule gDioramaLayoutRules[];
extern const size_t gDioramaLayoutRuleCount;
extern const struct DioramaGeneratedMapRule gDioramaMapRules[];
extern const size_t gDioramaMapRuleCount;
extern const struct DioramaGeneratedBehaviorRule gDioramaBehaviorRules[];
extern const size_t gDioramaBehaviorRuleCount;
extern const struct DioramaGeneratedTilesetRule gDioramaTilesetRules[];
extern const size_t gDioramaTilesetRuleCount;
extern const struct DioramaGeneratedMapOverride gDioramaMapOverrides[];
extern const size_t gDioramaMapOverrideCount;
extern const struct DioramaGeneratedBuildingTemplate gDioramaBuildingTemplates[];
extern const size_t gDioramaBuildingTemplateCount;
extern const struct DioramaGeneratedBuildingPlacement gDioramaBuildingPlacements[];
extern const size_t gDioramaBuildingPlacementCount;

#endif
"""


def render_c(data: dict) -> str:
    lines = [
        "/* Auto-generated by tools/diorama_rules/compile_rules.py. Do not edit. */",
        "#ifdef ENABLE_DIORAMA",
        '#include "diorama/rules.generated.h"',
        "",
        f"const uint32_t gDioramaRulesGeneration = UINT32_C(0x{data['generation']:08X});",
        f"const struct DioramaRuleDefinition gDioramaDefaultRule = {fmt_rule(data['default_rule'])};",
        "",
    ]

    def table(declaration: str, name: str, rows: list[str]) -> None:
        lines.append(f"const {declaration} {name}[] = {{")
        lines.extend(f"    {row}," for row in rows)
        lines.append("};")
        lines.append(f"const size_t {name[:-1]}Count = sizeof({name}) / sizeof({name}[0]);")
        lines.append("")

    table("struct DioramaGeneratedLayoutRule", "gDioramaLayoutRules",
          [f"{{ {layout}, {primary}, {secondary} }}" for layout, primary, secondary in data["layout_rules"]])
    table("struct DioramaGeneratedMapRule", "gDioramaMapRules",
          [f"{{ {group}, {number}, {layout}, {{ {profile}, {fmt_float(pitch)}, {fmt_float(focal)} }} }}"
           for group, number, layout, (profile, pitch, focal) in data["map_rules"]])
    table("struct DioramaGeneratedBehaviorRule", "gDioramaBehaviorRules",
          [f"{{ {behavior}, {fmt_rule(rule)} }}" for behavior, rule in data["behavior_rules"]])
    table("struct DioramaGeneratedTilesetRule", "gDioramaTilesetRules",
          [f"{{ {tileset}, {metatile}, {fmt_rule(rule)} }}"
           for tileset, metatile, rule in data["tileset_rules"]])
    table("struct DioramaGeneratedMapOverride", "gDioramaMapOverrides",
          [f"{{ {group}, {number}, {x}, {y}, {fmt_rule(rule)} }}"
           for group, number, x, y, rule in data["map_overrides"]])
    table("struct DioramaGeneratedBuildingTemplate", "gDioramaBuildingTemplates",
          [f"{{ {template_id}, {width}, {height}, {roof_rows}, {profile}, {fmt_float(body)}, {fmt_float(roof)}, {fmt_materials(materials)} }}"
           for template_id, width, height, roof_rows, profile, body, roof, materials in data["templates"]])
    table("struct DioramaGeneratedBuildingPlacement", "gDioramaBuildingPlacements",
          [f"{{ {group}, {number}, {template_id}, {x}, {y} }}"
           for group, number, template_id, x, y in data["placements"]])
    lines.append("#endif")
    lines.append("")
    return "\n".join(lines)


def write_or_check(path: Path, content: str, check: bool) -> None:
    if check:
        if not path.exists() or path.read_text(encoding="utf-8") != content:
            raise RuleError(f"{path}: generated file is stale")
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--output-c", type=Path, default=Path("src/data/diorama/diorama_rules.generated.c"))
    parser.add_argument("--output-h", type=Path, default=Path("include/diorama/rules.generated.h"))
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    root = args.root.resolve()
    try:
        data = compile_data(root)
        output_c = args.output_c if args.output_c.is_absolute() else root / args.output_c
        output_h = args.output_h if args.output_h.is_absolute() else root / args.output_h
        write_or_check(output_c, render_c(data), args.check)
        write_or_check(output_h, render_header(), args.check)
    except (RuleError, OSError, KeyError, struct.error) as error:
        print(f"diorama rules: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
