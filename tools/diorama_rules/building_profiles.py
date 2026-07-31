#!/usr/bin/env python3
"""Compose Emerald map art and extract deterministic building roof profiles."""

from __future__ import annotations

import json
import struct
from collections import deque
from dataclasses import dataclass
from pathlib import Path

from catalog import CatalogError, load_tilesets

from emerald_compositor import (METATILE_SIZE, EmeraldCompositorError, Pixel,
                                PixelSource, TilesetComposer, TRANSPARENT_PIXEL)


BuildingProfileError = EmeraldCompositorError


@dataclass(frozen=True)
class ComposedFootprint:
    width: int
    height: int
    metatiles: tuple[int, ...]
    base: tuple[Pixel, ...]
    foreground: tuple[Pixel, ...]
    full: tuple[Pixel, ...]

    def layer(self, name: str) -> tuple[Pixel, ...]:
        if name not in ("base", "foreground", "full"):
            raise BuildingProfileError(f"unknown composition layer {name!r}")
        return getattr(self, name)


def _catalog_entry(root: Path, map_symbol: str) -> tuple[dict, dict]:
    layouts_data = json.loads((root / "data/layouts/layouts.json").read_text(encoding="utf-8"))
    layouts = {entry["id"]: entry for entry in layouts_data["layouts"]}
    groups = json.loads((root / "data/maps/map_groups.json").read_text(encoding="utf-8"))
    for group_name in groups["group_order"]:
        for directory in groups[group_name]:
            map_path = root / "data/maps" / directory / "map.json"
            map_data = json.loads(map_path.read_text(encoding="utf-8"))
            if map_data["id"] == map_symbol:
                return map_data, layouts[map_data["layout"]]
    raise BuildingProfileError(f"unknown reference map {map_symbol}")


def compose_reference(root: Path, map_symbol: str, x: int, y: int,
                      width: int, height: int) -> ComposedFootprint:
    _, layout = _catalog_entry(root, map_symbol)
    map_width, map_height = layout["width"], layout["height"]
    if x < 0 or y < 0 or x + width > map_width or y + height > map_height:
        raise BuildingProfileError(
            f"reference ({x}, {y}, {width}, {height}) is outside {map_symbol} "
            f"({map_width}x{map_height})")
    map_path = root / layout["blockdata_filepath"]
    raw_map = map_path.read_bytes()
    if len(raw_map) != map_width * map_height * 2:
        raise BuildingProfileError(f"{map_path}: map dimensions do not match its layout")
    metatiles = tuple((struct.unpack_from("<H", raw_map, ((y + row) * map_width + x + column) * 2)[0]
                       & 0x3FF)
                      for row in range(height) for column in range(width))
    try:
        tilesets = load_tilesets(root)
        primary = tilesets[layout["primary_tileset"]]
        secondary = tilesets[layout["secondary_tileset"]]
        primary_root = root / primary.root
        secondary_root = root / secondary.root
    except (CatalogError, KeyError) as error:
        raise BuildingProfileError(f"{map_symbol}: unsupported tileset {error.args[0]}") from error
    composer = TilesetComposer(primary_root, secondary_root,
                               root / primary.metatiles_root,
                               root / secondary.metatiles_root)
    pixel_width, pixel_height = width * METATILE_SIZE, height * METATILE_SIZE
    output = [[TRANSPARENT_PIXEL] * (pixel_width * pixel_height) for _ in range(3)]
    for cell_y in range(height):
        for cell_x in range(width):
            metatile_id = metatiles[cell_y * width + cell_x]
            layers = composer.compose_metatile(metatile_id)
            for layer_index, layer_pixels in enumerate(layers):
                for py in range(METATILE_SIZE):
                    source = py * METATILE_SIZE
                    target = (cell_y * METATILE_SIZE + py) * pixel_width + cell_x * METATILE_SIZE
                    output[layer_index][target:target + METATILE_SIZE] = layer_pixels[source:source + METATILE_SIZE]
    return ComposedFootprint(pixel_width, pixel_height, metatiles,
                             tuple(output[0]), tuple(output[1]), tuple(output[2]))


def extract_roof_profile(footprint: ComposedFootprint, roof_rows: int, layer: str,
                         seals: tuple[tuple[int, int], ...] = (), context: str = "roof") -> tuple[int, ...]:
    roof_height = roof_rows * METATILE_SIZE
    if roof_rows <= 0 or roof_height > footprint.height:
        raise BuildingProfileError(f"{context}: roofRows does not select a valid image region")
    pixels = footprint.layer(layer)
    occupied = [pixels[y * footprint.width + x].visible
                for y in range(roof_height) for x in range(footprint.width)]
    for x, y in seals:
        if x < 0 or x >= footprint.width or y < 0 or y >= roof_height:
            raise BuildingProfileError(f"{context}: seal pixel ({x}, {y}) is outside the roof image")
        occupied[y * footprint.width + x] = True
    exterior = [False] * len(occupied)
    queue: deque[tuple[int, int]] = deque()
    for x in range(footprint.width):
        queue.extend(((x, 0), (x, roof_height - 1)))
    for y in range(roof_height):
        queue.extend(((0, y), (footprint.width - 1, y)))
    while queue:
        x, y = queue.popleft()
        index = y * footprint.width + x
        if occupied[index] or exterior[index]:
            continue
        exterior[index] = True
        if x:
            queue.append((x - 1, y))
        if x + 1 < footprint.width:
            queue.append((x + 1, y))
        if y:
            queue.append((x, y - 1))
        if y + 1 < roof_height:
            queue.append((x, y + 1))
    profile = []
    for boundary in range(footprint.width + 1):
        x = min(boundary, footprint.width - 1)
        first = next((y for y in range(roof_height)
                      if not exterior[y * footprint.width + x]), None)
        profile.append(0 if first is None else roof_height - first)
    if not any(profile):
        raise BuildingProfileError(
            f"{context}: pixel (0, 0): roof layer has no separable silhouette")
    if max(profile, default=0) > 255:
        raise BuildingProfileError(f"{context}: profile exceeds uint8_t range")
    return tuple(profile)
