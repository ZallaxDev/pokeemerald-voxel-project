#!/usr/bin/env python3
"""Conservative pixel-object extraction from complete R3 candidates."""

from __future__ import annotations

from collections import Counter, deque
from dataclasses import dataclass
from typing import Any


PIXELS_PER_CELL = 16
Q32_PER_PIXEL = 2
LAYERED_CUTOUT_DEPTH_Q32 = 6
CONSOLE_DEPTH_Q32 = 8
RELIEF_DEPTH_Q32 = 12


@dataclass(frozen=True)
class ExtractionThresholds:
    # One GBA 5-bit palette step expands to at least eight 8-bit values. Keep
    # distinct indexed shades separate while tolerating only conversion noise.
    color_distance: int = 7
    black_max_channel: int = 90
    dark_max_channel: int = 150
    light_max_channel: int = 220
    max_upright_height: int = 64
    max_solid_ratio: float = 0.82
    repetitive_row_ratio: float = 0.75
    min_pixels: int = 3


DEFAULT_THRESHOLDS = ExtractionThresholds()
UPRIGHT_CLASSES = frozenset(("billboard", "cutout", "console", "signpost", "post"))
AUTOMATIC_CLASSES = frozenset(("rock", "shrub", "stump", "tree"))
FORCED_OUTLINE_CLASSES = frozenset(("billboard", "console", "signpost", "post"))
GROUND_REPLACEMENT = "replacement"
GROUND_SOURCE_BASE = "source-base"


def _shape_value(shape: Any, attribute: str, key: str) -> Any:
    return getattr(shape, attribute, shape[key])


def _neighbors(index: int, width: int, height: int, diagonal: bool = False):
    x, y = index % width, index // width
    directions = ((-1, 0), (1, 0), (0, -1), (0, 1))
    if diagonal:
        directions += ((-1, -1), (1, -1), (-1, 1), (1, 1))
    for dx, dy in directions:
        nx, ny = x + dx, y + dy
        if 0 <= nx < width and 0 <= ny < height:
            yield ny * width + nx


def _ground_vote(candidate: dict, cells: tuple[dict, ...], shapes: tuple[Any, ...],
                 layout_width: int, layout_height: int) -> int | None:
    configured = candidate.get("propGround")
    if configured and configured.get("mode") == "manual":
        return int(configured["metatile"])
    manual = set()
    for offset in candidate["cells"]:
        prop_ground = getattr(shapes[offset], "prop_ground", None)
        if prop_ground is not None and getattr(prop_ground, "mode", None) == "manual":
            manual.add(prop_ground.metatile)
    if len(manual) == 1:
        return manual.pop()
    if len(manual) > 1:
        return None
    claimed = set(candidate["cells"])
    votes: Counter[int] = Counter()
    for offset in claimed:
        for neighbor in _neighbors(offset, layout_width, layout_height):
            if neighbor in claimed:
                continue
            shape = shapes[neighbor]
            if (str(_shape_value(shape, "pool", "pool")) == "terrain"
                    and str(_shape_value(shape, "art_mode", "artMode")) == "flat"):
                votes[int(cells[neighbor]["metatile"])] += 1
    if not votes:
        return None
    best = max(votes.values())
    winners = sorted(value for value, count in votes.items() if count == best)
    return winners[0] if len(winners) == 1 else None


def _rgba_distance(left: tuple[int, ...], right: tuple[int, ...]) -> int:
    return max(abs(int(a) - int(b)) for a, b in zip(left[:3], right[:3]))


def _shade_class(pixel: Any, thresholds: ExtractionThresholds) -> str:
    if not pixel.visible:
        return "transparent"
    darkest = min(pixel.rgba[:3])
    if darkest <= thresholds.black_max_channel:
        return "black"
    if darkest <= thresholds.dark_max_channel:
        return "dark"
    if darkest <= thresholds.light_max_channel:
        return "light"
    return "white"


def _components(mask: tuple[bool, ...], width: int, height: int) -> list[list[int]]:
    pending = {index for index, value in enumerate(mask) if value}
    output = []
    while pending:
        start = min(pending)
        pending.remove(start)
        queue, component = deque((start,)), []
        while queue:
            index = queue.popleft()
            component.append(index)
            for neighbor in _neighbors(index, width, height, diagonal=True):
                if neighbor in pending:
                    pending.remove(neighbor)
                    queue.append(neighbor)
        output.append(sorted(component))
    return output


def _is_repetitive(mask: tuple[bool, ...], width: int, height: int,
                   threshold: float) -> bool:
    if height < PIXELS_PER_CELL * 3:
        return False
    rows = [mask[y * width:(y + 1) * width] for y in range(height)]
    matches = sum(rows[y] == rows[y - PIXELS_PER_CELL]
                  for y in range(PIXELS_PER_CELL, height))
    return matches / (height - PIXELS_PER_CELL) >= threshold


