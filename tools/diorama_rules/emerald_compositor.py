#!/usr/bin/env python3
"""Canonical dependency-free Emerald metatile compositor with pixel provenance."""

from __future__ import annotations

import re
import struct
import zlib
from dataclasses import dataclass
from pathlib import Path


PRIMARY_METATILE_COUNT = 0x200
TILE_SIZE = 8
METATILE_SIZE = 16


class EmeraldCompositorError(ValueError):
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
class AnimationSlot:
    callback: str
    source: str
    role: str
    local_start: int
    tile_count: int
    frames: tuple[tuple[Path, ...], ...]


def _function_bodies(text: str) -> dict[str, str]:
    bodies = {}
    pattern = re.compile(r"(?:static\s+)?void\s+([A-Za-z0-9_]+)\s*\([^;]*?\)\s*\{")
    for match in pattern.finditer(text):
        depth = 1
        position = match.end()
        while position < len(text) and depth:
            if text[position] == "{":
                depth += 1
            elif text[position] == "}":
                depth -= 1
            position += 1
        if depth:
            raise EmeraldCompositorError(f"unterminated function {match.group(1)} in tileset_anims.c")
        bodies[match.group(1)] = text[match.end():position - 1]
    return bodies


def load_animation_slots(root: Path) -> dict[str, tuple[AnimationSlot, ...]]:
    """Extract callback-owned tile writes and frame assets from tileset_anims.c."""
    text = (root / "src/tileset_anims.c").read_text(encoding="utf-8")
    frame_assets = {}
    for match in re.finditer(
            r"const\s+u16\s+(gTilesetAnims_[A-Za-z0-9_]+_Frame\d+)\[\]\s*=\s*"
            r"INCBIN_U16\((.*?)\);", text, re.S):
        paths = tuple(root / item for item in re.findall(r'"([^"]+\.4bpp)"', match.group(2)))
        if not paths:
            raise EmeraldCompositorError(f"{match.group(1)} has no frame assets")
        frame_assets[match.group(1)] = paths

    sequences = {}
    for match in re.finditer(
            r"const\s+u16\s*\*const\s+(gTilesetAnims_[A-Za-z0-9_]+)\[\]\s*=\s*\{(.*?)\};",
            text, re.S):
        symbols = re.findall(r"\b(gTilesetAnims_[A-Za-z0-9_]+_Frame\d+)\b", match.group(2))
        if symbols:
            try:
                sequences[match.group(1)] = tuple(frame_assets[symbol] for symbol in symbols)
            except KeyError as error:
                raise EmeraldCompositorError(
                    f"{match.group(1)} references undeclared frame {error.args[0]}") from error

    destinations = {}
    for match in re.finditer(
            r"u16\s*\*const\s+(gTilesetAnims_[A-Za-z0-9_]+_VDests)\[\]\s*=\s*\{(.*?)\};",
            text, re.S):
        destinations[match.group(1)] = tuple(
            ("secondary" if "NUM_TILES_IN_PRIMARY" in expression else "primary",
             int(re.findall(r"\d+", expression)[-1]))
            for expression in re.findall(r"TILE_OFFSET_4BPP\(([^)]+)\)", match.group(2)))

    bodies = _function_bodies(text)
    callbacks = {}
    for name, body in bodies.items():
        if name.startswith("InitTilesetAnim_"):
            target = re.search(
                r"s(?:Primary|Secondary)TilesetAnimCallback\s*=\s*(TilesetAnim_[A-Za-z0-9_]+)",
                body)
            callbacks[name] = target.group(1) if target else None
    calls = {name: set(re.findall(r"\b([A-Za-z_][A-Za-z0-9_]*)\s*\(", body)) & bodies.keys()
             for name, body in bodies.items()}
    append_pattern = re.compile(
        r"AppendTilesetAnimToBuffer\((gTilesetAnims_[A-Za-z0-9_]+)\[[^]]+\],\s*"
        r"(?:(?:\(u16\s*\*\)\(BG_VRAM\s*\+\s*TILE_OFFSET_4BPP\(([^)]+)\)\))|"
        r"(gTilesetAnims_[A-Za-z0-9_]+_VDests)\[[^]]+\]),\s*"
        r"(\d+)\s*\*\s*TILE_SIZE_4BPP\)")
    result = {}
    for callback, target in callbacks.items():
        reachable = set()
        pending = [target] if target else []
        while pending:
            function = pending.pop()
            if function in reachable or function not in bodies:
                continue
            reachable.add(function)
            pending.extend(calls[function] - reachable)
        slots = []
        for function in sorted(reachable):
            matches = tuple(append_pattern.finditer(bodies[function]))
            if len(matches) != bodies[function].count("AppendTilesetAnimToBuffer("):
                raise EmeraldCompositorError(
                    f"{function} contains an unsupported animation write expression")
            for match in matches:
                source, direct, destination_array, count_text = match.groups()
                if source not in sequences:
                    raise EmeraldCompositorError(f"{function} uses unknown animation sequence {source}")
                if direct:
                    targets = (("secondary" if "NUM_TILES_IN_PRIMARY" in direct else "primary",
                                int(re.findall(r"\d+", direct)[-1])),)
                else:
                    targets = destinations.get(destination_array, ())
                    if not targets:
                        raise EmeraldCompositorError(f"{function} uses unknown destinations {destination_array}")
                for role, local_start in targets:
                    slots.append(AnimationSlot(callback, source, role, local_start,
                                               int(count_text), sequences[source]))
        result[callback] = tuple(sorted(set(slots), key=lambda item: (
            item.role, item.local_start, item.source, item.tile_count)))
    return result


