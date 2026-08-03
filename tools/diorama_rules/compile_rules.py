#!/usr/bin/env python3
"""Validate schema-v2 diorama rules and compile a deterministic normalized IR."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import re
import struct
import sys
from collections import Counter, defaultdict
from pathlib import Path

from catalog import CatalogError, TilesetInfo, load_tilesets, load_world
from emerald_compositor import TilesetComposer, load_animation_slots
from profiles import NOMINAL_HEIGHTS, TileShape, shape_from_action
from pixel_objects import UPRIGHT_CLASSES as UPRIGHT_PIXEL_CLASSES, extract_layout
from structures import analyze_layout
from tile_shape import ClassificationError, TileShapeClassifier


SCHEMA_VERSION = 2
ARCHETYPES = (
    "ground", "void", "water", "shallow-water", "waterfall", "current", "hot-spring",
    "ledge", "cliff", "mound", "wall-volume", "bridge", "deck", "rail", "support",
    "stairs-n", "stairs-s", "stairs-e", "stairs-w", "stairs-down-n", "stairs-down-s",
    "stairs-down-e", "stairs-down-w", "roof", "top-slab", "awning", "building",
    "claim-only", "billboard", "cutout", "console", "signpost", "post", "counter",
    "table", "desk", "bed", "bookcase", "relief", "round-hull", "grouped-hull",
    "stump", "tree", "forest-wall", "shrub", "hedge", "rock", "boulder", "grass",
    "flower", "animated-cutout",
)
TERRAIN_CLASSES = ("ground", "path", "sand", "ash", "rock", "pavement",
                   "wood", "carpet", "void", "water")
CLASS_NAMES = tuple(NOMINAL_HEIGHTS)
ART_MODE_NAMES = ("flat", "top", "upright", "grass", "flower", "stair", "cutout")
TERRAIN_SHAPES = ("flat", "hidden", "water", "ledge", "cliff", "stairs",
                  "bridge", "cutout", "extruded", "roof", "building-part")
LAYERS = ("normal", "covered", "split")
MATERIAL_LAYERS = ("base", "foreground", "full")
MATERIAL_FACES = ("top", "north", "east", "south", "west", "plane")
MAP_TYPES = ("MAP_TYPE_NONE", "MAP_TYPE_TOWN", "MAP_TYPE_CITY", "MAP_TYPE_ROUTE",
             "MAP_TYPE_UNDERGROUND", "MAP_TYPE_INDOOR", "MAP_TYPE_SECRET_BASE",
             "MAP_TYPE_UNDERWATER", "MAP_TYPE_OCEAN_ROUTE")
EVENT_KINDS = ("object", "warp", "coordinate", "background")
DIRECTIONS = ("n", "ne", "e", "se", "s", "sw", "w", "nw")
CAMERAS = {"exterior": (40.542, 130.0), "interior": (60.0, 145.0)}
IDENTIFIER = re.compile(r"^[a-z][a-z0-9-]*$")
_PIXEL_SUMMARY_CACHE: dict[Path, dict[str, tuple[dict, ...]]] = {}

class RuleError(ValueError):
    pass


def _unknown(value: dict, allowed: set[str], path: str) -> None:
    extra = set(value) - allowed
    if extra:
        raise RuleError(f"{path}: unknown fields: {', '.join(sorted(extra))}")


def _object(value: object, path: str) -> dict:
    if not isinstance(value, dict):
        raise RuleError(f"{path}: must be an object")
    return value


def array(value: object, path: str) -> list:
    if not isinstance(value, list):
        raise RuleError(f"{path}: must be an array")
    return value


def _integer(value: object, path: str, minimum: int, maximum: int) -> int:
    if not isinstance(value, int) or isinstance(value, bool) or not minimum <= value <= maximum:
        raise RuleError(f"{path}: value must be an integer from {minimum} through {maximum}")
    return value


def number(value: object, path: str, minimum: float = -8.0, maximum: float = 8.0) -> float:
    if not isinstance(value, (int, float)) or isinstance(value, bool):
        raise RuleError(f"{path}: expected a number")
    result = float(value)
    if not math.isfinite(result) or not minimum <= result <= maximum:
        raise RuleError(f"{path}: value must be between {minimum} and {maximum}")
    return result


def _identifier(value: object, path: str) -> str:
    if not isinstance(value, str) or not IDENTIFIER.fullmatch(value):
        raise RuleError(f"{path}: must be a lower-case identifier")
    return value


def _allowance(value: object, path: str) -> dict | None:
    if value is None:
        return None
    item = _object(value, path)
    _unknown(item, {"reason"}, path)
    if not isinstance(item.get("reason"), str) or not item["reason"].strip():
        raise RuleError(f"{path}.reason: must be a non-empty string")
    return {"reason": item["reason"]}


def load_json(path: Path) -> dict:
    def pairs_hook(pairs):
        result = {}
        for key, value in pairs:
            if key in result:
                raise RuleError(f"{path}: duplicate key {key!r}")
            result[key] = value
        return result

    try:
        value = json.loads(path.read_text(encoding="utf-8"), object_pairs_hook=pairs_hook)
    except (OSError, json.JSONDecodeError) as error:
        raise RuleError(f"{path}: {error}") from error
    if not isinstance(value, dict):
        raise RuleError(f"{path}: root must be an object")
    if value.get("schemaVersion") != SCHEMA_VERSION:
        raise RuleError(f"{path}: schemaVersion must be 2; run migrate_v1_to_v2.py explicitly for v1")
    return value


def parse_behaviors(path: Path) -> dict[str, int]:
    text = re.sub(r"//[^\n]*|/\*.*?\*/", "", path.read_text(encoding="utf-8"), flags=re.S)
    match = re.search(r"enum\s*\{(.*?)\};", text, flags=re.S)
    if match is None:
        raise RuleError(f"{path}: behavior enum not found")
    result, current = {}, -1
    for entry in match.group(1).split(","):
        item = re.search(r"\b(MB_[A-Z0-9_]+)\b(?:\s*=\s*(0x[0-9A-Fa-f]+|[0-9]+))?", entry)
        if item:
            current = int(item.group(2), 0) if item.group(2) else current + 1
            result[item.group(1)] = current
    return result


def parse_camera(value: object, path: str) -> tuple[str, float, float]:
    item = _object(value if value is not None else {"profile": "exterior"}, path)
    _unknown(item, {"profile", "pitch", "focalLength"}, path)
    profile = item.get("profile")
    if profile not in CAMERAS:
        raise RuleError(f"{path}.profile: unknown camera profile {profile!r}")
    default_pitch, default_focal = CAMERAS[profile]
    pitch = number(item.get("pitch", default_pitch), f"{path}.pitch", 20.0, 70.0)
    focal = number(item.get("focalLength", default_focal), f"{path}.focalLength", 80.0, 200.0)
    return profile, math.radians(pitch), focal


def _ground_policy(value: object, path: str) -> dict:
    item = _object(value, path)
    mode = item.get("mode")
    if mode == "automatic":
        _unknown(item, {"mode"}, path)
        return {"mode": mode}
    if mode == "manual":
        _unknown(item, {"mode", "metatile"}, path)
        return {"mode": mode, "metatile": _integer(item.get("metatile"), f"{path}.metatile", 0, 1023)}
    raise RuleError(f"{path}.mode: must be 'automatic' or 'manual'")


def _voxel_number(value: object, path: str, minimum: float = -8.0,
                  maximum: float = 8.0) -> float:
    result = number(value, path, minimum, maximum)
    if not math.isclose(result * 16, round(result * 16), abs_tol=1e-9):
        raise RuleError(f"{path}: must align to the 1/16-cell voxel grid")
    return result


def _action(value: object, path: str, pools: set[str], profiles: dict[str, dict]) -> dict:
    item = _object(value, path)
    _unknown(item, {"archetype", "pool", "profile", "axis", "shape", "terrainClass",
                    "groundOffset", "depthOffset", "thickness", "pixelRows",
                    "objectGroundMode", "spriteDepthBias", "height",
                    "groundPolicy", "faces"}, path)
    archetype = item.get("archetype")
    if archetype not in ARCHETYPES:
        raise RuleError(f"{path}.archetype: unknown archetype {archetype!r}")
    pool = item.get("pool")
    if pool not in pools:
        raise RuleError(f"{path}.pool: unknown semantic pool {pool!r}")
    default_class = ("water" if archetype in ("water", "shallow-water", "waterfall",
                                              "current", "hot-spring")
                     else "void" if archetype in ("void", "claim-only")
                     else "wood" if archetype in ("bridge", "deck", "rail", "support")
                     else "rock" if archetype in ("ledge", "cliff", "mound", "wall-volume")
                     else "ground")
    output = {"archetype": archetype, "pool": pool, "terrainClass": default_class}
    if "shape" in item:
        if item["shape"] not in TERRAIN_SHAPES:
            raise RuleError(f"{path}.shape: unknown shape {item['shape']!r}")
        output["shape"] = item["shape"]
    if "terrainClass" in item:
        if item["terrainClass"] not in TERRAIN_CLASSES:
            raise RuleError(f"{path}.terrainClass: unknown terrain class {item['terrainClass']!r}")
        output["terrainClass"] = item["terrainClass"]
    if "profile" in item:
        profile = item["profile"]
        if profile not in profiles:
            raise RuleError(f"{path}.profile: unknown profile {profile!r}")
        if profiles[profile]["archetype"] != archetype:
            raise RuleError(f"{path}.profile: profile archetype does not match {archetype}")
        output["profile"] = profile
    if "axis" in item:
        if item["axis"] not in ("x", "z", "cross"):
            raise RuleError(f"{path}.axis: must be x, z or cross")
        output["axis"] = item["axis"]
    if "groundOffset" in item:
        output["groundOffset"] = _voxel_number(item["groundOffset"], f"{path}.groundOffset")
    if "depthOffset" in item:
        output["depthOffset"] = _voxel_number(item["depthOffset"], f"{path}.depthOffset")
    if "thickness" in item:
        output["thickness"] = _voxel_number(item["thickness"], f"{path}.thickness", 0.0, 1.0)
    if "pixelRows" in item:
        rows = array(item["pixelRows"], f"{path}.pixelRows")
        if len(rows) != 2:
            raise RuleError(f"{path}.pixelRows: expected [start, end]")
        start = _integer(rows[0], f"{path}.pixelRows[0]", 0, 15)
        end = _integer(rows[1], f"{path}.pixelRows[1]", 1, 16)
        if start >= end:
            raise RuleError(f"{path}.pixelRows: start must be less than end")
        output["pixelRows"] = [start, end]
    if "objectGroundMode" in item:
        if item["objectGroundMode"] not in ("replacement", "source-base"):
            raise RuleError(f"{path}.objectGroundMode: must be replacement or source-base")
        output["objectGroundMode"] = item["objectGroundMode"]
    if "spriteDepthBias" in item:
        output["spriteDepthBias"] = number(
            item["spriteDepthBias"], f"{path}.spriteDepthBias", 0.0, 0.01)
    if "height" in item:
        output["height"] = _voxel_number(item["height"], f"{path}.height", 0.0, 32.0)
    if "groundPolicy" in item:
        output["groundPolicy"] = _ground_policy(item["groundPolicy"], f"{path}.groundPolicy")
    if "faces" in item:
        faces = _object(item["faces"], f"{path}.faces")
        _unknown(faces, set(MATERIAL_FACES), f"{path}.faces")
        normalized_faces = {}
        for face, raw in faces.items():
            face_path = f"{path}.faces.{face}"
            if raw == "none":
                normalized_faces[face] = "none"
                continue
            material = _object(raw, face_path)
            _unknown(material, {"metatile", "layer", "rotation", "flipX", "flipY"}, face_path)
            metatile = material.get("metatile")
            if metatile != "self":
                metatile = _integer(metatile, f"{face_path}.metatile", 0, 1023)
            layer = material.get("layer")
            if layer not in MATERIAL_LAYERS:
                raise RuleError(f"{face_path}.layer: unknown material layer {layer!r}")
            rotation = _integer(material.get("rotation", 0), f"{face_path}.rotation", 0, 270)
            if rotation not in (0, 90, 180, 270):
                raise RuleError(f"{face_path}.rotation: must be 0, 90, 180, or 270")
            flip_x = material.get("flipX", False)
            flip_y = material.get("flipY", False)
            if not isinstance(flip_x, bool) or not isinstance(flip_y, bool):
                raise RuleError(f"{face_path}: flipX and flipY must be booleans")
            normalized_faces[face] = {"metatile": metatile, "layer": layer,
                                      "rotation": rotation, "flipX": flip_x,
                                      "flipY": flip_y}
        output["faces"] = normalized_faces
    return output


# Kept as a small compatibility entry point for callers that used the old helper.
def parse_definition(value: object, path: str) -> dict:
    return _action(value, path, {"default"}, {})


def _mask(value: object, path: str) -> dict:
    item = _object(value, path)
    _unknown(item, {"width", "height", "rows"}, path)
    width = _integer(item.get("width"), f"{path}.width", 1, 64)
    height = _integer(item.get("height"), f"{path}.height", 1, 64)
    rows = array(item.get("rows"), f"{path}.rows")
    digits = (width + 3) // 4
    if len(rows) != height:
        raise RuleError(f"{path}.rows: expected {height} rows")
    for index, row in enumerate(rows):
        if not isinstance(row, str) or not re.fullmatch(f"[0-9a-f]{{{digits}}}", row):
            raise RuleError(f"{path}.rows[{index}]: expected {digits} lower-case hex digits")
        if int(row, 16) >= 1 << width:
            raise RuleError(f"{path}.rows[{index}]: sets bits outside mask width")
    return {"width": width, "height": height, "rows": rows}


def _profiles(values: object, path: str, tilesets: dict[str, TilesetInfo]) -> tuple[list[dict], dict[str, dict]]:
    output, by_id = [], {}
    for index, raw in enumerate(array(values, path)):
        item_path = f"{path}[{index}]"
        item = _object(raw, item_path)
        _unknown(item, {"id", "archetype", "masks", "samples"}, item_path)
        profile_id = _identifier(item.get("id"), f"{item_path}.id")
        if profile_id in by_id:
            raise RuleError(f"{item_path}.id: duplicate profile {profile_id}")
        archetype = item.get("archetype")
        if archetype not in ARCHETYPES:
            raise RuleError(f"{item_path}.archetype: unknown archetype {archetype!r}")
        masks = _object(item.get("masks"), f"{item_path}.masks")
        _unknown(masks, {"occupancy", "material", "claim"}, f"{item_path}.masks")
        if not masks:
            raise RuleError(f"{item_path}.masks: at least one mask is required")
        normalized_masks = {name: _mask(mask, f"{item_path}.masks.{name}")
                            for name, mask in sorted(masks.items())}
        samples = []
        for sample_index, raw_sample in enumerate(array(item.get("samples"), f"{item_path}.samples")):
            sample_path = f"{item_path}.samples[{sample_index}]"
            sample = _object(raw_sample, sample_path)
            _unknown(sample, {"tileset", "metatile", "layer"}, sample_path)
            symbol = sample.get("tileset")
            if symbol not in tilesets:
                raise RuleError(f"{sample_path}.tileset: unknown tileset {symbol!r}")
            metatile = _integer(sample.get("metatile"), f"{sample_path}.metatile", 0,
                                tilesets[symbol].metatile_count - 1)
            if sample.get("layer") not in MATERIAL_LAYERS:
                raise RuleError(f"{sample_path}.layer: unknown material layer {sample.get('layer')!r}")
            samples.append({"tileset": symbol, "metatile": metatile, "layer": sample["layer"]})
        if not samples:
            raise RuleError(f"{item_path}.samples: at least one sample is required")
        normalized = {"id": profile_id, "archetype": archetype,
                      "masks": normalized_masks, "samples": samples}
        output.append(normalized)
        by_id[profile_id] = normalized
    output.sort(key=lambda item: item["id"])
    return output, by_id


def _neighbor(value: object, path: str, behaviors: dict[str, int]) -> dict:
    item = _object(value, path)
    _unknown(item, {"metatile", "behavior", "layerType", "elevation"}, path)
    if not item:
        raise RuleError(f"{path}: neighbor selector cannot be empty")
    output = {}
    if "metatile" in item:
        output["metatile"] = _integer(item["metatile"], f"{path}.metatile", 0, 1023)
    if "behavior" in item:
        if item["behavior"] not in behaviors:
            raise RuleError(f"{path}.behavior: unknown behavior {item['behavior']!r}")
        output["behavior"] = item["behavior"]
        output["behaviorId"] = behaviors[item["behavior"]]
    if "layerType" in item:
        if item["layerType"] not in LAYERS:
            raise RuleError(f"{path}.layerType: unknown layer type {item['layerType']!r}")
        output["layerType"] = item["layerType"]
    if "elevation" in item:
        output["elevation"] = _integer(item["elevation"], f"{path}.elevation", 0, 15)
    return output


def _selector(value: object, path: str, tilesets: dict[str, TilesetInfo],
              behaviors: dict[str, int]) -> dict:
    item = _object(value, path)
    _unknown(item, {"tilesets", "metatiles", "behaviors", "layerTypes", "elevations",
                    "mapTypes", "neighbors", "event"}, path)
    if not item:
        raise RuleError(f"{path}: selector cannot be empty")
    output = {}
    for field, universe in (("tilesets", tilesets), ("behaviors", behaviors),
                            ("layerTypes", LAYERS), ("mapTypes", MAP_TYPES)):
        if field in item:
            values = array(item[field], f"{path}.{field}")
            if not values or len(set(values)) != len(values):
                raise RuleError(f"{path}.{field}: must be a non-empty unique array")
            unknown = set(values) - set(universe)
            if unknown:
                raise RuleError(f"{path}.{field}: unknown references: {', '.join(sorted(unknown))}")
            output[field] = sorted(values)
    if "behaviors" in output:
        output["behaviorIds"] = [behaviors[value] for value in output["behaviors"]]
    for field, maximum in (("metatiles", 1023), ("elevations", 15)):
        if field in item:
            values = [_integer(value, f"{path}.{field}[{index}]", 0, maximum)
                      for index, value in enumerate(array(item[field], f"{path}.{field}"))]
            if not values or len(set(values)) != len(values):
                raise RuleError(f"{path}.{field}: must be a non-empty unique array")
            output[field] = sorted(values)
    if "neighbors" in item:
        neighbors = _object(item["neighbors"], f"{path}.neighbors")
        _unknown(neighbors, set(DIRECTIONS), f"{path}.neighbors")
        if not neighbors:
            raise RuleError(f"{path}.neighbors: cannot be empty")
        output["neighbors"] = {direction: _neighbor(rule, f"{path}.neighbors.{direction}", behaviors)
                               for direction, rule in sorted(neighbors.items())}
    if "event" in item:
        event = _object(item["event"], f"{path}.event")
        _unknown(event, {"kind", "class"}, f"{path}.event")
        if event.get("kind") not in EVENT_KINDS:
            raise RuleError(f"{path}.event.kind: unknown event kind {event.get('kind')!r}")
        output["event"] = {"kind": event["kind"]}
        if "class" in event:
            output["event"]["class"] = _identifier(event["class"], f"{path}.event.class")
    return output


def _rule_list(values: object, path: str, tilesets: dict[str, TilesetInfo], behaviors: dict[str, int],
               pools: set[str], profiles: dict[str, dict]) -> list[dict]:
    output, ids = [], set()
    for index, raw in enumerate(array(values, path)):
        item_path = f"{path}[{index}]"
        item = _object(raw, item_path)
        _unknown(item, {"id", "selector", "action", "priority", "allowedUnused"}, item_path)
        rule_id = _identifier(item.get("id"), f"{item_path}.id")
        if rule_id in ids:
            raise RuleError(f"{item_path}.id: duplicate rule {rule_id}")
        ids.add(rule_id)
        output.append({"id": rule_id,
                       "selector": _selector(item.get("selector"), f"{item_path}.selector", tilesets, behaviors),
                       "action": _action(item.get("action"), f"{item_path}.action", pools, profiles),
                       "priority": _integer(item.get("priority", 0), f"{item_path}.priority", -32768, 32767),
                       "allowedUnused": _allowance(item.get("allowedUnused"), f"{item_path}.allowedUnused")})
    return sorted(output, key=lambda item: (-item["priority"], item["id"]))


def _patterns(values: object, path: str, tilesets: dict[str, TilesetInfo], pools: set[str],
               profiles: dict[str, dict]) -> list[dict]:
    output, ids = [], set()
    for index, raw in enumerate(array(values, path)):
        item_path = f"{path}[{index}]"
        item = _object(raw, item_path)
        _unknown(item, {"id", "dimensions", "tilesets", "cells", "priority", "claimMask",
                        "action", "allowedNoPlacements"}, item_path)
        pattern_id = _identifier(item.get("id"), f"{item_path}.id")
        if pattern_id in ids:
            raise RuleError(f"{item_path}.id: duplicate pattern {pattern_id}")
        ids.add(pattern_id)
        dimensions = _object(item.get("dimensions"), f"{item_path}.dimensions")
        _unknown(dimensions, {"width", "height"}, f"{item_path}.dimensions")
        width = _integer(dimensions.get("width"), f"{item_path}.dimensions.width", 1, 32)
        height = _integer(dimensions.get("height"), f"{item_path}.dimensions.height", 1, 32)
        cells = array(item.get("cells"), f"{item_path}.cells")
        if len(cells) != height:
            raise RuleError(f"{item_path}.cells: expected {height} rows")
        normalized_cells = []
        for row_index, raw_row in enumerate(cells):
            row = array(raw_row, f"{item_path}.cells[{row_index}]")
            if len(row) != width:
                raise RuleError(f"{item_path}.cells[{row_index}]: expected {width} cells")
            normalized_cells.append([_integer(cell, f"{item_path}.cells[{row_index}]", 0, 1023)
                                     for cell in row])
        claim = array(item.get("claimMask"), f"{item_path}.claimMask")
        if len(claim) != height or any(not isinstance(row, str) or
                                       not re.fullmatch(f"[01]{{{width}}}", row) for row in claim):
            raise RuleError(f"{item_path}.claimMask: expected {height} binary rows of width {width}")
        if not any("1" in row for row in claim):
            raise RuleError(f"{item_path}.claimMask: must claim at least one cell")
        pair = _object(item.get("tilesets"), f"{item_path}.tilesets")
        _unknown(pair, {"primary", "secondary"}, f"{item_path}.tilesets")
        if set(pair) != {"primary", "secondary"}:
            raise RuleError(f"{item_path}.tilesets: primary and secondary are required")
        for role in ("primary", "secondary"):
            symbol = pair[role]
            if symbol not in tilesets or tilesets[symbol].role != role:
                raise RuleError(f"{item_path}.tilesets.{role}: unknown {role} tileset {symbol!r}")
        primary_count = tilesets[pair["primary"]].metatile_count
        secondary_count = tilesets[pair["secondary"]].metatile_count
        for row_index, row in enumerate(normalized_cells):
            for column_index, metatile in enumerate(row):
                valid = (metatile < 0x200 and metatile < primary_count) or (
                    metatile >= 0x200 and metatile - 0x200 < secondary_count)
                if not valid:
                    raise RuleError(
                        f"{item_path}.cells[{row_index}][{column_index}]: metatile {metatile} "
                        "is outside the declared primary/secondary tileset counts")
        output.append({"id": pattern_id, "dimensions": {"width": width, "height": height},
                       "tilesets": dict(pair), "cells": normalized_cells,
                       "priority": _integer(item.get("priority"), f"{item_path}.priority", -32768, 32767),
                       "claimMask": claim,
                       "action": _action(item.get("action"), f"{item_path}.action", pools, profiles),
                       "allowedNoPlacements": _allowance(item.get("allowedNoPlacements"),
                                                         f"{item_path}.allowedNoPlacements")})
    return sorted(output, key=lambda item: (-item["priority"], item["id"]))


def _animation_provenance(root: Path, tilesets: dict[str, TilesetInfo],
                          layouts: list[dict]) -> dict[tuple[str, str, int], tuple[str, ...]]:
    slots_by_callback = load_animation_slots(root)
    result = {}
    pairs = sorted({(layout["primary_tileset"], layout["secondary_tileset"])
                    for layout in layouts if layout["primary_tileset"] in tilesets
                    and layout["secondary_tileset"] in tilesets})
    metatiles = {symbol: (root / info.metatiles_root / "metatiles.bin").read_bytes()
                 for symbol, info in tilesets.items()}
    for primary, secondary in pairs:
        pair = primary + "|" + secondary
        for symbol in (primary, secondary):
            for metatile, values in enumerate(struct.iter_unpack("<8H", metatiles[symbol])):
                sources = set()
                for value in values:
                    tile = value & 0x3FF
                    role = "secondary" if tile >= 0x200 else "primary"
                    graphics_symbol = secondary if role == "secondary" else primary
                    local_tile = tile - 0x200 if role == "secondary" else tile
                    for slot in slots_by_callback.get(tilesets[graphics_symbol].callback, ()):
                        if slot.role == role and slot.local_start <= local_tile \
                                < slot.local_start + slot.tile_count:
                            sources.add(slot.source)
                if sources:
                    result[(pair, symbol, metatile)] = tuple(sorted(sources))
    return result


def _load_cells(root: Path, layouts: list[dict], tilesets: dict[str, TilesetInfo],
                 behavior_names: dict[int, str]) -> tuple[dict[str, tuple[dict, ...]], Counter, Counter]:
    attributes = {}
    for symbol, info in tilesets.items():
        raw = (root / info.metatiles_root / "metatile_attributes.bin").read_bytes()
        attributes[symbol] = tuple(value for (value,) in struct.iter_unpack("<H", raw))
    animations = _animation_provenance(root, tilesets, layouts)
    cells_by_layout, behavior_counts, pin_counts = {}, Counter(), Counter()
    for layout in layouts:
        primary, secondary = layout["primary_tileset"], layout["secondary_tileset"]
        raw = (root / layout["blockdata_filepath"]).read_bytes()
        count = layout["width"] * layout["height"]
        if len(raw) < count * 2 or len(raw) % 2:
            raise RuleError(f"{layout['blockdata_filepath']}: invalid layout block data")
        cells = []
        for (entry,) in struct.iter_unpack("<H", raw[:count * 2]):
            metatile = entry & 0x3FF
            symbol = secondary if metatile >= 0x200 else primary
            local_id = metatile - 0x200 if metatile >= 0x200 else metatile
            if symbol not in tilesets:
                cells.append({"metatile": metatile, "tileset": symbol,
                              "localMetatile": local_id, "behavior": None,
                              "behaviorId": None, "layerType": None,
                              "collision": (entry >> 10) & 3,
                              "elevation": (entry >> 12) & 15,
                              "missingTileset": True})
                continue
            if local_id >= tilesets[symbol].metatile_count:
                raise RuleError(f"{layout['id']}: metatile {metatile} exceeds {symbol}")
            attribute = attributes[symbol][local_id]
            behavior_id, layer_id = attribute & 0xFF, (attribute >> 12) & 0xF
            behavior = behavior_names.get(behavior_id)
            animation_sources = animations.get((primary + "|" + secondary, symbol, local_id), ())
            cell = {"metatile": metatile, "tileset": symbol, "localMetatile": local_id,
                     "behavior": behavior, "behaviorId": behavior_id,
                     "layerType": LAYERS[layer_id] if layer_id < 3 else None,
                     "collision": (entry >> 10) & 3, "elevation": (entry >> 12) & 15}
            if animation_sources:
                cell["animationSources"] = animation_sources
                if any("Flower" in source for source in animation_sources):
                    cell["animationClass"] = "flower"
            cells.append(cell)
            pin_counts[(symbol, local_id)] += 1
            if behavior:
                behavior_counts[behavior] += 1
        cells_by_layout[layout["id"]] = tuple(cells)
    return cells_by_layout, behavior_counts, pin_counts


def _event_class(kind: str, event: dict, cell: dict | None) -> str:
    if kind == "background":
        return str(event.get("type", "background")).lower().replace("_", "-")
    if kind == "coordinate":
        return str(event.get("type", "coordinate")).lower().replace("_", "-")
    if kind == "warp":
        return "door" if cell and "DOOR" in (cell.get("behavior") or "") else "warp"
    graphics, script = event.get("graphics_id", ""), event.get("script", "")
    signatures = (("cut-obstacle", "CUTTABLE_TREE", "CutTree"),
                  ("rock-smash-obstacle", "BREAKABLE_ROCK", "RockSmash"),
                  ("berry-tree", "BERRY_TREE", "BerryTree"),
                  ("strength-boulder", "PUSHABLE_BOULDER", "StrengthBoulder"))
    return next((name for name, gfx, code in signatures if gfx in graphics or code in script), "object")


def _events(root: Path, maps: list[dict], cells: dict[str, tuple[dict, ...]],
            layouts_by_id: dict[str, dict]) -> dict[tuple[str, int, int], list[dict]]:
    result = defaultdict(list)
    for map_row in maps:
        source = json.loads((root / "data/maps" / map_row["directory"] / "map.json").read_text(encoding="utf-8"))
        layout = layouts_by_id[map_row["layout"]]
        for kind, field in (("object", "object_events"), ("warp", "warp_events"),
                            ("coordinate", "coord_events"), ("background", "bg_events")):
            for event in source.get(field, []):
                x, y = event.get("x"), event.get("y")
                cell = None
                if isinstance(x, int) and isinstance(y, int) and 0 <= x < layout["width"] and 0 <= y < layout["height"]:
                    layout_cells = cells[layout["id"]]
                    cell = layout_cells[y * layout["width"] + x] if layout_cells else None
                    classification = _event_class(kind, event, cell)
                    normalized = {"kind": kind, "class": classification}
                    result[(map_row["symbol"], x, y)].append(normalized)
    return result


def _matches_neighbor(rule: dict, cell: dict) -> bool:
    return all(key == "behaviorId" or
               cell.get({"metatile": "metatile", "behavior": "behavior", "layerType": "layerType",
                         "elevation": "elevation"}[key]) == value for key, value in rule.items())


def _rule_placements(rule: dict, layouts: list[dict], maps_by_layout: dict[str, list[dict]],
                     cells: dict[str, tuple[dict, ...]], events: dict[tuple[str, int, int], list[dict]]) -> int:
    selector, total = rule["selector"], 0
    offsets = {"n": (0, -1), "ne": (1, -1), "e": (1, 0), "se": (1, 1),
               "s": (0, 1), "sw": (-1, 1), "w": (-1, 0), "nw": (-1, -1)}
    for layout in layouts:
        layout_cells = cells[layout["id"]]
        if not layout_cells:
            continue
        map_rows = maps_by_layout.get(layout["id"], [])
        for index, cell in enumerate(layout_cells):
            if any(field in selector and cell[key] not in selector[field]
                   for field, key in (("tilesets", "tileset"), ("metatiles", "metatile"),
                                      ("behaviors", "behavior"), ("layerTypes", "layerType"),
                                      ("elevations", "elevation"))):
                continue
            x, y = index % layout["width"], index // layout["width"]
            failed = False
            for direction, neighbor_rule in selector.get("neighbors", {}).items():
                dx, dy = offsets[direction]
                nx, ny = x + dx, y + dy
                if not (0 <= nx < layout["width"] and 0 <= ny < layout["height"] and
                        _matches_neighbor(neighbor_rule, layout_cells[ny * layout["width"] + nx])):
                    failed = True
                    break
            if failed:
                continue
            eligible_maps = [row for row in map_rows if not selector.get("mapTypes") or
                             row["mapType"] in selector["mapTypes"]]
            if "event" in selector:
                wanted = selector["event"]
                eligible_maps = [row for row in eligible_maps if any(
                    event["kind"] == wanted["kind"] and
                    ("class" not in wanted or event["class"] == wanted["class"])
                    for event in events.get((row["symbol"], x, y), []))]
            total += len(eligible_maps) if ("mapTypes" in selector or "event" in selector) else 1
    return total


def _pattern_placements(pattern: dict, layouts: list[dict], cells: dict[str, tuple[dict, ...]]) -> int:
    width, height = pattern["dimensions"].values()
    expected = [value for row in pattern["cells"] for value in row]
    total = 0
    for layout in layouts:
        if (layout["primary_tileset"] != pattern["tilesets"]["primary"] or
                layout["secondary_tileset"] != pattern["tilesets"]["secondary"]):
            continue
        ids = [cell["metatile"] for cell in cells[layout["id"]]]
        for y in range(layout["height"] - height + 1):
            for x in range(layout["width"] - width + 1):
                actual = [ids[(y + row) * layout["width"] + x + column]
                          for row in range(height) for column in range(width)]
                total += actual == expected
    return total


def _pattern_origins(pattern: dict, layout: dict, cells: dict[str, tuple[dict, ...]]):
    width = pattern["dimensions"]["width"]
    height = pattern["dimensions"]["height"]
    expected = [value for row in pattern["cells"] for value in row]
    ids = [cell["metatile"] for cell in cells[layout["id"]]]
    for y in range(layout["height"] - height + 1):
        for x in range(layout["width"] - width + 1):
            actual = [ids[(y + row) * layout["width"] + x + column]
                      for row in range(height) for column in range(width)]
            if actual == expected:
                yield x, y


def _validate_pattern_claim_overlaps(global_patterns: list[dict], maps: list[dict],
                                     layouts_by_id: dict[str, dict],
                                     cells: dict[str, tuple[dict, ...]],
                                     local_patterns: dict[str, list[dict]]) -> None:
    """Reject ambiguous equal-priority claims at actual matched coordinates."""
    claims = {}
    contexts = [(map_row["symbol"], layouts_by_id[map_row["layout"]],
                 local_patterns.get(map_row["symbol"], [])) for map_row in maps]
    mapped_layouts = {map_row["layout"] for map_row in maps}
    contexts.extend((f"layout {layout_id}", layout, [])
                    for layout_id, layout in layouts_by_id.items() if layout_id not in mapped_layouts)
    for context, layout, scoped_patterns in contexts:
        applicable = [(pattern, None) for pattern in global_patterns]
        applicable.extend((pattern, context) for pattern in scoped_patterns)
        for pattern, scope in applicable:
            if (layout["primary_tileset"] != pattern["tilesets"]["primary"] or
                    layout["secondary_tileset"] != pattern["tilesets"]["secondary"]):
                continue
            width = pattern["dimensions"]["width"]
            for x, y in _pattern_origins(pattern, layout, cells):
                placement = (pattern["id"], scope, x, y)
                for row, mask in enumerate(pattern["claimMask"]):
                    for column, claimed in enumerate(mask):
                        if claimed != "1":
                            continue
                        key = (context, (y + row) * layout["width"] + x + column,
                               pattern["priority"])
                        previous = claims.get(key)
                        if previous is not None and previous != placement:
                            raise RuleError(
                                f"equal-priority pattern claim overlap on {context} "
                                f"between {previous[0]!r} and {pattern['id']!r}")
                        claims[key] = placement


def _validate_logical_ids(entries: list[tuple[str, str]]) -> None:
    seen = {}
    for logical_id, location in entries:
        if logical_id in seen:
            raise RuleError(
                f"duplicate logical ID {logical_id!r}: {seen[logical_id]} and {location}")
        seen[logical_id] = location


def _parse_pin_files(root: Path, tilesets: dict[str, TilesetInfo], pools: set[str],
                     profiles: dict[str, dict], pin_counts: Counter) -> list[dict]:
    output, seen = [], set()
    for path in sorted((root / "data/diorama/tilesets").glob("*.json")):
        data = load_json(path)
        _unknown(data, {"schemaVersion", "kind", "tileset", "pins"}, str(path))
        if data.get("kind") != "tileset":
            raise RuleError(f"{path}.kind: must be 'tileset'")
        symbol = data.get("tileset")
        if symbol not in tilesets:
            raise RuleError(f"{path}.tileset: unknown tileset {symbol!r}")
        for index, raw in enumerate(array(data.get("pins"), f"{path}.pins")):
            item_path = f"{path}.pins[{index}]"
            item = _object(raw, item_path)
            _unknown(item, {"metatile", "action", "reason", "allowedUnused"}, item_path)
            metatile = _integer(item.get("metatile"), f"{item_path}.metatile", 0,
                                tilesets[symbol].metatile_count - 1)
            if (symbol, metatile) in seen:
                raise RuleError(f"{item_path}: duplicate pin for {symbol} metatile {metatile}")
            seen.add((symbol, metatile))
            count = pin_counts[(symbol, metatile)]
            allowance = _allowance(item.get("allowedUnused"), f"{item_path}.allowedUnused")
            if not count and allowance is None:
                raise RuleError(f"{item_path}: dead pin has no placements; add structured allowedUnused")
            action = _action(item.get("action"), f"{item_path}.action", pools, profiles)
            reason = item.get("reason")
            if reason is not None and (not isinstance(reason, str) or not reason.strip()):
                raise RuleError(f"{item_path}.reason: must be a non-empty string")
            output.append({"tileset": symbol, "metatile": metatile,
                            "action": action,
                            "placementCount": count, "reason": reason,
                            "allowedUnused": allowance})
    return sorted(output, key=lambda item: (item["tileset"], item["metatile"]))


def _with_animation_provenance(shape: TileShape, cell: dict) -> TileShape:
    sources = tuple(cell.get("animationSources", ()))
    if not sources:
        return shape
    evidence = shape.evidence + tuple(f"animation:{source}" for source in sources)
    source = shape.source
    if (cell.get("animationClass") == "flower" and shape.class_name == "flower"
            and not source.startswith(("pin:", "context:"))):
        source = "animation:" + next(item for item in sources if "Flower" in item)
    return TileShape(shape.class_name, shape.height, shape.art_mode, shape.pool,
                     shape.authored, source, shape.confidence, evidence,
                     shape.ambiguity, shape.prop_ground)


def _visible_components(pixels: tuple) -> int:
    pending = {index for index, pixel in enumerate(pixels) if pixel.visible}
    components = 0
    while pending:
        components += 1
        stack = [pending.pop()]
        while stack:
            index = stack.pop()
            x, y = index % 16, index // 16
            for neighbor in ((index - 1 if x else -1), (index + 1 if x < 15 else -1),
                             (index - 16 if y else -1), (index + 16 if y < 15 else -1)):
                if neighbor in pending:
                    pending.remove(neighbor)
                    stack.append(neighbor)
    return components


def _pixel_summaries(root: Path, layouts: list[dict], cells: dict[str, tuple[dict, ...]],
                     tilesets: dict[str, TilesetInfo]) -> dict[str, tuple[dict, ...]]:
    """Compose each logical metatile once per tileset pair with source-safe pixels."""
    if root in _PIXEL_SUMMARY_CACHE:
        return _PIXEL_SUMMARY_CACHE[root]
    slots = load_animation_slots(root)
    cache = {}
    output = {}
    for layout in layouts:
        primary_symbol = layout["primary_tileset"]
        secondary_symbol = layout["secondary_tileset"]
        pair = (primary_symbol, secondary_symbol)
        if pair not in cache:
            if primary_symbol not in tilesets or secondary_symbol not in tilesets:
                cache[pair] = (None, {})
            else:
                primary = tilesets[primary_symbol]
                secondary = tilesets[secondary_symbol]
                composer = TilesetComposer(
                    root / primary.root, root / secondary.root,
                    root / primary.metatiles_root, root / secondary.metatiles_root,
                    slots.get(primary.callback, ()), slots.get(secondary.callback, ()),
                    animation_frame=0)
                cache[pair] = (composer, {})
        composer, summaries = cache[pair]
        rows = []
        for cell in cells[layout["id"]]:
            metatile = cell["metatile"]
            if metatile not in summaries:
                if composer is None:
                    summaries[metatile] = {"metatile": metatile, "resolved": False, "visible": 0,
                                           "transparent": 256, "black": 0,
                                           "components": 0, "signature": ""}
                else:
                    before = set(composer.missing_tile_details)
                    layers = composer.compose_metatile(metatile)
                    full = layers[2]
                    missing = set(composer.missing_tile_details) - before
                    visible = sum(pixel.visible for pixel in full)
                    black = sum(pixel.visible and pixel.rgba[:3] == (0, 0, 0)
                                for pixel in full)
                    rgba = bytes(channel for pixel in full for channel in pixel.rgba)
                    summaries[metatile] = {
                        "metatile": metatile, "resolved": not missing, "visible": visible,
                        "transparent": len(full) - visible, "black": black,
                        "components": _visible_components(full),
                        "signature": hashlib.sha256(rgba).hexdigest(),
                        "layers": layers,
                    }
            rows.append(summaries[metatile])
        output[layout["id"]] = tuple(rows)
    _PIXEL_SUMMARY_CACHE[root] = output
    return output


def _apply_derived_voids(shapes: tuple[TileShape, ...], analysis: dict) -> tuple[TileShape, ...]:
    output = []
    for shape, structural in zip(shapes, analysis["cells"]):
        kind = structural["voidKind"]
        if kind == "none":
            output.append(shape)
            continue
        output.append(TileShape(
            "void", 0.0, "flat", shape.pool, False, f"structure:void-{kind}", 1.0,
            shape.evidence + (f"pixels:void-{kind}", "metadata:unauthored"), (),
            shape.prop_ground))
    return tuple(output)


def _structure_semantic(analysis: dict, offset: int) -> tuple:
    cell = analysis["cells"][offset]
    values = []
    for field in ("owner", "region"):
        local_id = cell[field]
        if local_id is None:
            values.append(None)
            continue
        candidate = analysis["candidates"][local_id - 1]
        values.append(tuple((key, json.dumps(candidate[key], sort_keys=True,
                                             separators=(",", ":")))
                            for key in sorted(candidate) if key != "id"))
    return tuple(values) + (cell["doorFold"], cell["voidKind"], cell["source"])


def _register_candidates(analysis: dict, layout_id: int, map_group: int, map_number: int,
                         catalog: list[dict], local_ids: set[int] | None = None) -> dict[int, int]:
    mapping = {}
    for candidate in analysis["candidates"]:
        if local_ids is not None and candidate["id"] not in local_ids:
            continue
        global_id = len(catalog) + 1
        if global_id > 0xFFFF:
            raise RuleError("R3 structure candidate catalog exceeds uint16_t ownership")
        mapping[candidate["id"]] = global_id
        catalog.append({**candidate, "id": global_id, "layoutId": layout_id,
                        "mapGroup": map_group, "mapNumber": map_number})
    return mapping


def _register_pixel_objects(extracted: list[dict], mapping: dict[int, int], layout_id: int,
                            map_group: int, map_number: int, catalog: list[dict]) -> None:
    for item in extracted:
        structure_id = mapping.get(item["structureId"])
        if structure_id is None:
            continue
        object_id = len(catalog) + 1
        if object_id > 0xFFFF:
            raise RuleError("R4 pixel object catalog exceeds uint16_t ownership")
        catalog.append({**item, "id": object_id, "structureId": structure_id,
                        "supportStructureId": mapping.get(item["supportStructureId"], 0),
                        "layoutId": layout_id, "mapGroup": map_group,
                        "mapNumber": map_number})


def _validate_pixel_object_catalog(catalog: list[dict]) -> None:
    by_scope: defaultdict[tuple[int, int, int], list[dict]] = defaultdict(list)
    for obj in catalog:
        if not 1 <= obj["width"] <= 64 or not 1 <= obj["height"] <= 0xFF:
            raise RuleError(f"R4 pixel object {obj['id']} dimensions exceed generated ABI")
        if not 0 <= obj["componentCount"] <= 0xFF:
            raise RuleError(f"R4 pixel object {obj['id']} component count exceeds generated ABI")
        if not -0x8000 <= obj["supportOffsetQ16"] <= 0x7FFF:
            raise RuleError(f"R4 pixel object {obj['id']} support exceeds generated ABI")
        if not 0 <= obj["spriteDepthBiasMillionths"] <= 10000:
            raise RuleError(f"R4 pixel object {obj['id']} sprite bias exceeds generated ABI")
        if len(obj["maskRows"]) != obj["height"]:
            raise RuleError(f"R4 pixel object {obj['id']} mask height is inconsistent")
        if any(not 0 <= row < 1 << obj["width"] for row in obj["maskRows"]):
            raise RuleError(f"R4 pixel object {obj['id']} mask row exceeds uint64_t width")
        by_scope[(obj["layoutId"], obj["mapGroup"], obj["mapNumber"])].extend(obj["pixels"])
        for pixel in obj["pixels"]:
            for field in ("xQ32", "yQ32", "zQ32"):
                if not -0x8000 <= pixel[field] <= 0x7FFF:
                    raise RuleError(f"R4 pixel object {obj['id']} {field} exceeds int16_t")
            for field in ("sizeXQ32", "sizeYQ32", "sizeZQ32", "sourceLayer",
                          "sourceSubtile", "sourcePalette", "sourceColor", "sourceU",
                          "sourceV", "sourceX", "sourceY", "component"):
                if not 0 <= pixel[field] <= 0xFF:
                    raise RuleError(f"R4 pixel object {obj['id']} {field} exceeds uint8_t")
    # Connections translate source maps by whole cells. Test every modulo-eight
    # translation because chunk ownership can change even though object geometry cannot.
    validation_scopes = []
    for scope, scoped_pixels in by_scope.items():
        pixels = list(scoped_pixels)
        if scope[1:] != (0xFF, 0xFF):
            pixels.extend(by_scope.get((scope[0], 0xFF, 0xFF), ()))
        validation_scopes.append((scope, pixels))
    for scope, pixels in validation_scopes:
        for shift_y in range(8):
            for shift_x in range(8):
                chunk_counts: Counter[tuple[int, int]] = Counter()
                for pixel in pixels:
                    chunk = ((pixel["xQ32"] + shift_x * 32
                              + pixel["sizeXQ32"] // 2) // (8 * 32),
                             (pixel["zQ32"] + shift_y * 32
                              + pixel["sizeZQ32"] // 2) // (8 * 32))
                    chunk_counts[chunk] += 1
                    if chunk_counts[chunk] > 4096:
                        raise RuleError(
                            f"R4 pixel primitives exceed runtime chunk capacity at "
                            f"{scope + chunk} after modulo translation {shift_x},{shift_y}")


def _shape_geometry(class_name: str) -> tuple[str, str, str]:
    if class_name == "void" or class_name == "claim-only":
        return "hidden", class_name, "void"
    if class_name in ("water", "shallow-water", "waterfall", "current", "hot-spring"):
        return "water", class_name, "water"
    if class_name == "ledge":
        return "ledge", class_name, "rock"
    if class_name.startswith("stairs"):
        return "stairs", "stairs-n" if class_name == "stairs" else class_name, "rock"
    if class_name in ("cliff", "mound", "wall", "wall-volume"):
        archetype = "wall-volume" if class_name == "wall" else class_name
        return "cliff", archetype, "rock"
    if class_name in ("bridge", "deck", "rail", "support"):
        return "bridge", class_name, "wood"
    if class_name in UPRIGHT_PIXEL_CLASSES:
        return "flat", class_name, "ground"
    if class_name == "relief":
        return "flat", class_name, "ground"
    return "flat", class_name if class_name in ARCHETYPES else "ground", "ground"


def _evidence_flags(values: tuple[str, ...]) -> int:
    prefixes = (("rule:", 1 << 0), ("selector:", 1 << 1), ("tileset:", 1 << 2),
                ("metatile:", 1 << 3), ("behavior:", 1 << 4),
                ("animation:", 1 << 5), ("collision:", 1 << 6),
                ("elevation:", 1 << 7), ("layerType:", 1 << 8),
                ("pattern:", 1 << 9), ("pixels:", 1 << 10))
    return sum(flag for prefix, flag in prefixes if any(value.startswith(prefix) for value in values))


def _terrain_record(cell_offset: int, cell: dict, shape: TileShape,
                    structure: dict, map_group: int = 0xFF,
                    map_number: int = 0xFF) -> dict:
    geometry, archetype, terrain_class = _shape_geometry(shape.class_name)
    prop = shape.prop_ground.as_dict()
    return {
        "cellOffset": cell_offset, "expectedMetatile": cell["metatile"],
        "mapGroup": map_group, "mapNumber": map_number,
        "class": shape.class_name, "heightQ16": round(shape.height * 16),
        "artMode": shape.art_mode, "pool": shape.pool, "authored": shape.authored,
        "source": shape.source, "confidence": shape.confidence,
        "evidence": list(shape.evidence), "evidenceFlags": _evidence_flags(shape.evidence),
        "ambiguity": list(shape.ambiguity),
        "ambiguityFlags": 1 if shape.ambiguity else 0,
        "propGroundMode": prop["mode"], "propGroundMetatile": prop.get("metatile", 0xFFFF),
        "shape": geometry, "archetype": archetype, "terrainClass": terrain_class,
        "axis": "x", "cliffEdgeMask": 0, "cliffBaseMask": 0,
        "cliffTransitionMask": 0, "cliffCornerMask": 0,
        **structure,
    }


def _structure_fields(analysis: dict, mapping: dict[int, int], offset: int,
                      layout_width: int) -> dict:
    cell = analysis["cells"][offset]
    owner = mapping.get(cell["owner"], 0)
    region = mapping.get(cell["region"], 0)
    local_id = cell["owner"] or cell["region"]
    candidate = analysis["candidates"][local_id - 1] if local_id else None
    bbox = candidate["bbox"] if candidate else {"x": 0, "y": 0, "width": 0, "height": 0}
    x, y = offset % layout_width, offset // layout_width
    return {
        "claimOwner": owner, "regionId": region,
        "structureId": owner or region,
        "structureTemplateId": (owner if candidate and candidate["kind"] == "template" else 0),
        "structureX": bbox["x"], "structureY": bbox["y"],
        "structureWidth": bbox["width"], "structureHeight": bbox["height"],
        "structureLocalX": x - bbox["x"] if candidate else 0,
        "structureLocalY": y - bbox["y"] if candidate else 0,
        "structurePriority": candidate["priority"] if candidate else -32768,
        "ownerKind": candidate["kind"] if candidate else "none",
        "doorFold": cell["doorFold"], "voidKind": cell["voidKind"],
    }


def compile_data(root: Path) -> dict:
    root = Path(root)
    rules_root = root / "data/diorama"
    try:
        tilesets = load_tilesets(root)
        maps, layouts = load_world(root)
    except CatalogError as error:
        raise RuleError(str(error)) from error
    tileset_ids = {symbol: index for index, symbol in enumerate(sorted(tilesets), 1)}
    if len(tilesets) != 75 or len(layouts) != 441:
        raise RuleError("catalog cardinality changed: expected 75 tilesets and 441 layouts")
    behaviors = parse_behaviors(root / "include/constants/metatile_behaviors.h")
    behavior_names = {value: name for name, value in behaviors.items()}
    layouts_by_id = {layout["id"]: layout for layout in layouts}
    maps_by_symbol = {row["symbol"]: row for row in maps}
    maps_by_layout = defaultdict(list)
    for row in maps:
        maps_by_layout[row["layout"]].append(row)
    cells, behavior_counts, pin_counts = _load_cells(root, layouts, tilesets, behavior_names)
    event_cells = _events(root, maps, cells, layouts_by_id)
    pixel_summaries = _pixel_summaries(root, layouts, cells, tilesets)

    source = load_json(rules_root / "defaults.json")
    _unknown(source, {"schemaVersion", "kind", "default", "semanticPools", "profiles",
                      "behaviorRules", "contextualRules", "exactPatterns",
                      "mapDefault"}, "defaults")
    if source.get("kind") != "global":
        raise RuleError("defaults.kind: must be 'global'")
    pools_list = array(source.get("semanticPools"), "defaults.semanticPools")
    pools = []
    for index, raw in enumerate(pools_list):
        item = _object(raw, f"defaults.semanticPools[{index}]")
        _unknown(item, {"id", "description"}, f"defaults.semanticPools[{index}]")
        pool_id = _identifier(item.get("id"), f"defaults.semanticPools[{index}].id")
        if not isinstance(item.get("description"), str) or not item["description"].strip():
            raise RuleError(f"defaults.semanticPools[{index}].description: must be non-empty")
        pools.append({"id": pool_id, "description": item["description"]})
    pool_ids = [item["id"] for item in pools]
    if not pool_ids or len(set(pool_ids)) != len(pool_ids):
        raise RuleError("defaults.semanticPools: IDs must be non-empty and unique")
    pools.sort(key=lambda item: item["id"])
    profiles, profiles_by_id = _profiles(source.get("profiles"), "defaults.profiles", tilesets)
    default_action = _action(source.get("default"), "defaults.default", set(pool_ids), profiles_by_id)

    behavior_rules = []
    seen_behaviors = set()
    for index, raw in enumerate(array(source.get("behaviorRules"), "defaults.behaviorRules")):
        item_path = f"defaults.behaviorRules[{index}]"
        item = _object(raw, item_path)
        _unknown(item, {"behavior", "action", "allowedUnused"}, item_path)
        behavior = item.get("behavior")
        if behavior not in behaviors:
            raise RuleError(f"{item_path}.behavior: unknown behavior {behavior!r}")
        if behavior in seen_behaviors:
            raise RuleError(f"{item_path}.behavior: duplicate behavior rule")
        seen_behaviors.add(behavior)
        count = behavior_counts[behavior]
        allowance = _allowance(item.get("allowedUnused"), f"{item_path}.allowedUnused")
        if not count and allowance is None:
            raise RuleError(f"{item_path}: dead behavior rule has no placements; add structured allowedUnused")
        behavior_rules.append({"behavior": behavior, "behaviorId": behaviors[behavior],
                               "action": _action(item.get("action"), f"{item_path}.action",
                                                 set(pool_ids), profiles_by_id),
                               "placementCount": count, "allowedUnused": allowance})
    behavior_rules.sort(key=lambda item: item["behaviorId"])
    contextual_rules = _rule_list(source.get("contextualRules"), "defaults.contextualRules",
                                  tilesets, behaviors, set(pool_ids), profiles_by_id)
    for rule in contextual_rules:
        rule["placementCount"] = _rule_placements(rule, layouts, maps_by_layout, cells, event_cells)
        if not rule["placementCount"] and rule["allowedUnused"] is None:
            raise RuleError(f"contextual rule {rule['id']}: dead rule has no placements; add structured allowedUnused")
    patterns = _patterns(source.get("exactPatterns"), "defaults.exactPatterns", tilesets,
                         set(pool_ids), profiles_by_id)
    for pattern in patterns:
        pattern["placementCount"] = _pattern_placements(pattern, layouts, cells)
        if not pattern["placementCount"] and pattern["allowedNoPlacements"] is None:
            raise RuleError(f"exact pattern {pattern['id']}: no placements; add structured allowedNoPlacements")
    pins = _parse_pin_files(root, tilesets, set(pool_ids), profiles_by_id, pin_counts)

    map_default = _object(source.get("mapDefault"), "defaults.mapDefault")
    _unknown(map_default, {"supported", "reason", "groundPolicy", "camera"},
             "defaults.mapDefault")
    map_documents = {}
    for path in sorted((rules_root / "maps").glob("*.json")):
        item = load_json(path)
        symbol = item.get("map")
        if symbol not in maps_by_symbol:
            raise RuleError(f"{path}.map: unknown map {symbol!r}")
        if symbol in map_documents:
            raise RuleError(f"{path}.map: duplicate map {symbol!r}")
        map_documents[symbol] = (path, item)

    if not isinstance(map_default.get("supported"), bool):
        raise RuleError("defaults.mapDefault.supported: must be a boolean")
    if map_default["supported"] and "reason" in map_default:
        raise RuleError("defaults.mapDefault.reason: supported defaults cannot have a reason")
    if not map_default["supported"] and (not isinstance(map_default.get("reason"), str)
                                         or not map_default["reason"].strip()):
        raise RuleError("defaults.mapDefault: unsupported default requires a non-empty reason")
    default_ground = _ground_policy(map_default.get("groundPolicy"), "defaults.mapDefault.groundPolicy")
    default_camera_value = _object(map_default.get("camera"), "defaults.mapDefault.camera")
    if default_camera_value.get("profile") == "automatic":
        _unknown(default_camera_value, {"profile"}, "defaults.mapDefault.camera")
        default_camera = None
    else:
        default_camera = parse_camera(default_camera_value, "defaults.mapDefault.camera")
    explicit_maps = {}
    local_patterns_by_map = {}
    for symbol, (path, item) in sorted(map_documents.items()):
        _unknown(item, {"schemaVersion", "kind", "map", "layout", "status", "camera",
                        "groundPolicy", "contextualRules", "exactPatterns"}, str(path))
        if item.get("kind") != "map":
            raise RuleError(f"{path}.kind: must be 'map'")
        if symbol not in maps_by_symbol or symbol in explicit_maps:
            raise RuleError(f"{path}.map: unknown or duplicate map {symbol!r}")
        map_row = maps_by_symbol[symbol]
        if item.get("layout") != map_row["layout"]:
            raise RuleError(f"{path}.layout: expected {map_row['layout']}")
        status = _object(item.get("status"), f"{path}.status")
        _unknown(status, {"supported", "reason"}, f"{path}.status")
        if not isinstance(status.get("supported"), bool):
            raise RuleError(f"{path}.status.supported: must be a boolean")
        if status["supported"] and "reason" in status:
            raise RuleError(f"{path}.status.reason: supported maps cannot have an unsupported reason")
        if not status["supported"] and (not isinstance(status.get("reason"), str) or not status["reason"].strip()):
            raise RuleError(f"{path}.status.reason: unsupported maps require a non-empty reason")
        local_rules = _rule_list(item.get("contextualRules", []), f"{path}.contextualRules",
                                 tilesets, behaviors, set(pool_ids), profiles_by_id)
        for rule in local_rules:
            rule["placementCount"] = _rule_placements(rule, [layouts_by_id[map_row["layout"]]],
                                                       {map_row["layout"]: [map_row]}, cells, event_cells)
            if not rule["placementCount"] and rule["allowedUnused"] is None:
                raise RuleError(f"{path}: contextual rule {rule['id']} is dead")
        local_patterns = _patterns(item.get("exactPatterns", []), f"{path}.exactPatterns",
                                   tilesets, set(pool_ids), profiles_by_id)
        for pattern in local_patterns:
            pattern["placementCount"] = _pattern_placements(pattern,
                                                             [layouts_by_id[map_row["layout"]]], cells)
            if not pattern["placementCount"] and pattern["allowedNoPlacements"] is None:
                raise RuleError(f"{path}: exact pattern {pattern['id']} has no placements")
        explicit_maps[symbol] = {"status": dict(status),
                                   "camera": parse_camera(item.get("camera"), f"{path}.camera"),
                                   "groundPolicy": _ground_policy(item.get("groundPolicy", {"mode": "automatic"}),
                                                                  f"{path}.groundPolicy"),
                                   "contextualRules": local_rules, "exactPatterns": local_patterns}
        local_patterns_by_map[symbol] = local_patterns

    logical_ids = [(item["id"], "semanticPools") for item in pools]
    logical_ids.extend((item["id"], "profiles") for item in profiles)
    logical_ids.extend((item["id"], "contextualRules") for item in contextual_rules)
    logical_ids.extend((item["id"], "exactPatterns") for item in patterns)
    for symbol, configured in explicit_maps.items():
        logical_ids.extend((item["id"], f"{symbol}.contextualRules")
                           for item in configured["contextualRules"])
        logical_ids.extend((item["id"], f"{symbol}.exactPatterns")
                           for item in configured["exactPatterns"])
    _validate_logical_ids(logical_ids)
    _validate_pattern_claim_overlaps(patterns, maps, layouts_by_id, cells,
                                     local_patterns_by_map)

    tileset_catalog = [{"id": tileset_ids[symbol], "symbol": symbol, "role": info.role,
                        "metatileCount": info.metatile_count,
                        "terrainClass": "ground"}
                       for symbol, info in sorted(tilesets.items(), key=lambda item: tileset_ids[item[0]])]
    layout_catalog = []
    structure_cell_offset = 0
    for layout in layouts:
        layout_catalog.append({"id": layout["numericId"], "symbol": layout["id"],
                        "primaryTilesetId": tileset_ids.get(layout["primary_tileset"], 0),
                        "secondaryTilesetId": tileset_ids.get(layout["secondary_tileset"], 0),
                         "width": layout["width"], "height": layout["height"],
                        "structureCellOffset": structure_cell_offset,
                        "terrainRecordOffset": 0,
                        "terrainRecordCount": 0, "terrainRecords": []})
        structure_cell_offset += layout["width"] * layout["height"]
    map_catalog = []
    for row in sorted(maps, key=lambda item: (item["group"], item["number"])):
        configured = explicit_maps.get(row["symbol"])
        status = configured["status"] if configured else {
            "supported": map_default["supported"], **({"reason": map_default["reason"]}
            if not map_default["supported"] else {})}
        if configured:
            camera = configured["camera"]
        elif default_camera is not None:
            camera = default_camera
        else:
            camera = parse_camera({"profile": "interior" if row["mapType"] in
                                   ("MAP_TYPE_INDOOR", "MAP_TYPE_SECRET_BASE") else "exterior"},
                                  f"{row['symbol']}.automaticCamera")
        ground = configured["groundPolicy"] if configured else default_ground
        map_catalog.append({"symbol": row["symbol"], "group": row["group"], "number": row["number"],
                            "layoutId": layouts_by_id[row["layout"]]["numericId"], "mapType": row["mapType"],
                            "supported": status["supported"], "unsupportedReason": status.get("reason"),
                             "camera": {"profile": camera[0], "pitchRadians": camera[1],
                                        "focalLength": camera[2]}, "groundPolicy": ground,
                            "contextualRules": configured["contextualRules"] if configured else [],
                            "exactPatterns": configured["exactPatterns"] if configured else []})
    map_catalog_by_symbol = {row["symbol"]: row for row in map_catalog}
    ambiguity_counts = Counter()
    ambiguity_flags = defaultdict(int)
    evidence_details = set()
    source_names = set()
    terrain_offset = 0
    structure_catalog = []
    pixel_object_catalog = []
    global_classifier = TileShapeClassifier(
        contextual_rules=contextual_rules, tileset_pins=pins,
        behavior_rules=behavior_rules, default_action=default_action)
    for layout, catalog_row in zip(layouts, layout_catalog):
        layout_cells = cells[layout["id"]]
        records = []
        if layout_cells:
            wildcard = global_classifier.classify_layout(
                layout_cells, layout["width"], layout["height"])
            wildcard = tuple(_with_animation_provenance(shape, cell)
                             for shape, cell in zip(wildcard, layout_cells))
            wildcard_structures = analyze_layout(
                layout, layout_cells, wildcard, patterns, pixel_summaries[layout["id"]])
            wildcard = _apply_derived_voids(wildcard, wildcard_structures)
            wildcard_mapping = _register_candidates(
                wildcard_structures, layout["numericId"], 0xFF, 0xFF, structure_catalog)
            _register_pixel_objects(extract_layout(
                wildcard_structures, layout_cells, wildcard,
                pixel_summaries[layout["id"]], layout["width"], layout["height"]),
                wildcard_mapping, layout["numericId"], 0xFF, 0xFF, pixel_object_catalog)
            for offset, (cell, shape) in enumerate(zip(layout_cells, wildcard)):
                records.append(_terrain_record(
                    offset, cell, shape,
                    _structure_fields(wildcard_structures, wildcard_mapping, offset,
                                      layout["width"])))
                if not shape.authored and shape.confidence == 0.0:
                    ambiguity_counts[shape.ambiguity] += 1
                    ambiguity_flags[shape.ambiguity] |= _evidence_flags(shape.evidence)
            for map_row in sorted(maps_by_layout.get(layout["id"], []),
                                  key=lambda item: (item["group"], item["number"])):
                configured = explicit_maps.get(map_row["symbol"])
                local_rules = configured["contextualRules"] if configured else []
                classifier = TileShapeClassifier(
                    contextual_rules=tuple(local_rules) + tuple(contextual_rules),
                    tileset_pins=pins, behavior_rules=behavior_rules,
                    default_action=default_action)
                coordinate_events = {
                    (x, y): values for (symbol, x, y), values in event_cells.items()
                    if symbol == map_row["symbol"]
                }
                exact = classifier.classify_layout(
                    layout_cells, layout["width"], layout["height"],
                    map_type=map_row["mapType"], events=coordinate_events)
                exact_patterns = patterns + (configured["exactPatterns"] if configured else [])
                exact = tuple(_with_animation_provenance(shape, cell)
                              for shape, cell in zip(exact, layout_cells))
                map_events = {offset: values for (symbol, x, y), values in event_cells.items()
                              if symbol == map_row["symbol"]
                              for offset in (y * layout["width"] + x,)}
                doors = frozenset(offset for offset, values in map_events.items()
                                  if any(value["class"] == "door" for value in values))
                sign_events = ({offset: {"class": "signpost", "pool": "prop",
                                         "source": "event:background-sign",
                                         "evidence": ["event:background-sign"]}
                                for offset, values in map_events.items()
                                if any(value["kind"] == "background"
                                       and value["class"] == "sign" for value in values)}
                               if map_row["symbol"] in ("MAP_LITTLEROOT_TOWN",
                                                        "MAP_OLDALE_TOWN") else {})
                exact_structures = analyze_layout(
                    layout, layout_cells, exact, exact_patterns,
                    pixel_summaries[layout["id"]], doors, sign_events)
                exact = _apply_derived_voids(exact, exact_structures)
                scope = map_catalog_by_symbol[map_row["symbol"]]
                changed_structure = {
                    offset for offset in range(len(layout_cells))
                    if _structure_semantic(exact_structures, offset)
                    != _structure_semantic(wildcard_structures, offset)
                }
                needed = {local_id for offset in changed_structure
                          for local_id in (exact_structures["cells"][offset]["owner"],
                                           exact_structures["cells"][offset]["region"])
                          if local_id is not None}
                exact_mapping = _register_candidates(
                    exact_structures, layout["numericId"], scope["group"], scope["number"],
                    structure_catalog, needed)
                _register_pixel_objects(extract_layout(
                    exact_structures, layout_cells, exact,
                    pixel_summaries[layout["id"]], layout["width"], layout["height"]),
                    exact_mapping, layout["numericId"], scope["group"], scope["number"],
                    pixel_object_catalog)
                for offset, (cell, shape, base) in enumerate(zip(layout_cells, exact, wildcard)):
                    if shape.as_dict() == base.as_dict() and offset not in changed_structure:
                        continue
                    structure = (_structure_fields(exact_structures, exact_mapping, offset,
                                                   layout["width"])
                                 if offset in changed_structure else
                                 _structure_fields(wildcard_structures, wildcard_mapping, offset,
                                                   layout["width"]))
                    records.append(_terrain_record(offset, cell, shape, structure,
                                                   scope["group"], scope["number"]))
                    if not shape.authored and shape.confidence == 0.0:
                        ambiguity_counts[shape.ambiguity] += 1
                        ambiguity_flags[shape.ambiguity] |= _evidence_flags(shape.evidence)
        records.sort(key=lambda row: (row["cellOffset"],
                                      row["mapGroup"] == 0xFF,
                                      row["mapGroup"], row["mapNumber"]))
        for record in records:
            evidence_details.add(tuple(record["evidence"]))
            source_names.add(record["source"])
        catalog_row["terrainRecordOffset"] = terrain_offset
        catalog_row["terrainRecordCount"] = len(records)
        catalog_row["terrainRecords"] = records
        terrain_offset += len(records)
    _validate_pixel_object_catalog(pixel_object_catalog)
    evidence_catalog = [{"id": index, "details": list(details)}
                        for index, details in enumerate(sorted(evidence_details), 1)]
    ambiguity_catalog = [
        {"id": index, "details": list(details), "flags": ambiguity_flags[details],
         "placementCount": ambiguity_counts[details]}
        for index, details in enumerate(sorted(ambiguity_counts), 1)
    ]
    source_catalog = [{"id": index, "name": name}
                      for index, name in enumerate(sorted(source_names), 1)]
    canonical = {"schemaVersion": SCHEMA_VERSION, "tilesets": tileset_catalog,
                  "layouts": layout_catalog, "pools": pools, "profiles": profiles,
                   "default": default_action, "behaviorRules": behavior_rules,
                   "tilesetPins": pins, "contextualRules": contextual_rules,
                   "exactPatterns": patterns, "maps": map_catalog,
                   "structureCandidates": structure_catalog,
                   "pixelObjects": pixel_object_catalog,
                   "evidenceDetails": evidence_catalog, "ambiguities": ambiguity_catalog,
                   "classifierSources": source_catalog}
    encoded = json.dumps(canonical, sort_keys=True, separators=(",", ":"), ensure_ascii=True).encode("ascii")
    digest = hashlib.sha256(encoded).hexdigest()
    generation = int(digest[:8], 16) or 1
    return {**canonical, "sha256": digest, "generation": generation,
            "templates": [], "roof_profiles": []}


def _c_string(value: str | None) -> str:
    if value is None:
        return "NULL"
    return json.dumps(value, ensure_ascii=True)


def render_header() -> str:
    return """/* Auto-generated by tools/diorama_rules/compile_rules.py. Do not edit. */
