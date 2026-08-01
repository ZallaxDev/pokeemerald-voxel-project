#!/usr/bin/env python3
"""Cross-language goldens for every logical Emerald tileset."""

from __future__ import annotations

import argparse
import struct
import subprocess
import tempfile
import zlib
from pathlib import Path

from catalog import load_tilesets
from emerald_compositor import (EmeraldCompositorError, TilesetComposer,
                                decode_indexed_png, load_animation_slots)


ROOT = Path(__file__).resolve().parents[2]
TILE_BYTES = 32
TILES_PER_ROLE = 0x200
PROVENANCE_BYTES = 9


def _png_tiles(path: Path) -> tuple[bytes, int]:
    width, height, pixels = decode_indexed_png(path)
    output = bytearray()
    for tile_y in range(0, height, 8):
        for tile_x in range(0, width, 8):
            for y in range(8):
                for x in range(0, 8, 2):
                    low = pixels[(tile_y + y) * width + tile_x + x]
                    high = pixels[(tile_y + y) * width + tile_x + x + 1]
                    if low > 15 or high > 15:
                        raise AssertionError(f"{path}: 4bpp source contains an index above 15")
                    output.append(low | high << 4)
    return bytes(output), width * height // 64


def _padded_graphics(root: Path) -> bytes:
    png, capacity = _png_tiles(root / "tiles.png")
    packed = (root / "tiles.4bpp").read_bytes()
    if len(packed) % TILE_BYTES:
        raise AssertionError(f"{root}/tiles.4bpp is not tile-aligned")
    if png[:len(packed)] != packed or any(png[len(packed):]):
        raise AssertionError(f"{root}: indexed PNG and generated 4bpp data differ")
    if capacity > TILES_PER_ROLE:
        raise AssertionError(f"{root}: more than {TILES_PER_ROLE} tiles")
    return packed.ljust(TILES_PER_ROLE * TILE_BYTES, b"\0")


def _runtime_palette(primary: Path, secondary: Path) -> bytes:
    values = [0] * 256
    for palette in range(6):
        values[palette * 16:(palette + 1) * 16] = struct.unpack(
            "<16H", (primary / "palettes" / f"{palette:02}.gbapal").read_bytes())
    for palette in range(6, 13):
        values[palette * 16:(palette + 1) * 16] = struct.unpack(
            "<16H", (secondary / "palettes" / f"{palette:02}.gbapal").read_bytes())
    values[0] = 0
    return struct.pack("<256H", *values)


def _argb(rgba: tuple[int, int, int, int]) -> int:
    red, green, blue, alpha = rgba
    return alpha << 24 | red << 16 | green << 8 | blue


def _write_indexed_8bit_png(path: Path) -> None:
    def chunk(kind: bytes, data: bytes) -> bytes:
        return (struct.pack(">I", len(data)) + kind + data
                + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF))

    header = struct.pack(">IIBBBBB", 2, 1, 8, 3, 0, 0, 0)
    path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header)
                     + chunk(b"PLTE", b"\0\0\0\xff\xff\xff")
                     + chunk(b"IDAT", zlib.compress(b"\0\x01\0")) + chunk(b"IEND", b""))


def _validate_animation_contract(tilesets: dict, slots_by_callback: dict) -> None:
    if sum(len(slots) for slots in slots_by_callback.values()) != 75:
        raise AssertionError("expected 75 callback-owned animation destinations")
    mauville_gym = slots_by_callback["InitTilesetAnim_MauvilleGym"]
    if {slot.source for slot in mauville_gym} != {"gTilesetAnims_MauvilleGym_ElectricGates"}:
        raise AssertionError("Mauville Gym inherited unrelated Mauville animations")
    cave = slots_by_callback["InitTilesetAnim_Cave"]
    lavaridge = slots_by_callback["InitTilesetAnim_Lavaridge"]
    if not any(slot.source == "gTilesetAnims_Lavaridge_Cave_Lava" and slot.local_start == 416
               for slot in cave):
        raise AssertionError("Cave lava destination was not assigned to Cave")
    if not any(slot.source == "gTilesetAnims_Lavaridge_Cave_Lava" and slot.local_start == 160
               for slot in lavaridge):
        raise AssertionError("Lavaridge lava destination was not assigned to Lavaridge")

    general = tilesets["gTileset_General"]
    sootopolis_gym = tilesets["gTileset_SootopolisGym"]
    with tempfile.TemporaryDirectory() as temporary:
        metatiles = Path(temporary)
        metatiles.joinpath("metatiles.bin").write_bytes(struct.pack("<8H", *([0x200 + 496] * 8)))
        composer = TilesetComposer(
            ROOT / general.root, ROOT / sootopolis_gym.root,
            ROOT / general.metatiles_root, metatiles,
            slots_by_callback[general.callback], slots_by_callback[sootopolis_gym.callback],
            animation_frame=0, missing_policy="error")
        composer.compose_metatile(0x200)
        if composer.tile_status("secondary", 496) != "animated-frame":
            raise AssertionError("declared Sootopolis Gym frame did not fill its absent static slot")

    petalburg = tilesets["gTileset_Petalburg"]
    composer = TilesetComposer(
        ROOT / general.root, ROOT / petalburg.root,
        ROOT / general.metatiles_root, ROOT / petalburg.metatiles_root,
        slots_by_callback[general.callback], slots_by_callback[petalburg.callback],
        missing_policy="error")
    try:
        composer.compose_metatile(0x200 + 74)
    except EmeraldCompositorError as error:
        if "unresolved" not in str(error):
            raise
    else:
        raise AssertionError("strict composition accepted an unresolved Petalburg tile")


