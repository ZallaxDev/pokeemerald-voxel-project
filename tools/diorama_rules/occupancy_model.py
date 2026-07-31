#!/usr/bin/env python3
"""Deterministic, language-neutral G4 span occupancy and shell reference."""

from __future__ import annotations

import argparse
import json
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable

UNITS_PER_CELL = 16
MODEL_VERSION = 3
FACE_TOP, FACE_BOTTOM, FACE_NORTH, FACE_EAST, FACE_SOUTH, FACE_WEST = range(6)


@dataclass(frozen=True, order=True)
class Span:
    x: int
    z: int
    y_min: int
    y_max: int
    source_x: int
    source_y: int
    materials: tuple[int, int, int, int, int, int]
    flags: int = 0


@dataclass(frozen=True, order=True)
class Face:
    axis: int
    sign: int
    plane: int
    source_x: int
    source_y: int
    material: int
    flags: int
    v_min: int
    v_max: int
    u_min: int
    u_max: int


def canonicalize(spans: Iterable[Span]) -> list[Span]:
    result: list[Span] = []
    for span in sorted(spans):
        if span.y_min >= span.y_max:
            raise ValueError("occupancy spans must be non-empty half-open intervals")
        if result and (result[-1].x, result[-1].z) == (span.x, span.z) \
                and span.y_min <= result[-1].y_max:
            previous = result[-1]
            if (previous.source_x, previous.source_y, previous.materials, previous.flags) != \
                    (span.source_x, span.source_y, span.materials, span.flags):
                raise ValueError("overlapping spans require one resolved provenance")
            result[-1] = Span(previous.x, previous.z, previous.y_min,
                              max(previous.y_max, span.y_max), previous.source_x,
                              previous.source_y, previous.materials, previous.flags)
        else:
            result.append(span)
    return result


def build_shell(spans: Iterable[Span], owner: tuple[int, int, int, int]) -> list[Face]:
    canonical = canonicalize(spans)
    columns: dict[tuple[int, int], list[Span]] = {}
    for span in canonical:
        columns.setdefault((span.x, span.z), []).append(span)
    min_x, min_z, max_x, max_z = owner
    faces: list[Face] = []

    def occupied(column: list[Span], y: int) -> bool:
        return any(span.y_min <= y < span.y_max for span in column)

    for span in canonical:
        if not (min_x <= span.x < max_x and min_z <= span.z < max_z):
            continue
        own_column = columns[(span.x, span.z)]
        common = (span.source_x, span.source_y, span.flags)
        if not occupied(own_column, span.y_max):
            faces.append(Face(1, 1, span.y_max, *common[:2], span.materials[FACE_TOP],
                              common[2], span.z, span.z + 1, span.x, span.x + 1))
        if not occupied(own_column, span.y_min - 1):
            faces.append(Face(1, -1, span.y_min, *common[:2], span.materials[FACE_BOTTOM],
                              common[2], span.z, span.z + 1, span.x, span.x + 1))
        for dx, dz, face_id, axis, sign in (
                (0, -1, FACE_NORTH, 2, -1), (1, 0, FACE_EAST, 0, 1),
                (0, 1, FACE_SOUTH, 2, 1), (-1, 0, FACE_WEST, 0, -1)):
            cursor = span.y_min
            neighbor = columns.get((span.x + dx, span.z + dz), ())
            plane = (span.x if axis == 0 else span.z) + (1 if sign > 0 else 0)
            u = span.z if axis == 0 else span.x
            for adjacent in neighbor:
                covered_min = max(cursor, adjacent.y_min)
                covered_max = min(span.y_max, adjacent.y_max)
                if covered_max <= cursor or covered_min >= span.y_max:
                    continue
                if covered_min > cursor:
                    faces.append(Face(axis, sign, plane, *common[:2],
                                      span.materials[face_id], common[2], cursor,
                                      covered_min, u, u + 1))
                cursor = max(cursor, covered_max)
            if cursor < span.y_max:
                faces.append(Face(axis, sign, plane, *common[:2],
                                  span.materials[face_id], common[2], cursor,
                                  span.y_max, u, u + 1))
    return merge_faces(faces)