#ifndef GUARD_DIORAMA_RULES_GENERATED_H
#define GUARD_DIORAMA_RULES_GENERATED_H
#include <stddef.h>
#include <stdint.h>

struct DioramaGeneratedTilesetV2 { uint8_t id, role, terrainClass; uint16_t metatileCount; const char *symbol; };
struct DioramaGeneratedLayoutV2 { uint16_t id, width, height; uint8_t primaryTilesetId, secondaryTilesetId; uint32_t terrainRecordOffset, terrainRecordCount, structureCellOffset; const char *symbol; };
struct DioramaGeneratedTerrainV2 { uint32_t cellOffset; uint16_t expectedMetatile, classId, sourceId, evidenceDetailsId, ambiguityDetailsId, propGroundMetatile, evidenceFlags, ambiguityFlags; int16_t heightQ16; uint8_t mapGroup, mapNumber, shape, archetype, terrainClass, artMode, pool, authored, sourceKind, propGroundMode, axis, cliffEdgeMask, cliffBaseMask, cliffTransitionMask, cliffCornerMask; float confidence; };
struct DioramaGeneratedClassifierSourceV2 { uint16_t id; const char *name; };
struct DioramaGeneratedEvidenceV2 { uint16_t id; const char *details; };
struct DioramaGeneratedAmbiguityV2 { uint16_t id, flags; uint32_t placementCount; const char *details; };
struct DioramaGeneratedMapV2 { uint8_t group, number, supported, cameraProfile, mapType, groundMode; uint16_t layoutId, groundMetatile; float pitchRadians, focalLength; const char *symbol, *unsupportedReason; };
struct DioramaGeneratedPoolV2 { uint16_t id; const char *logicalId, *description; };
struct DioramaGeneratedProfileV2 { uint16_t id; uint8_t archetypeId; uint32_t maskOffset, sampleOffset; uint16_t maskCount, sampleCount; const char *logicalId; };
struct DioramaGeneratedMaskV2 { uint16_t profileId; uint8_t kind, width, height; uint32_t rowOffset; uint16_t rowCount; };
struct DioramaGeneratedSampleV2 { uint8_t tilesetId, layer; uint16_t metatile; };
struct DioramaGeneratedStructureV2 { uint16_t id, layoutId; uint8_t mapGroup, mapNumber, kind, pool, classId, claimOnly; int16_t priority, x, y; uint8_t width, height; uint32_t cellOffset, cellCount, visiblePixels, transparentPixels, blackPixels, pixelComponents; const char *owner, *source, *evidence; };
struct DioramaGeneratedStructureCellV2 { uint16_t claimOwner, regionId; uint8_t doorFold, voidKind; };
struct DioramaGeneratedStructureOverrideV2 { uint16_t layoutId, expectedMetatile; uint32_t cellOffset; uint8_t mapGroup, mapNumber; struct DioramaGeneratedStructureCellV2 cell; };
struct DioramaGeneratedPixelObjectV2 { uint16_t id, structureId, supportStructureId, layoutId, groundMetatile, spriteDepthBiasMillionths; uint8_t mapGroup, mapNumber, kind, pool, classId, componentCount, width, height, groundMode; int16_t supportOffsetQ16; uint32_t pixelOffset, pixelCount, maskOffset, maskCount; };
struct DioramaGeneratedPixelV2 { uint32_t sourceCellOffset; uint16_t expectedMetatile, expectedTileEntry, sourceTile; int16_t xQ32, yQ32, zQ32; uint8_t sizeXQ32, sizeYQ32, sizeZQ32, sourceLayer, sourceSubtile, sourcePalette, sourceColor, sourceU, sourceV, sourceX, sourceY, component; };