def _source_record(pixel: Any, source_cell: int, source_x: int, source_y: int,
                   x_q32: int, y_q32: int, z_q32: int, kind: str,
                   depth_q32: int) -> dict:
    source = pixel.source
    if source is None or source.color_index == 0:
        raise ValueError("visible pixel has no provenance")
    tile_entry = (source.tile | (0x400 if source.hflip else 0)
                  | (0x800 if source.vflip else 0) | (source.palette << 12))
    return {
        "sourceCellOffset": source_cell,
        "expectedMetatile": source.metatile_id,
        "expectedTileEntry": tile_entry,
        "xQ32": x_q32,
        "yQ32": y_q32,
        "zQ32": z_q32,
        "sizeXQ32": Q32_PER_PIXEL,
        "sizeYQ32": (Q32_PER_PIXEL if kind == "cutout" else RELIEF_DEPTH_Q32),
        "sizeZQ32": (Q32_PER_PIXEL if kind == "relief" else depth_q32),
        "sourceLayer": source.layer,
        "sourceSubtile": source.subtile,
        "sourceTile": source.tile,
        "sourcePalette": source.palette,
        "sourceColor": source.color_index,
        "sourceU": source.u,
        "sourceV": source.v,
        "sourceX": source_x,
        "sourceY": source_y,
    }