def run(driver: Path) -> None:
    tilesets = load_tilesets(ROOT)
    slots_by_callback = load_animation_slots(ROOT)
    _validate_animation_contract(tilesets, slots_by_callback)
    with tempfile.TemporaryDirectory() as temporary:
        temporary_path = Path(temporary)
        png8 = temporary_path / "indexed8.png"
        _write_indexed_8bit_png(png8)
        if decode_indexed_png(png8) != (2, 1, (1, 0)):
            raise AssertionError("8-bit indexed PNG decoding failed")

        layouts = __import__("json").loads(
            (ROOT / "data/layouts/layouts.json").read_text(encoding="utf-8"))["layouts"]
        contexts = []
        for symbol, info in sorted(tilesets.items()):
            if info.role == "primary":
                layout = next((item for item in layouts if item["primary_tileset"] == symbol), None)
                secondary_symbol = layout["secondary_tileset"] if layout else "gTileset_Petalburg"
                primary_symbol = symbol
            else:
                layout = next((item for item in layouts if item["secondary_tileset"] == symbol), None)
                primary_symbol = layout["primary_tileset"] if layout else "gTileset_General"
                secondary_symbol = symbol
            primary = tilesets[primary_symbol]
            secondary = tilesets[secondary_symbol]
            composer = TilesetComposer(
                ROOT / primary.root, ROOT / secondary.root,
                ROOT / primary.metatiles_root, ROOT / secondary.metatiles_root,
                slots_by_callback.get(primary.callback, ()),
                slots_by_callback.get(secondary.callback, ()))
            is_secondary = info.role == "secondary"
            start = 0x200 if is_secondary else 0
            records = tuple(range(start, start + composer.metatile_count(is_secondary)))
            contexts.append((symbol, info, primary, secondary, composer, records))

        fixture = temporary_path / "input.bin"
        result = temporary_path / "output.bin"
        with fixture.open("wb") as output:
            output.write(struct.pack("<I", len(contexts)))
            for _, _, primary, secondary, _, records in contexts:
                output.write(struct.pack("<I", len(records)))
                output.write(_padded_graphics(ROOT / primary.root))
                output.write(_padded_graphics(ROOT / secondary.root))
                output.write(_runtime_palette(ROOT / primary.root, ROOT / secondary.root))
                metatile_root = ROOT / (secondary.metatiles_root if records[0] >= 0x200
                                        else primary.metatiles_root)
                entries = (metatile_root / "metatiles.bin").read_bytes()
                for metatile_id in records:
                    local_id = metatile_id - (0x200 if metatile_id >= 0x200 else 0)
                    output.write(struct.pack("<H", metatile_id))
                    output.write(entries[local_id * 16:(local_id + 1) * 16])
        subprocess.run([str(driver), str(fixture), str(result)], check=True)

        compared = 0
        with result.open("rb") as source:
            for symbol, info, _, _, composer, records in contexts:
                for metatile_id in records:
                    actual_id_raw = source.read(2)
                    if len(actual_id_raw) != 2 or struct.unpack("<H", actual_id_raw)[0] != metatile_id:
                        raise AssertionError(f"{symbol}: corrupt C golden stream")
                    layers = composer.compose_metatile(metatile_id)
                    for layer_index in range(2):
                        actual_pixels = struct.unpack("<256I", source.read(256 * 4))
                        expected_pixels = tuple(_argb(pixel.rgba) for pixel in layers[layer_index])
                        if actual_pixels != expected_pixels:
                            pixel = next(index for index, values in enumerate(
                                zip(actual_pixels, expected_pixels)) if values[0] != values[1])
                            raise AssertionError(
                                f"{symbol} metatile 0x{metatile_id:X} layer {layer_index} "
                                f"pixel ({pixel % 16},{pixel // 16}): C={actual_pixels[pixel]:08X} "
                                f"Python={expected_pixels[pixel]:08X}")
                        provenance = source.read(256 * PROVENANCE_BYTES)
                        if len(provenance) != 256 * PROVENANCE_BYTES:
                            raise AssertionError("truncated C provenance stream")
                        for pixel_index, pixel in enumerate(layers[layer_index]):
                            values = provenance[pixel_index * PROVENANCE_BYTES:
                                                (pixel_index + 1) * PROVENANCE_BYTES]
                            subtile, tile_low, tile_high, palette, color, u, v, hflip, vflip = values
                            expected = pixel.source
                            actual = (subtile, tile_low | tile_high << 8, palette, color,
                                      u, v, bool(hflip), bool(vflip))
                            wanted = (expected.subtile, expected.tile, expected.palette,
                                      expected.color_index, expected.u, expected.v,
                                      expected.hflip, expected.vflip)
                            if actual != wanted:
                                raise AssertionError(
                                    f"{symbol} metatile 0x{metatile_id:X} layer {layer_index} "
                                    f"pixel {pixel_index}: C provenance {actual}, Python {wanted}")
                    actual_full = struct.unpack("<256I", source.read(256 * 4))
                    expected_full = tuple(_argb(pixel.rgba) if pixel.visible else 0
                                          for pixel in layers[2])
                    if actual_full != expected_full:
                        raise AssertionError(
                            f"{symbol} metatile 0x{metatile_id:X}: C/Python full composition differs")
                    compared += 1
            if source.read(1):
                raise AssertionError("trailing C golden output")
    if compared != 18318:
        raise AssertionError(f"expected 18318 logical metatiles, compared {compared}")
    compressed = sum(info.compressed for info in tilesets.values())
    print(f"compositor goldens passed: {compared} metatiles, 75 tilesets, "
          f"{compressed} compressed and {75 - compressed} uncompressed")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--driver", type=Path, required=True)
    args = parser.parse_args()
    run(args.driver.resolve())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