def decode_indexed_png(path: Path) -> tuple[int, int, tuple[int, ...]]:
    raw = path.read_bytes()
    if raw[:8] != b"\x89PNG\r\n\x1a\n":
        raise EmeraldCompositorError(f"{path}: not a PNG")
    position = 8
    width = height = depth = color_type = None
    packed = bytearray()
    while position < len(raw):
        if position + 12 > len(raw):
            raise EmeraldCompositorError(f"{path}: truncated PNG chunk")
        length = struct.unpack_from(">I", raw, position)[0]
        kind = raw[position + 4:position + 8]
        payload = raw[position + 8:position + 8 + length]
        position += 12 + length
        if kind == b"IHDR":
            width, height, depth, color_type, compression, filtering, interlace = struct.unpack(
                ">IIBBBBB", payload)
            if color_type != 3 or depth not in (4, 8) or compression or filtering or interlace:
                raise EmeraldCompositorError(f"{path}: unsupported indexed PNG format")
        elif kind == b"IDAT":
            packed.extend(payload)
        elif kind == b"IEND":
            break
    if width is None or height is None or depth is None:
        raise EmeraldCompositorError(f"{path}: missing PNG header")
    try:
        packed = bytearray(zlib.decompress(packed))
    except zlib.error as error:
        raise EmeraldCompositorError(f"{path}: invalid PNG data: {error}") from error
    row_bytes = (width * depth + 7) // 8
    stride = row_bytes + 1
    if len(packed) != stride * height:
        raise EmeraldCompositorError(f"{path}: invalid PNG row data")
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
                raise EmeraldCompositorError(f"{path}: unsupported PNG filter {mode}")
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


def expand_5bit(value: int) -> int:
    """Match the runtime compositor's BGR555 bit replication exactly."""
    return (value << 3) | (value >> 2)


def _decode_4bpp_tile(raw: bytes) -> tuple[int, ...]:
    if len(raw) != 32:
        raise EmeraldCompositorError("a 4bpp tile must contain exactly 32 bytes")
    return tuple(nibble for value in raw for nibble in (value & 0xF, value >> 4))