extern const char gDioramaRulesSha256[65];
extern const uint32_t gDioramaRulesGeneration;
extern const struct DioramaGeneratedTilesetV2 gDioramaTilesetsV2[];
extern const size_t gDioramaTilesetV2Count;
extern const struct DioramaGeneratedLayoutV2 gDioramaLayoutsV2[];
extern const size_t gDioramaLayoutV2Count;
extern const struct DioramaGeneratedTerrainV2 gDioramaTerrainV2[];
extern const size_t gDioramaTerrainV2Count;
extern const struct DioramaGeneratedClassifierSourceV2 gDioramaClassifierSourcesV2[];
extern const size_t gDioramaClassifierSourceV2Count;
extern const struct DioramaGeneratedEvidenceV2 gDioramaEvidenceDetailsV2[];
extern const size_t gDioramaEvidenceDetailV2Count;
extern const struct DioramaGeneratedAmbiguityV2 gDioramaAmbiguitiesV2[];
extern const size_t gDioramaAmbiguityV2Count;
extern const struct DioramaGeneratedMapV2 gDioramaMapsV2[];
extern const size_t gDioramaMapV2Count;
extern const struct DioramaGeneratedPoolV2 gDioramaPoolsV2[];
extern const size_t gDioramaPoolV2Count;
extern const struct DioramaGeneratedProfileV2 gDioramaProfilesV2[];
extern const size_t gDioramaProfileV2Count;
extern const struct DioramaGeneratedMaskV2 gDioramaMasksV2[];
extern const size_t gDioramaMaskV2Count;
extern const uint64_t gDioramaMaskRowsV2[];
extern const size_t gDioramaMaskRowV2Count;
extern const struct DioramaGeneratedSampleV2 gDioramaSamplesV2[];
extern const size_t gDioramaSampleV2Count;
extern const struct DioramaGeneratedStructureV2 gDioramaStructuresV2[];
extern const size_t gDioramaStructureV2Count;
extern const uint32_t gDioramaStructureCellsV2[];
extern const size_t gDioramaStructureCellV2Count;
extern const struct DioramaGeneratedStructureCellV2 gDioramaStructureOwnersV2[];
extern const size_t gDioramaStructureOwnerV2Count;
extern const struct DioramaGeneratedStructureOverrideV2 gDioramaStructureOverridesV2[];
extern const size_t gDioramaStructureOverrideV2Count;
extern const struct DioramaGeneratedPixelObjectV2 gDioramaPixelObjectsV2[];
extern const size_t gDioramaPixelObjectV2Count;
extern const struct DioramaGeneratedPixelV2 gDioramaPixelsV2[];
extern const size_t gDioramaPixelV2Count;
extern const uint64_t gDioramaPixelMaskRowsV2[];
extern const size_t gDioramaPixelMaskRowV2Count;
#endif
"""


def render_c(data: dict) -> str:
    lines = ["/* Auto-generated by tools/diorama_rules/compile_rules.py. Do not edit. */",
             "#ifdef ENABLE_DIORAMA", '#include "diorama/rules.generated.h"', "",
             f'const char gDioramaRulesSha256[65] = "{data["sha256"]}";',
             f"const uint32_t gDioramaRulesGeneration = UINT32_C(0x{data['generation']:08X});", ""]
    def table(c_type: str, name: str, count_name: str, rows: list[str]) -> None:
        lines.append(f"const {c_type} {name}[] = {{")
        empty = "{ 0 }" if c_type.startswith("struct ") else "0"
        lines.extend(f"    {row}," for row in (rows or [empty]))
        count = f"sizeof({name}) / sizeof({name}[0])" if rows else "0"
        lines.extend(("};", f"const size_t {count_name} = {count};", ""))

    tileset_ids = {row["symbol"]: row["id"] for row in data["tilesets"]}
    archetypes = {name: index for index, name in enumerate(ARCHETYPES, 1)}
    pools = {row["id"]: index for index, row in enumerate(data["pools"], 1)}
    profiles = {row["id"]: index for index, row in enumerate(data["profiles"], 1)}
    map_types = {name: index for index, name in enumerate(MAP_TYPES)}
    material_layers = {name: index for index, name in enumerate(MATERIAL_LAYERS, 1)}
    terrain_classes = {name: index + 1 for index, name in enumerate(TERRAIN_CLASSES)}
    class_ids = {name: index for index, name in enumerate(CLASS_NAMES, 1)}
    art_modes = {name: index for index, name in enumerate(ART_MODE_NAMES)}
    source_ids = {row["name"]: row["id"] for row in data["classifierSources"]}
    evidence_ids = {tuple(row["details"]): row["id"] for row in data["evidenceDetails"]}
    ambiguity_ids = {tuple(row["details"]): row["id"] for row in data["ambiguities"]}

    table("struct DioramaGeneratedTilesetV2", "gDioramaTilesetsV2", "gDioramaTilesetV2Count",
          [f"{{ {row['id']}, {1 if row['role'] == 'secondary' else 0}, "
           f"{terrain_classes[row['terrainClass']]}, {row['metatileCount']}, {_c_string(row['symbol'])} }}"
           for row in data["tilesets"]])
    table("struct DioramaGeneratedLayoutV2", "gDioramaLayoutsV2", "gDioramaLayoutV2Count",
          [f"{{ {row['id']}, {row['width']}, {row['height']}, {row['primaryTilesetId']}, "
           f"{row['secondaryTilesetId']}, {row['terrainRecordOffset']}, "
           f"{row['terrainRecordCount']}, {row['structureCellOffset']}, "
           f"{_c_string(row['symbol'])} }}" for row in data["layouts"]])
    shapes = {name: index for index, name in enumerate(
        ("flat", "extruded", "cliff", "ledge", "stairs", "water", "bridge",
         "billboard", "cutout", "roof", "building-part", "hidden"))}
    axes = {"x": 0, "z": 1, "cross": 2}
    owner_kinds = {"none": 0, "template": 1, "authored-special": 2,
                   "prop-candidate": 3, "generic-volume": 4, "region": 5}
    void_kinds = {"none": 0, "transparent": 1, "black": 2}
    def source_kind(name: str) -> int:
        if name.startswith("pin:"):
            return 1
        if name.startswith("behavior:"):
            return 3
        if name.startswith("pattern:"):
            return 7
        if name.startswith("context:"):
            return 8
        if name.startswith("animation:"):
            return 10
        return 6
    terrain_rows = []
    for layout in data["layouts"]:
        for row in layout["terrainRecords"]:
            terrain_rows.append(
                f"{{ {row['cellOffset']}, {row['expectedMetatile']}, {class_ids[row['class']]}, "
                f"{source_ids[row['source']]}, {evidence_ids[tuple(row['evidence'])]}, "
                f"{ambiguity_ids.get(tuple(row['ambiguity']), 0)}, {row['propGroundMetatile']}, "
                f"{row['evidenceFlags']}, {row['ambiguityFlags']}, {row['heightQ16']}, "
                f"{row['mapGroup']}, {row['mapNumber']}, {shapes[row['shape']]}, "
                f"{archetypes[row['archetype']]}, {terrain_classes[row['terrainClass']]}, "
                f"{art_modes[row['artMode']]}, {pools[row['pool']]}, {int(row['authored'])}, "
                f"{source_kind(row['source'])}, {1 if row['propGroundMode'] == 'manual' else 0}, "
                f"{axes[row['axis']]}, "
                f"{row['cliffEdgeMask']}, "
                f"{row['cliffBaseMask']}, {row['cliffTransitionMask']}, "
                f"{row['cliffCornerMask']}, "
                f"{row['confidence']:.6f}f }}")
    table("struct DioramaGeneratedTerrainV2", "gDioramaTerrainV2", "gDioramaTerrainV2Count",
          terrain_rows)
    table("struct DioramaGeneratedClassifierSourceV2", "gDioramaClassifierSourcesV2",
          "gDioramaClassifierSourceV2Count",
          [f"{{ {row['id']}, {_c_string(row['name'])} }}" for row in data["classifierSources"]])
    table("struct DioramaGeneratedEvidenceV2", "gDioramaEvidenceDetailsV2",
          "gDioramaEvidenceDetailV2Count",
          [f"{{ {row['id']}, {_c_string('|'.join(row['details']))} }}"
           for row in data["evidenceDetails"]])
    table("struct DioramaGeneratedAmbiguityV2", "gDioramaAmbiguitiesV2",
          "gDioramaAmbiguityV2Count",
          [f"{{ {row['id']}, {row['flags']}, {row['placementCount']}, "
           f"{_c_string('|'.join(row['details']))} }}" for row in data["ambiguities"]])
    table("struct DioramaGeneratedMapV2", "gDioramaMapsV2", "gDioramaMapV2Count",
          [f"{{ {row['group']}, {row['number']}, {int(row['supported'])}, "
            f"{1 if row['camera']['profile'] == 'interior' else 0}, {map_types[row['mapType']]}, "
            f"{1 if row['groundPolicy']['mode'] == 'manual' else 0}, {row['layoutId']}, "
            f"{row['groundPolicy'].get('metatile', 0xFFFF)}, "
            f"{row['camera']['pitchRadians']:.6f}f, {row['camera']['focalLength']:.6f}f, "
            f"{_c_string(row['symbol'])}, {_c_string(row['unsupportedReason'])} }}" for row in data["maps"]])

    table("struct DioramaGeneratedPoolV2", "gDioramaPoolsV2", "gDioramaPoolV2Count",
          [f"{{ {pools[row['id']]}, {_c_string(row['id'])}, {_c_string(row['description'])} }}"
           for row in data["pools"]])
    masks, mask_rows, samples, profile_rows = [], [], [], []
    mask_kinds = {"occupancy": 1, "material": 2, "claim": 3}
    for profile in data["profiles"]:
        mask_offset, sample_offset = len(masks), len(samples)
        for name, mask in profile["masks"].items():
            row_offset = len(mask_rows)
            mask_rows.extend(int(value, 16) for value in mask["rows"])
            masks.append(f"{{ {profiles[profile['id']]}, {mask_kinds[name]}, {mask['width']}, "
                         f"{mask['height']}, {row_offset}, {len(mask['rows'])} }}")
        for sample in profile["samples"]:
            samples.append(f"{{ {tileset_ids[sample['tileset']]}, {material_layers[sample['layer']]}, "
                           f"{sample['metatile']} }}")
        profile_rows.append(f"{{ {profiles[profile['id']]}, {archetypes[profile['archetype']]}, "
                            f"{mask_offset}, {sample_offset}, {len(profile['masks'])}, "
                            f"{len(profile['samples'])}, {_c_string(profile['id'])} }}")
    table("struct DioramaGeneratedProfileV2", "gDioramaProfilesV2", "gDioramaProfileV2Count", profile_rows)
    table("struct DioramaGeneratedMaskV2", "gDioramaMasksV2", "gDioramaMaskV2Count", masks)
    table("uint64_t", "gDioramaMaskRowsV2", "gDioramaMaskRowV2Count",
          [f"UINT64_C(0x{value:X})" for value in mask_rows])
    table("struct DioramaGeneratedSampleV2", "gDioramaSamplesV2", "gDioramaSampleV2Count", samples)
    structure_cells, structure_rows = [], []
    for row in data["structureCandidates"]:
        cell_offset = len(structure_cells)
        structure_cells.extend(row["cells"])
        pixels = row["pixels"]
        bbox = row["bbox"]
        structure_rows.append(
            f"{{ {row['id']}, {row['layoutId']}, {row['mapGroup']}, {row['mapNumber']}, "
            f"{owner_kinds[row['kind']]}, {pools[row['pool']]}, {class_ids[row['class']]}, "
            f"{int(row['claimOnly'])}, {row['priority']}, {bbox['x']}, {bbox['y']}, "
            f"{bbox['width']}, {bbox['height']}, {cell_offset}, {len(row['cells'])}, "
            f"{pixels['visible']}, {pixels['transparent']}, {pixels['black']}, "
            f"{pixels['components']}, {_c_string(row['owner'])}, {_c_string(row['source'])}, "
            f"{_c_string('|'.join(row['evidence']))} }}")
    table("struct DioramaGeneratedStructureV2", "gDioramaStructuresV2",
          "gDioramaStructureV2Count", structure_rows)
    packed_structure_cells = [", ".join(f"UINT32_C({value})" for value in
                              structure_cells[index:index + 16])
                              for index in range(0, len(structure_cells), 16)]
    table("uint32_t", "gDioramaStructureCellsV2", "gDioramaStructureCellV2Count",
          packed_structure_cells)
    owner_rows, override_rows = [], []
    for layout in data["layouts"]:
        by_offset = {}
        for row in layout["terrainRecords"]:
            if row["mapGroup"] == 0xFF:
                by_offset[row["cellOffset"]] = row
            else:
                override_rows.append(
                    f"{{ {layout['id']}, {row['expectedMetatile']}, {row['cellOffset']}, "
                    f"{row['mapGroup']}, {row['mapNumber']}, {{ {row['claimOwner']}, "
                    f"{row['regionId']}, {int(row['doorFold'])}, {void_kinds[row['voidKind']]} }} }}")
        if len(by_offset) != layout["width"] * layout["height"]:
            raise RuleError(f"{layout['symbol']}: structure ownership is not layout-complete")
        owner_rows.extend(
            f"{{ {by_offset[offset]['claimOwner']}, {by_offset[offset]['regionId']}, "
            f"{int(by_offset[offset]['doorFold'])}, {void_kinds[by_offset[offset]['voidKind']]} }}"
            for offset in range(layout["width"] * layout["height"]))
    packed_owner_rows = [", ".join(owner_rows[index:index + 16])
                         for index in range(0, len(owner_rows), 16)]
    table("struct DioramaGeneratedStructureCellV2", "gDioramaStructureOwnersV2",
          "gDioramaStructureOwnerV2Count", packed_owner_rows)
    table("struct DioramaGeneratedStructureOverrideV2", "gDioramaStructureOverridesV2",
          "gDioramaStructureOverrideV2Count", override_rows)
    pixel_rows, object_rows, pixel_mask_rows = [], [], []
    pixel_kinds = {"cutout": 1, "relief": 2}
    pixel_ground_modes = {"replacement": 1, "source-base": 2}
    for row in data["pixelObjects"]:
        pixel_offset = len(pixel_rows)
        mask_offset = len(pixel_mask_rows)
        pixel_mask_rows.extend(row["maskRows"])
        for pixel in row["pixels"]:
            pixel_rows.append(
                f"{{ {pixel['sourceCellOffset']}, {pixel['expectedMetatile']}, "
                f"{pixel['expectedTileEntry']}, {pixel['sourceTile']}, "
                f"{pixel['xQ32']}, {pixel['yQ32']}, {pixel['zQ32']}, "
                f"{pixel['sizeXQ32']}, {pixel['sizeYQ32']}, {pixel['sizeZQ32']}, "
                f"{pixel['sourceLayer']}, {pixel['sourceSubtile']}, "
                f"{pixel['sourcePalette']}, {pixel['sourceColor']}, "
                f"{pixel['sourceU']}, {pixel['sourceV']}, {pixel['sourceX']}, "
                f"{pixel['sourceY']}, {pixel['component']} }}")
        object_rows.append(
            f"{{ {row['id']}, {row['structureId']}, {row['supportStructureId']}, {row['layoutId']}, "
            f"{row['groundMetatile']}, {row['spriteDepthBiasMillionths']}, "
            f"{row['mapGroup']}, {row['mapNumber']}, "
            f"{pixel_kinds[row['kind']]}, {pools[row['pool']]}, {class_ids[row['class']]}, "
            f"{row['componentCount']}, {row['width']}, {row['height']}, "
            f"{pixel_ground_modes[row['groundMode']]}, "
            f"{row['supportOffsetQ16']}, {pixel_offset}, {len(row['pixels'])}, "
            f"{mask_offset}, {len(row['maskRows'])} }}")
    table("struct DioramaGeneratedPixelObjectV2", "gDioramaPixelObjectsV2",
          "gDioramaPixelObjectV2Count", object_rows)
    table("struct DioramaGeneratedPixelV2", "gDioramaPixelsV2",
          "gDioramaPixelV2Count", pixel_rows)
    table("uint64_t", "gDioramaPixelMaskRowsV2", "gDioramaPixelMaskRowV2Count",
          [f"UINT64_C(0x{value:X})" for value in pixel_mask_rows])
    lines.extend(("#endif", ""))
    return "\n".join(lines)


def write_or_check(path: Path, content: str, check: bool) -> None:
    if check:
        if not path.exists() or path.read_text(encoding="utf-8") != content:
            raise RuleError(f"{path}: generated file is stale")
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
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
    except (RuleError, ClassificationError, OSError, KeyError, IndexError, struct.error) as error:
        print(f"diorama rules: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