def merge_faces(faces: Iterable[Face]) -> list[Face]:
    ordered = sorted(faces)
    horizontal: list[Face] = []
    for face in ordered:
        if horizontal:
            previous = horizontal[-1]
            key = ("axis", "sign", "plane", "source_x", "source_y", "material",
                   "flags", "v_min", "v_max")
            if all(getattr(previous, field) == getattr(face, field) for field in key) \
                    and previous.u_max == face.u_min:
                horizontal[-1] = Face(previous.axis, previous.sign, previous.plane,
                                      previous.source_x, previous.source_y,
                                      previous.material, previous.flags, previous.v_min,
                                      previous.v_max, previous.u_min, face.u_max)
                continue
        horizontal.append(face)
    result: list[Face] = []
    for face in horizontal:
        if result:
            previous = result[-1]
            key = ("axis", "sign", "plane", "source_x", "source_y", "material",
                   "flags", "u_min", "u_max")
            if all(getattr(previous, field) == getattr(face, field) for field in key) \
                    and previous.v_max == face.v_min:
                result[-1] = Face(previous.axis, previous.sign, previous.plane,
                                  previous.source_x, previous.source_y,
                                  previous.material, previous.flags, previous.v_min,
                                  face.v_max, previous.u_min, previous.u_max)
                continue
        result.append(face)
    return result