def read_palettes(path: Path) -> tuple[tuple[tuple[int, int, int, int], ...], ...]:
    palettes = []
    for index in range(16):
        palette_path = path / f"{index:02}.gbapal"
        raw = palette_path.read_bytes()
        if len(raw) < 32:
            raise EmeraldCompositorError(f"{palette_path}: palette is shorter than 16 colors")
        colors = []
        for color_index, (value,) in enumerate(struct.iter_unpack("<H", raw[:32])):
            colors.append((expand_5bit(value & 31), expand_5bit((value >> 5) & 31),
                           expand_5bit((value >> 10) & 31), 0 if color_index == 0 else 255))
        palettes.append(tuple(colors))
    return tuple(palettes)


def encode_rgba_png(width: int, height: int, pixels: bytes) -> bytes:
    if len(pixels) != width * height * 4:
        raise EmeraldCompositorError("RGBA buffer size does not match its dimensions")

    def chunk(kind: bytes, data: bytes) -> bytes:
        checksum = zlib.crc32(kind + data) & 0xFFFFFFFF
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", checksum)

    rows = b"".join(b"\0" + pixels[y * width * 4:(y + 1) * width * 4]
                    for y in range(height))
    header = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header)
            + chunk(b"IDAT", zlib.compress(rows, 9)) + chunk(b"IEND", b""))


