#!/usr/bin/env python3
"""Compile the authored MagicaVoxel tree into color-aware greedy quads."""

from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path


class VoxError(ValueError):
    pass


def read_vox(path: Path) -> tuple[tuple[int, int, int], dict[tuple[int, int, int], int], list[int]]:
    data = path.read_bytes()
    if len(data) < 20 or data[:4] != b"VOX ":
        raise VoxError(f"{path}: not a MagicaVoxel file")
    if struct.unpack_from("<I", data, 4)[0] > 150:
        raise VoxError(f"{path}: unsupported VOX version")

    size = None
    voxels = None
    palette = None
    offset = 8
    while offset + 12 <= len(data):
        chunk_id = data[offset:offset + 4]
        content_size, _children_size = struct.unpack_from("<II", data, offset + 4)
        content = offset + 12
        end = content + content_size
        if end > len(data):
            raise VoxError(f"{path}: truncated {chunk_id!r} chunk")
        if chunk_id == b"SIZE":
            if content_size != 12:
                raise VoxError(f"{path}: invalid SIZE chunk")
            size = struct.unpack_from("<III", data, content)
        elif chunk_id == b"XYZI":
            count = struct.unpack_from("<I", data, content)[0]
            if content_size != 4 + count * 4:
                raise VoxError(f"{path}: invalid XYZI chunk")
            voxels = {}
            for index in range(count):
                x, y, z, color = struct.unpack_from("<BBBB", data, content + 4 + index * 4)
                if color == 0:
                    raise VoxError(f"{path}: voxel uses reserved palette index 0")
                voxels[(x, y, z)] = color
        elif chunk_id == b"RGBA":
            if content_size != 1024:
                raise VoxError(f"{path}: invalid RGBA chunk")
            palette = list(struct.unpack_from("<256I", data, content))
        offset = end

    if size is None or voxels is None or palette is None:
        raise VoxError(f"{path}: SIZE, XYZI and RGBA chunks are required")
    for coordinate in voxels:
        if any(coordinate[axis] >= size[axis] for axis in range(3)):
            raise VoxError(f"{path}: voxel {coordinate} exceeds model size {size}")
    return size, voxels, palette


def greedy_faces(size: tuple[int, int, int], voxels: dict[tuple[int, int, int], int]):
    faces = []
    for axis in range(3):
        u_axis = (axis + 1) % 3
        v_axis = (axis + 2) % 3
        width = size[u_axis]
        height = size[v_axis]
        for plane in range(size[axis] + 1):
            mask = [None] * (width * height)
            for v in range(height):
                for u in range(width):
                    coordinate = [0, 0, 0]
                    coordinate[u_axis] = u
                    coordinate[v_axis] = v
                    before = 0
                    after = 0
                    if plane > 0:
                        coordinate[axis] = plane - 1
                        before = voxels.get(tuple(coordinate), 0)
                    if plane < size[axis]:
                        coordinate[axis] = plane
                        after = voxels.get(tuple(coordinate), 0)
                    if before and not after:
                        mask[v * width + u] = (before, 1)
                    elif after and not before:
                        mask[v * width + u] = (after, -1)

            v = 0
            while v < height:
                u = 0
                while u < width:
                    key = mask[v * width + u]
                    if key is None:
                        u += 1
                        continue
                    run_width = 1
                    while u + run_width < width and mask[v * width + u + run_width] == key:
                        run_width += 1
                    run_height = 1
                    while v + run_height < height and all(
                        mask[(v + run_height) * width + candidate] == key
                        for candidate in range(u, u + run_width)
                    ):
                        run_height += 1
                    for clear_v in range(v, v + run_height):
                        for clear_u in range(u, u + run_width):
                            mask[clear_v * width + clear_u] = None
                    faces.append((axis, key[1], plane, u, u + run_width,
                                  v, v + run_height, key[0]))
                    u += run_width
                v += 1
    return faces


