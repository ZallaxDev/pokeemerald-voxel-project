#!/usr/bin/env python3
"""Compose Emerald map art and extract deterministic building roof profiles."""

from __future__ import annotations

import json
import struct
import zlib
from collections import deque
from dataclasses import dataclass
from pathlib import Path


PRIMARY_METATILE_COUNT = 0x200
TILE_SIZE = 8
METATILE_SIZE = 16


class BuildingProfileError(ValueError):
    pass


@dataclass(frozen=True)
class PixelSource:
    metatile_id: int
    layer: int
    subtile: int
    tile: int
    palette: int
    color_index: int
    u: int
    v: int
    hflip: bool
    vflip: bool


@dataclass(frozen=True)
class Pixel:
    rgba: tuple[int, int, int, int]
    source: PixelSource | None

    @property
    def visible(self) -> bool:
        return self.source is not None and self.source.color_index != 0


TRANSPARENT_PIXEL = Pixel((0, 0, 0, 0), None)


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


def _decode_indexed_png(path: Path) -> tuple[int, int, tuple[int, ...]]:
    raw = path.read_bytes()
    if raw[:8] != b"\x89PNG\r\n\x1a\n":
        raise BuildingProfileError(f"{path}: not a PNG")
    position = 8
    width = height = depth = color_type = None
    packed = bytearray()
    while position < len(raw):
        length = struct.unpack_from(">I", raw, position)[0]
        kind = raw[position + 4:position + 8]
        payload = raw[position + 8:position + 8 + length]
        position += 12 + length
        if kind == b"IHDR":
            width, height, depth, color_type, compression, filtering, interlace = struct.unpack(
                ">IIBBBBB", payload)
            if color_type != 3 or depth not in (4, 8) or compression or filtering or interlace:
                raise BuildingProfileError(f"{path}: unsupported indexed PNG format")
        elif kind == b"IDAT":
            packed.extend(payload)
        elif kind == b"IEND":
            break
    if width is None or height is None or depth is None:
        raise BuildingProfileError(f"{path}: missing PNG header")
    try:
        packed = bytearray(zlib.decompress(packed))
    except zlib.error as error:
        raise BuildingProfileError(f"{path}: invalid PNG data: {error}") from error
    row_bytes = (width * depth + 7) // 8
    stride = row_bytes + 1
    if len(packed) != stride * height:
        raise BuildingProfileError(f"{path}: invalid PNG row data")
    previous = bytearray(row_bytes)
    pixels: list[int] = []
    for y in range(height):
        mode = packed[y * stride]
        source = packed[y * stride + 1:(y + 1) * stride]
        row = bytearray(row_bytes)
        for x, value in enumerate(source):
            left = row[x - 1] if x else 0
            up = previous[x]
            upper_left = previous[x - 1] if x else 0
            if mode == 0:
                prediction = 0
            elif mode == 1:
                prediction = left
            elif mode == 2:
                prediction = up
            elif mode == 3:
                prediction = (left + up) // 2
            elif mode == 4:
                estimate = left + up - upper_left
                candidates = (left, up, upper_left)
                prediction = candidates[min(range(3), key=lambda i: abs(estimate - candidates[i]))]
            else:
                raise BuildingProfileError(f"{path}: unsupported PNG filter {mode}")
            row[x] = (value + prediction) & 0xFF
        if depth == 8:
            pixels.extend(row[:width])
        else:
            expanded: list[int] = []
            for value in row:
                expanded.extend((value >> 4, value & 0xF))
            pixels.extend(expanded[:width])
        previous = row
    return width, height, tuple(pixels)