class TilesetComposer:
    def __init__(self, primary_root: Path, secondary_root: Path,
                  primary_metatiles_root: Path | None = None,
                  secondary_metatiles_root: Path | None = None,
                  primary_animation_slots: tuple[AnimationSlot, ...] = (),
                  secondary_animation_slots: tuple[AnimationSlot, ...] = (),
                  animation_frame: int | None = None,
                  missing_policy: str = "transparent"):
        if missing_policy not in ("transparent", "error"):
            raise EmeraldCompositorError(f"unknown missing tile policy {missing_policy!r}")
        self._missing_policy = missing_policy
        self._animation_frame = animation_frame
        self._slots = {"primary": primary_animation_slots, "secondary": secondary_animation_slots}
        self._primary = self._load_tileset(primary_root, primary_metatiles_root,
                                           primary_animation_slots, animation_frame)
        self._secondary = self._load_tileset(secondary_root, secondary_metatiles_root,
                                             secondary_animation_slots, animation_frame)
        self.missing_tiles: set[tuple[str, int]] = set()
        self.missing_tile_references: set[tuple[str, int, int]] = set()
        self.missing_tile_details: set[tuple[str, int, int, str]] = set()

    @staticmethod
    def _load_tileset(root: Path, metatiles_root: Path | None,
                      animation_slots: tuple[AnimationSlot, ...],
                      animation_frame: int | None) -> tuple:
        metatiles_root = metatiles_root or root
        width, height, pixels = decode_indexed_png(root / "tiles.png")
        if width % TILE_SIZE or height % TILE_SIZE:
            raise EmeraldCompositorError(f"{root / 'tiles.png'}: dimensions must be multiples of 8")
        raw = (metatiles_root / "metatiles.bin").read_bytes()
        if len(raw) % 16:
            raise EmeraldCompositorError(
                f"{metatiles_root / 'metatiles.bin'}: invalid metatile table size")
        packed_path = root / "tiles.4bpp"
        png_tile_capacity = width * height // 64
        if packed_path.is_file() and packed_path.stat().st_size % 32:
            raise EmeraldCompositorError(f"{packed_path}: size is not a whole number of tiles")
        packed_tiles = packed_path.stat().st_size // 32 if packed_path.is_file() else png_tile_capacity
        pixels = list(pixels)
        if animation_frame is not None:
            for slot in animation_slots:
                paths = slot.frames[animation_frame % len(slot.frames)]
                frame = b"".join(path.read_bytes() for path in paths)
                expected = slot.tile_count * 32
                if len(frame) < expected:
                    raise EmeraldCompositorError(
                        f"{slot.source}: frame has {len(frame)} bytes, expected at least {expected}")
                for offset in range(slot.tile_count):
                    tile_pixels = _decode_4bpp_tile(frame[offset * 32:(offset + 1) * 32])
                    tile = slot.local_start + offset
                    # Offline composition stays on the static frame. Declared animation
                    # data only fills VRAM destinations absent from that static payload.
                    if tile < packed_tiles:
                        continue
                    tiles_per_row = width // TILE_SIZE
                    source_x = (tile % tiles_per_row) * TILE_SIZE
                    source_y = (tile // tiles_per_row) * TILE_SIZE
                    required_height = source_y + TILE_SIZE
                    if required_height > height:
                        pixels.extend([0] * (width * (required_height - height)))
                        height = required_height
                    for y in range(TILE_SIZE):
                        start = (source_y + y) * width + source_x
                        pixels[start:start + TILE_SIZE] = tile_pixels[y * TILE_SIZE:(y + 1) * TILE_SIZE]
        return (root, width, height, tuple(pixels), read_palettes(root / "palettes"),
                tuple(struct.iter_unpack("<8H", raw)), packed_tiles, png_tile_capacity)

    def tile_status(self, role: str, local_tile: int) -> str:
        data = self._secondary if role == "secondary" else self._primary
        if local_tile < data[6]:
            return "static"
        if any(slot.local_start <= local_tile < slot.local_start + slot.tile_count
               for slot in self._slots[role]):
            return "animated-frame" if self._animation_frame is not None else "animated-slot"
        if local_tile < data[7]:
            return "static-padding"
        return "unresolved"

    def metatile_count(self, secondary: bool) -> int:
        return len((self._secondary if secondary else self._primary)[5])

    def compose_metatile(self, metatile_id: int) -> tuple[tuple[Pixel, ...], tuple[Pixel, ...], tuple[Pixel, ...]]:
        selected = self._primary if metatile_id < PRIMARY_METATILE_COUNT else self._secondary
        local_id = metatile_id if metatile_id < PRIMARY_METATILE_COUNT else metatile_id - PRIMARY_METATILE_COUNT
        entries = selected[5]
        if local_id < 0 or local_id >= len(entries):
            raise EmeraldCompositorError(f"metatile 0x{metatile_id:X} is outside its tileset")
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
                root, tile_width, tile_height, tile_pixels, _, _, _, _ = tile_data
                role = "secondary" if tile >= PRIMARY_METATILE_COUNT else "primary"
                status = self.tile_status(role, source_tile)
                if status not in ("static", "animated-frame"):
                    self.missing_tiles.add((root.as_posix(), source_tile))
                    self.missing_tile_references.add((root.as_posix(), source_tile, metatile_id))
                    self.missing_tile_details.add((root.as_posix(), source_tile,
                                                   metatile_id, status))
                    if self._missing_policy == "error":
                        raise EmeraldCompositorError(
                            f"metatile 0x{metatile_id:X} references {status} "
                            f"{role} tile {source_tile} in {root}")
                tiles_per_row = tile_width // TILE_SIZE
                source_x = (source_tile % tiles_per_row) * TILE_SIZE
                source_y = (source_tile // tiles_per_row) * TILE_SIZE
                palettes = self._primary[4] if palette < 6 else selected[4]
                for py in range(TILE_SIZE):
                    for px in range(TILE_SIZE):
                        sx = source_x + (TILE_SIZE - 1 - px if hflip else px)
                        sy = source_y + (TILE_SIZE - 1 - py if vflip else py)
                        if sy >= tile_height:
                            color_index = 0
                        else:
                            color_index = tile_pixels[sy * tile_width + sx]
                        dx = (subtile % 2) * TILE_SIZE + px
                        dy = (subtile // 2) * TILE_SIZE + py
                        source = PixelSource(metatile_id, layer, subtile, tile, palette,
                                             color_index, sx - source_x, sy - source_y,
                                             hflip, vflip)
                        rgba = palettes[palette][color_index] if color_index else (0, 0, 0, 0)
                        output[dy * METATILE_SIZE + dx] = Pixel(rgba, source)
            layers.append(tuple(output))
        full = tuple(foreground if foreground.visible else base
                     for base, foreground in zip(layers[0], layers[1]))
        return layers[0], layers[1], full