def extract_candidate(candidate: dict, cells: tuple[dict, ...], shapes: tuple[Any, ...],
                      pixel_summaries: tuple[dict, ...], layout_width: int,
                      layout_height: int,
                      thresholds: ExtractionThresholds = DEFAULT_THRESHOLDS) -> dict | None:
    """Extract one immutable cutout/relief IR record, or reject it safely."""
    class_name = str(candidate["class"])
    kind = "relief" if class_name == "relief" else "cutout"
    if class_name not in UPRIGHT_CLASSES | AUTOMATIC_CLASSES and kind != "relief":
        return None
    bbox = candidate["bbox"]
    width = bbox["width"] * PIXELS_PER_CELL
    height = bbox["height"] * PIXELS_PER_CELL
    if kind == "cutout" and height > thresholds.max_upright_height:
        return None
    ground = _ground_vote(candidate, cells, shapes, layout_width, layout_height)
    composed = [None] * (width * height)
    source_cells = [-1] * (width * height)
    claimed = set(candidate["cells"])
    for offset in claimed:
        summary = pixel_summaries[offset]
        layers = summary.get("layers")
        if not layers:
            return None
        cell_x, cell_y = offset % layout_width, offset // layout_width
        local_x = (cell_x - bbox["x"]) * PIXELS_PER_CELL
        local_y = (cell_y - bbox["y"]) * PIXELS_PER_CELL
        for py in range(PIXELS_PER_CELL):
            for px in range(PIXELS_PER_CELL):
                target = (local_y + py) * width + local_x + px
                composed[target] = layers[2][py * PIXELS_PER_CELL + px]
                source_cells[target] = offset
    if any(pixel is None for pixel in composed):
        return None

    foreground = [None] * (width * height)
    for offset in claimed:
        layers = pixel_summaries[offset].get("layers")
        cell_x, cell_y = offset % layout_width, offset // layout_width
        local_x = (cell_x - bbox["x"]) * PIXELS_PER_CELL
        local_y = (cell_y - bbox["y"]) * PIXELS_PER_CELL
        for py in range(PIXELS_PER_CELL):
            for px in range(PIXELS_PER_CELL):
                foreground[(local_y + py) * width + local_x + px] = \
                    layers[1][py * PIXELS_PER_CELL + px]
    has_foreground = class_name in ("cutout", "relief") \
        and any(pixel is not None and pixel.visible for pixel in foreground)
    if has_foreground:
        composed = foreground
        pixel_rows = candidate.get("objectPixelRows")
        mask = tuple(pixel.visible and (pixel_rows is None
            or pixel_rows[0] <= index // width < pixel_rows[1])
            for index, pixel in enumerate(composed))
        ground_mode = candidate.get("objectGroundMode") or GROUND_SOURCE_BASE
        if ground_mode == GROUND_SOURCE_BASE:
            ground = 0xFFFF
        elif ground is None:
            return None
    else:
        if ground is None:
            return None
        ground_layers = next((summary.get("layers") for summary in pixel_summaries
                              if summary.get("metatile") == ground and summary.get("layers")), None)
        if ground_layers is None:
            return None
        ground_pixels = ground_layers[2]
        ground_mode = GROUND_REPLACEMENT

    forced = class_name in FORCED_OUTLINE_CLASSES or class_name in ("cutout", "relief")
    if not has_foreground and class_name == "console":
        # Emerald's opaque TV metatile is the complete rectangular appliance. It has no
        # separable background layer; outline flooding would retain only three texels.
        mask = tuple(pixel.visible for pixel in composed)
    elif not has_foreground:
        boundary_indices = list(range(width))
        boundary_indices.extend((height - 1) * width + x for x in range(width))
        boundary_indices.extend(y * width for y in range(1, height - 1))
        boundary_indices.extend(y * width + width - 1 for y in range(1, height - 1))
        boundary_classes = {_shade_class(composed[index], thresholds)
                            for index in boundary_indices}
        boundary_classes.discard("black")
        if forced:
            background_candidate = tuple(
                _shade_class(pixel, thresholds) in boundary_classes for pixel in composed)
        else:
            background_candidate = tuple(
                not pixel.visible or (ground_pixels[index % PIXELS_PER_CELL
                    + ((index // width) % PIXELS_PER_CELL) * PIXELS_PER_CELL].visible
                    and _rgba_distance(pixel.rgba, ground_pixels[index % PIXELS_PER_CELL
                        + ((index // width) % PIXELS_PER_CELL) * PIXELS_PER_CELL].rgba)
                    <= thresholds.color_distance)
                for index, pixel in enumerate(composed))
        flooded = [False] * (width * height)
        queue = deque(index for index in boundary_indices if background_candidate[index])
        for index in queue:
            flooded[index] = True
        while queue:
            index = queue.popleft()
            for neighbor in _neighbors(index, width, height):
                if background_candidate[neighbor] and not flooded[neighbor]:
                    flooded[neighbor] = True
                    queue.append(neighbor)
        mask = tuple(pixel.visible and not flooded[index]
                     for index, pixel in enumerate(composed))
    visible_count = sum(mask)
    if visible_count < thresholds.min_pixels:
        return None
    if forced and kind == "cutout" and class_name != "console" \
            and visible_count / (width * height) > 0.95:
        return None
    if not forced:
        touches_edge = any(mask[x] or mask[(height - 1) * width + x] for x in range(width)) \
            or any(mask[y * width] or mask[y * width + width - 1] for y in range(height))
        if touches_edge or visible_count / (width * height) > thresholds.max_solid_ratio:
            return None
        if _is_repetitive(mask, width, height, thresholds.repetitive_row_ratio):
            return None
    components = _components(mask, width, height)
    pixels = []
    for component_id, component in enumerate(components, 1):
        foot = max(index // width for index in component)
        for index in component:
            x, y = index % width, index // width
            source_cell = source_cells[index]
            source_x = x % PIXELS_PER_CELL
            source_y = y % PIXELS_PER_CELL
            if kind == "cutout":
                x_q32 = bbox["x"] * 32 + x * Q32_PER_PIXEL
                y_q32 = (foot - y) * Q32_PER_PIXEL
                z_q32 = ((bbox["y"] + bbox["height"]) * 32 - 17
                           + int(candidate.get("objectDepthOffsetQ32", 0)))
            else:
                x_q32 = bbox["x"] * 32 + x * Q32_PER_PIXEL
                y_q32 = 0
                z_q32 = bbox["y"] * 32 + y * Q32_PER_PIXEL
            depth_q32 = (int(candidate["objectThicknessQ32"])
                         if candidate.get("objectThicknessQ32") is not None
                         else CONSOLE_DEPTH_Q32 if class_name == "console"
                         else LAYERED_CUTOUT_DEPTH_Q32 if has_foreground
                         else Q32_PER_PIXEL)
            record = _source_record(composed[index], source_cell, source_x, source_y,
                                    x_q32, y_q32, z_q32, kind, depth_q32)
            record["component"] = component_id
            pixels.append(record)
    return {
        "structureId": candidate["id"], "kind": kind, "pool": candidate["pool"],
        "class": class_name, "groundMode": ground_mode, "groundMetatile": ground,
        "supportOffsetQ16": (int(candidate["objectOffsetQ16"])
            if candidate.get("objectOffsetQ16") is not None
            else (12 if class_name in ("billboard", "console") else 0)),
        "spriteDepthBiasMillionths": int(candidate.get("spriteDepthBiasMillionths", 0)),
        "width": width, "height": height,
        "maskRows": [sum((1 << x) for x in range(width) if mask[y * width + x])
                     for y in range(height)],
        "componentCount": len(components), "pixels": pixels,
        "thresholds": {
            "colorDistance": thresholds.color_distance,
            "blackMaxChannel": thresholds.black_max_channel,
            "darkMaxChannel": thresholds.dark_max_channel,
            "lightMaxChannel": thresholds.light_max_channel,
            "maxUprightHeight": thresholds.max_upright_height,
            "maxSolidRatio": thresholds.max_solid_ratio,
            "repetitiveRowRatio": thresholds.repetitive_row_ratio,
            "minPixels": thresholds.min_pixels,
        },
    }


def extract_layout(analysis: dict, cells: tuple[dict, ...], shapes: tuple[Any, ...],
                   pixel_summaries: tuple[dict, ...], layout_width: int,
                   layout_height: int) -> list[dict]:
    output = []
    for candidate in analysis["candidates"]:
        result = extract_candidate(candidate, cells, shapes, pixel_summaries,
                                   layout_width, layout_height)
        if result is None:
            continue
        result["supportStructureId"] = 0
        if candidate["class"] in ("billboard", "console"):
            support_y = candidate["bbox"]["y"] - 1
            support_x = candidate["bbox"]["x"]
            if support_y < 0:
                continue
            support_owner = analysis["cells"][support_y * layout_width + support_x]["owner"]
            if support_owner is None:
                continue
            support = analysis["candidates"][support_owner - 1]
            if support["pool"] != "furniture":
                continue
            result["supportStructureId"] = support_owner
        output.append(result)
    return output