def editor_geometry(cells: list[dict], resolved: list[dict], profiles: list[dict]) -> dict:
    profile_masks = {}
    for profile in profiles:
        mask = profile.get("masks", {}).get("occupancy")
        if mask:
            profile_masks[profile["id"]] = [int(row, 16) for row in mask["rows"]]
    spans = []
    for index, (cell, resolution) in enumerate(zip(cells, resolved)):
        rule = resolution["rule"]
        if rule["shape"] == "hidden":
            continue
        profile = rule.get("semanticProfile")
        rows = profile_masks.get(profile, [0xFFFF] * UNITS_PER_CELL)
        ground = round(rule.get("groundHeight", 0) * UNITS_PER_CELL)
        height = round(rule.get("height", 0) * UNITS_PER_CELL)
        top = ground + height
        bottom = -1 if top > 0 else top - 1
        base = index * 12
        materials = (base + 1, base + 1, base + 2, base + 3, base + 4, base + 5)
        underlay = (base + 7, base + 7, base + 8, base + 9, base + 10, base + 11)
        archetype = rule.get("archetype")
        shape = rule["shape"]

        def stair_top(pixel_x: int, pixel_z: int) -> int:
            directions = {"stairs-n": 0, "stairs-s": 1, "stairs-e": 2,
                          "stairs-w": 3, "stairs-down-n": 0,
                          "stairs-down-s": 1, "stairs-down-e": 2,
                          "stairs-down-w": 3}
            direction = directions.get(archetype, 0)
            coordinate = pixel_z if direction in (0, 1) else pixel_x
            if direction in (0, 2):
                coordinate = 15 - coordinate
            if cell["behavior"] in (0xD0, 0xD1):
                step = coordinate + 1
            else:
                step = (coordinate // 4 + 1) * 4
            rise = height or 16
            amount = rise * step // 16
            return ground - amount if archetype and archetype.startswith("stairs-down-") \
                else ground + amount

        for pixel_z in range(min(len(rows), UNITS_PER_CELL)):
            for pixel_x in range(UNITS_PER_CELL):
                if rows[pixel_z] & (1 << pixel_x):
                    column_x = cell["x"] * 16 + pixel_x
                    column_z = cell["y"] * 16 + pixel_z
                    if shape == "bridge":
                        spans.append(Span(column_x, column_z, -3, -2,
                                          cell["x"], cell["y"], underlay))
                        spans.append(Span(column_x, column_z, top - 1, top,
                                          cell["x"], cell["y"], materials))
                    elif shape == "ledge":
                        spans.append(Span(column_x, column_z, -1, 0,
                                          cell["x"], cell["y"], materials))
                    elif shape == "stairs":
                        stair = stair_top(pixel_x, pixel_z)
                        descending = bool(archetype and archetype.startswith("stairs-down-"))
                        stair_bottom = ground - (height or 16) - 1 if descending else -1
                        spans.append(Span(column_x, column_z, stair_bottom,
                                          stair, cell["x"], cell["y"], materials))
                    else:
                        spans.append(Span(column_x, column_z, bottom, top,
                                          cell["x"], cell["y"], materials))
    width = max((cell["x"] for cell in cells), default=-1) + 1
    height = max((cell["y"] for cell in cells), default=-1) + 1
    canonical = canonicalize(spans)
    faces = build_shell(canonical, (0, 0, width * 16, height * 16))
    for index, (cell, resolution) in enumerate(zip(cells, resolved)):
        rule = resolution["rule"]
        if rule["shape"] != "ledge":
            continue
        direction = {0x3A: 0, 0x3C: 1, 0x38: 2, 0x3E: 3,
                     0x3B: 4, 0x3F: 5, 0x39: 6, 0x3D: 7}.get(cell["behavior"], 0)
        height_units = round((rule.get("height", 0) or 0.375) * UNITS_PER_CELL)
        x, z, base = cell["x"] * 16, cell["y"] * 16, index * 12
        alpha = cell.get("foregroundAlpha", [0] * (16 - height_units)
                         + [0xFFFF] * height_units)
        components = ((0,), (0, 2), (2,), (2, 4),
                      (4,), (4, 6), (6,), (6, 0))[direction]
        for component in components:
            axis, sign, plane, source_u, material = (
                (2, -1, z, x, base + 2) if component == 0 else
                (0, 1, x + 16, z, base + 3) if component == 2 else
                (2, 1, z + 16, x, base + 4) if component == 4 else
                (0, -1, x, z, base + 5))
            for source_y in range(16 - height_units, 16):
                row, source_x = alpha[source_y], 0
                while source_x < 16:
                    while source_x < 16 and not row & (1 << source_x):
                        source_x += 1
                    run_start = source_x
                    while source_x < 16 and row & (1 << source_x):
                        source_x += 1
                    if run_start < source_x:
                        faces.append(Face(axis, sign, plane, cell["x"], cell["y"],
                                          material, 0, 15 - source_y, 16 - source_y,
                                          source_u + run_start, source_u + source_x))
    faces = merge_faces(faces)
    return {
        "modelVersion": MODEL_VERSION,
        "unitsPerCell": UNITS_PER_CELL,
        "spans": [span.__dict__ | {"materials": list(span.materials)} for span in canonical],
        "faces": [face.__dict__ for face in faces],
        "provenance": [
            {"id": index * 12 + surface * 6 + face + 1, "cell": index,
             "surface": "primary" if surface == 0 else "underlay",
             "face": ("top", "north", "east", "south", "west", "plane")[face]}
            for index in range(len(cells)) for surface in range(2) for face in range(6)
        ],
    }


def main() -> int:
    parser = argparse.ArgumentParser(description="Build canonical G4 shell faces from occupancy spans")
    parser.add_argument("--input", type=Path, help="JSON input; stdin when omitted")
    parser.add_argument("--owner", default="-2147483648,-2147483648,2147483647,2147483647",
                        help="owned minX,minZ,maxX,maxZ rectangle")
    args = parser.parse_args()
    raw = json.loads(args.input.read_text(encoding="utf-8") if args.input else sys.stdin.read())
    owner = tuple(int(value) for value in args.owner.split(","))
    if len(owner) != 4:
        parser.error("--owner requires four comma-separated integers")
    spans = [Span(item["x"], item["z"], item["yMin"], item["yMax"],
                  item["sourceX"], item["sourceY"], tuple(item["materials"]),
                  item.get("flags", 0)) for item in raw["spans"]]
    canonical = canonicalize(spans)
    result = {"modelVersion": MODEL_VERSION, "unitsPerCell": UNITS_PER_CELL,
              "spans": [span.__dict__ | {"materials": list(span.materials)} for span in canonical],
              "faces": [face.__dict__ for face in build_shell(canonical, owner)]}
    json.dump(result, sys.stdout, sort_keys=True, separators=(",", ":"))
    sys.stdout.write("\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