def render_header(size: tuple[int, int, int], voxels: dict[tuple[int, int, int], int]) -> str:
    occupied_min = tuple(min(coordinate[axis] for coordinate in voxels) for axis in range(3))
    occupied_max = tuple(max(coordinate[axis] for coordinate in voxels) + 1 for axis in range(3))
    return f"""#ifndef GUARD_DIORAMA_TREE_MODEL_GENERATED_H
#define GUARD_DIORAMA_TREE_MODEL_GENERATED_H

#include <stdint.h>

#define DIORAMA_TREE_MODEL_SIZE_X {size[0]}
#define DIORAMA_TREE_MODEL_SIZE_Z {size[1]}
#define DIORAMA_TREE_MODEL_SIZE_Y {size[2]}
#define DIORAMA_TREE_MODEL_MIN_X {occupied_min[0]}
#define DIORAMA_TREE_MODEL_MIN_Z {occupied_min[1]}
#define DIORAMA_TREE_MODEL_MIN_Y {occupied_min[2]}
#define DIORAMA_TREE_MODEL_MAX_X {occupied_max[0]}
#define DIORAMA_TREE_MODEL_MAX_Z {occupied_max[1]}
#define DIORAMA_TREE_MODEL_MAX_Y {occupied_max[2]}

struct DioramaTreeModelFace
{{
    uint8_t axis;
    int8_t sign;
    uint8_t plane;
    uint8_t uMin;
    uint8_t uMax;
    uint8_t vMin;
    uint8_t vMax;
    uint32_t rgba;
}};

extern const struct DioramaTreeModelFace gDioramaTreeModelFaces[];
extern const uint32_t gDioramaTreeModelFaceCount;
extern const uint32_t gDioramaTreeModelVoxelCount;

#endif
"""


def render_source(faces, palette: list[int], voxel_count: int) -> str:
    lines = [
        "#ifdef ENABLE_DIORAMA",
        "#include <stdint.h>",
        '#include "diorama/tree_model.generated.h"',
        "",
        "const struct DioramaTreeModelFace gDioramaTreeModelFaces[] =",
        "{",
    ]
    for axis, sign, plane, u0, u1, v0, v1, color_index in faces:
        rgba = palette[color_index - 1]
        lines.append(
            f"    {{{axis}, {sign}, {plane}, {u0}, {u1}, {v0}, {v1}, "
            f"UINT32_C(0x{rgba:08X})}},"
        )
    lines.extend((
        "};",
        "",
        "const uint32_t gDioramaTreeModelFaceCount =",
        "    sizeof(gDioramaTreeModelFaces) / sizeof(gDioramaTreeModelFaces[0]);",
        f"const uint32_t gDioramaTreeModelVoxelCount = {voxel_count};",
        "#endif",
        "",
    ))
    return "\n".join(lines)


def write_or_check(path: Path, content: str, check: bool) -> None:
    if check:
        if not path.exists() or path.read_text(encoding="utf-8") != content:
            raise VoxError(f"{path}: generated file is stale")
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--input", type=Path, default=Path("tree_model/emerald_tree.vox"))
    parser.add_argument("--output-c", type=Path,
                        default=Path("src/data/diorama/tree_model.generated.c"))
    parser.add_argument("--output-h", type=Path,
                        default=Path("include/diorama/tree_model.generated.h"))
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    root = args.root.resolve()
    try:
        input_path = args.input if args.input.is_absolute() else root / args.input
        output_c = args.output_c if args.output_c.is_absolute() else root / args.output_c
        output_h = args.output_h if args.output_h.is_absolute() else root / args.output_h
        size, voxels, palette = read_vox(input_path)
        faces = greedy_faces(size, voxels)
        write_or_check(output_c, render_source(faces, palette, len(voxels)), args.check)
        write_or_check(output_h, render_header(size, voxels), args.check)
        if not args.check:
            print(f"tree model: {len(voxels)} voxels -> {len(faces)} greedy quads")
    except (OSError, struct.error, VoxError) as error:
        print(f"tree model: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
