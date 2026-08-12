#!/usr/bin/env python3
"""Deterministic artwork masks for one-cell Emerald mountain terrain."""

from __future__ import annotations

from collections import Counter, defaultdict, deque


class MountainArtError(ValueError):
    pass


GENERAL_TERRACE_METATILES = frozenset((111, 113, 121, 133, 135, 136, 137, 141, 144))


def mountain_art_mask(summary: dict) -> tuple[tuple[int, ...], str]:
    layers = summary.get("layers")
    if not summary.get("resolved") or not layers:
        raise MountainArtError("mountain metatile artwork is unresolved")

    foreground = layers[1]
    visible = sum(pixel.visible for pixel in foreground)
    if 0 < visible < 256:
        pixels = foreground
        mode = "foreground"
        outside: set[int] = set()
    else:
        pixels = layers[2]
        corners = [pixels[index].rgba for index in (0, 15, 240, 255)]
        background, count = min(Counter(corners).items(),
                                key=lambda item: (-item[1], item[0]))
        outside = set()
        if count >= 2:
            pending = deque(index for index in range(256)
                            if (index % 16 in (0, 15) or index // 16 in (0, 15))
                            and pixels[index].rgba == background)
            outside.update(pending)
            while pending:
                index = pending.popleft()
                x, y = index % 16, index // 16
                for neighbor in ((index - 1 if x else -1),
                                 (index + 1 if x < 15 else -1),
                                 (index - 16 if y else -1),
                                 (index + 16 if y < 15 else -1)):
                    if neighbor >= 0 and neighbor not in outside \
                            and pixels[neighbor].rgba == background:
                        outside.add(neighbor)
                        pending.append(neighbor)
        mode = "silhouette"

    rows = tuple(sum(1 << x for x in range(16)
                     if pixels[y * 16 + x].visible
                     and y * 16 + x not in outside)
                 for y in range(16))
    occupied = sum(row.bit_count() for row in rows)
    if occupied == 0 or occupied == 256:
        raise MountainArtError(
            f"mountain {mode} mask must be non-empty and non-rectangular, got {occupied} pixels")
    return rows, mode


def mountain_heightmap(rows: tuple[int, ...]) -> tuple[int, ...]:
    remaining = list(rows)
    depths = [0] * 256
    depth = 0
    while any(remaining):
        depth += 1
        eroded = [0] * 16
        for y, bits in enumerate(remaining):
            for x in range(16):
                if bits & (1 << x):
                    depths[y * 16 + x] = depth
            if 0 < y < 15:
                eroded[y] = (bits & (bits << 1) & (bits >> 1)
                              & remaining[y - 1] & remaining[y + 1]) & 0xFFFF
        remaining = eroded
    if depth == 0:
        return tuple(depths)
    return tuple(0 if value == 0 else 16 if depth == 1
                 else 4 + (value - 1) * 12 // (depth - 1)
                 for value in depths)


def detect_layout(cells: tuple[dict, ...], summaries: tuple[dict, ...],
                  width: int, height: int) -> frozenset[int]:
    seeds = {offset for offset, cell in enumerate(cells)
             if cell.get("behavior") == "MB_MOUNTAIN_TOP"}
    palette_rows: dict[int, set[int]] = defaultdict(set)
    for offset in seeds:
        for pixel in summaries[offset].get("layers", ((), (), ()))[2]:
            source = pixel.source
            if pixel.visible and source is not None and source.palette >= 6:
                palette_rows[source.palette].add(source.tile // 16)

    eligible = set()
    for offset, (cell, summary) in enumerate(zip(cells, summaries)):
        if cell.get("behavior") != "MB_NORMAL":
            continue
        mountain_pixels = 0
        outside_family = False
        for pixel in summary.get("layers", ((), (), ()))[2]:
            source = pixel.source
            if not pixel.visible or source is None or source.palette < 6:
                continue
            rows = palette_rows.get(source.palette, set())
            if source.tile // 16 not in rows:
                outside_family = True
                break
            mountain_pixels += 1
        if outside_family or mountain_pixels < 16:
            continue
        try:
            mountain_art_mask(summary)
        except MountainArtError:
            continue
        eligible.add(offset)

    pending = deque(sorted(seeds))
    reached = set(seeds)
    while pending:
        offset = pending.popleft()
        x, y = offset % width, offset // width
        for neighbor in ((offset - 1 if x else -1),
                         (offset + 1 if x + 1 < width else -1),
                         (offset - width if y else -1),
                         (offset + width if y + 1 < height else -1)):
            if neighbor in eligible and neighbor not in reached:
                reached.add(neighbor)
                pending.append(neighbor)
    return frozenset(reached)


def validate_layout(cells: tuple[dict, ...], summaries: tuple[dict, ...],
                    width: int, height: int) -> frozenset[int]:
    offsets = detect_layout(cells, summaries, width, height)
    dense_forest = {offset for offset, cell in enumerate(cells)
                    if cell.get("tileset") == "gTileset_General"
                    and cell.get("metatile") in (198, 199)}
    terraces = {offset for offset, cell in enumerate(cells)
                if cell.get("tileset") == "gTileset_General"
                and cell.get("metatile") in GENERAL_TERRACE_METATILES}
    for offset in sorted(offsets | dense_forest | terraces):
        summary = summaries[offset]
        try:
            rows, _ = mountain_art_mask(summary)
            heights = mountain_heightmap(rows)
            if max(heights) != 16:
                raise MountainArtError("mountain heightmap does not reach one cell")
        except MountainArtError as error:
            raise MountainArtError(f"mountain cell {offset}: {error}") from error
    return offsets