def _read_palettes(path: Path) -> tuple[tuple[tuple[int, int, int, int], ...], ...]:
    palettes = []
    for index in range(16):
        palette_path = path / f"{index:02}.gbapal"
        raw = palette_path.read_bytes()
        if len(raw) < 32:
            raise BuildingProfileError(f"{palette_path}: palette is shorter than 16 colors")
        colors = []
        for color_index, (value,) in enumerate(struct.iter_unpack("<H", raw[:32])):
            alpha = 0 if color_index == 0 else 255
            colors.append(((value & 31) * 255 // 31, ((value >> 5) & 31) * 255 // 31,
                           ((value >> 10) & 31) * 255 // 31, alpha))
        palettes.append(tuple(colors))
    return tuple(palettes)


class TilesetComposer:
    def __init__(self, primary_root: Path, secondary_root: Path):
        self._primary = self._load_tileset(primary_root)
        self._secondary = self._load_tileset(secondary_root)

    @staticmethod
    def _load_tileset(root: Path) -> tuple:
        width, height, pixels = _decode_indexed_png(root / "tiles.png")
        if width % TILE_SIZE or height % TILE_SIZE:
            raise BuildingProfileError(f"{root / 'tiles.png'}: tile sheet dimensions must be multiples of 8")
        raw = (root / "metatiles.bin").read_bytes()
        if len(raw) % 16:
            raise BuildingProfileError(f"{root / 'metatiles.bin'}: invalid metatile table size")
        metatiles = tuple(struct.iter_unpack("<8H", raw))
        return width, height, pixels, _read_palettes(root / "palettes"), metatiles

    def compose_metatile(self, metatile_id: int) -> tuple[tuple[Pixel, ...], tuple[Pixel, ...], tuple[Pixel, ...]]:
        selected = self._primary if metatile_id < PRIMARY_METATILE_COUNT else self._secondary
        local_id = metatile_id if metatile_id < PRIMARY_METATILE_COUNT else metatile_id - PRIMARY_METATILE_COUNT
        entries = selected[4]
        if local_id < 0 or local_id >= len(entries):
            raise BuildingProfileError(f"metatile 0x{metatile_id:X} is outside its tileset")
        layers: list[tuple[Pixel, ...]] = []
        for layer in range(2):
            output = [TRANSPARENT_PIXEL] * (METATILE_SIZE * METATILE_SIZE)
            for subtile in range(4):
                value = entries[local_id][layer * 4 + subtile]
                tile = value & 0x3FF
                hflip = bool(value & 0x400)
                vflip = bool(value & 0x800)
                palette = value >> 12
                tile_data = self._secondary if tile >= PRIMARY_METATILE_COUNT else self._primary
                source_tile = tile - PRIMARY_METATILE_COUNT if tile >= PRIMARY_METATILE_COUNT else tile
                tile_width, tile_height, tile_pixels, _, _ = tile_data
                tiles_per_row = tile_width // TILE_SIZE
                source_x = (source_tile % tiles_per_row) * TILE_SIZE
                source_y = (source_tile // tiles_per_row) * TILE_SIZE
                # Emerald reserves palettes 0..5 for the primary tileset.
                palettes = self._primary[3] if palette < 6 else selected[3]
                for py in range(TILE_SIZE):
                    for px in range(TILE_SIZE):
                        sx = source_x + (TILE_SIZE - 1 - px if hflip else px)
                        sy = source_y + (TILE_SIZE - 1 - py if vflip else py)
                        # Unpopulated VRAM slots referenced by static metatiles are blank.
                        color_index = (tile_pixels[sy * tile_width + sx]
                                       if sy < tile_height else 0)
                        dx = (subtile % 2) * TILE_SIZE + px
                        dy = (subtile // 2) * TILE_SIZE + py
                        source = PixelSource(metatile_id, layer, subtile, tile, palette,
                                             color_index, sx - source_x, sy - source_y,
                                             hflip, vflip)
                        rgba = palettes[palette][color_index]
                        output[dy * METATILE_SIZE + dx] = Pixel(rgba, source)
            layers.append(tuple(output))
        full = tuple(foreground if foreground.visible else base
                     for base, foreground in zip(layers[0], layers[1]))
        return layers[0], layers[1], full


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
    symbol_paths = {
        "gTileset_General": "primary/general",
        "gTileset_Petalburg": "secondary/petalburg",
        "gTileset_Building": "primary/building",
        "gTileset_BrendansMaysHouse": "secondary/brendans_mays_house",
        "gTileset_Lab": "secondary/lab",
    }
    try:
        primary_root = root / "data/tilesets" / symbol_paths[layout["primary_tileset"]]
        secondary_root = root / "data/tilesets" / symbol_paths[layout["secondary_tileset"]]
    except KeyError as error:
        raise BuildingProfileError(f"{map_symbol}: unsupported tileset {error.args[0]}") from error
    composer = TilesetComposer(primary_root, secondary_root)
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
