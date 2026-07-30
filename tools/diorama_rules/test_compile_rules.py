#!/usr/bin/env python3

import unittest
from pathlib import Path

from building_profiles import (ComposedFootprint, Pixel, PixelSource, TilesetComposer,
                               TRANSPARENT_PIXEL, compose_reference, extract_roof_profile)
from compile_rules import (RuleError, array, compile_data, parse_camera,
                           parse_definition, parse_pixel_profile, render_c)
from export_editor_map import build_editor_document


class DioramaRuleCompilerTests(unittest.TestCase):
    def test_repository_rules_are_deterministic(self):
        root = Path(__file__).resolve().parents[2]
        first = compile_data(root)
        second = compile_data(root)
        self.assertEqual(first, second)
        self.assertEqual(render_c(first), render_c(second))
        self.assertEqual(len(first["map_rules"]), 7)
        self.assertEqual(len(first["map_overrides"]), 68)
        self.assertEqual(len(first["placements"]), 3)
        inferred = {metatile: rule[3] for _, metatile, rule in first["tileset_rules"]}
        self.assertEqual(inferred[0x1C6], 0x00D)
        self.assertEqual(inferred[0x1C7], 0x00D)
        self.assertEqual(inferred[0x1CE], 0x001)
        self.assertEqual(inferred[0x1CF], 0x001)

    def test_rejects_unknown_shape(self):
        with self.assertRaises(RuleError):
            parse_definition({"shape": "sphere"}, "test")

    def test_rejects_invalid_height(self):
        with self.assertRaises(RuleError):
            parse_definition({"shape": "cutout", "height": 0}, "test")
        with self.assertRaises(RuleError):
            parse_definition({"shape": "flat", "groundHeight": 20}, "test")

    def test_rejects_unknown_fields(self):
        with self.assertRaises(RuleError):
            parse_definition({"shape": "flat", "typo": 1}, "test")

    def test_rejects_invalid_face_material(self):
        with self.assertRaises(RuleError):
            parse_definition({"shape": "flat", "faces": {"bottom": "none"}}, "test")
        with self.assertRaises(RuleError):
            parse_definition({"shape": "flat", "faces": {"north": {"layer": "glow"}}}, "test")

    def test_camera_profile_is_editor_friendly(self):
        profile, pitch, focal = parse_camera(
            {"profile": "interior", "pitch": 55, "focalLength": 150}, "test")
        self.assertEqual(profile, "DIORAMA_CAMERA_INTERIOR")
        self.assertAlmostEqual(pitch, 0.959931, places=5)
        self.assertEqual(focal, 150)

    def test_rejects_invalid_camera(self):
        with self.assertRaises(RuleError):
            parse_camera({"profile": "cinematic"}, "test")
        with self.assertRaises(RuleError):
            parse_camera({"profile": "interior", "pitch": 90}, "test")

    def test_rejects_non_array_editor_collections(self):
        with self.assertRaisesRegex(RuleError, "must be an array"):
            array(None, "test.overrides")

    def test_pixel_profile_south_facade(self):
        profile = {
            "version": 1,
            "facades": {"south": {
                "mode": "footprint-rows", "rows": 2,
                "unitHeight": 1.0, "fit": "natural",
            }},
        }
        self.assertEqual(parse_pixel_profile(profile, "test", 5, 5, 3),
                         (2, "DIORAMA_BUILDING_FACADE_FIT_NATURAL", 1.0,
                          0, 0, None, None, ()))

    def test_rejects_invalid_pixel_profile_facades(self):
        base = {"version": 1, "facades": {"south": {
            "mode": "footprint-rows", "rows": 2,
            "unitHeight": 1.0, "fit": "natural",
        }}}
        for field, value in (("rows", 0), ("rows", 5), ("unitHeight", 0),
                             ("fit", "contain"), ("mode", "tiles")):
            candidate = {"version": 1, "facades": {"south": dict(base["facades"]["south"])}}
            candidate["facades"]["south"][field] = value
            with self.assertRaises(RuleError):
                parse_pixel_profile(candidate, "test", 5, 5, 3)
        with self.assertRaises(RuleError):
            parse_pixel_profile({"version": 1, "facades": {"north": {}}}, "test", 5, 5, 3)
        with self.assertRaises(RuleError):
            parse_pixel_profile({"version": 1, "recesses": {}}, "test", 5, 5, 3)

    def test_pixel_profile_reference_roof_schema(self):
        profile = {
            "version": 1,
            "reference": {"map": "MAP_TEST", "x": 1, "y": 2},
            "roof": {
                "mode": "pixel-silhouette", "layer": "full", "slabPixels": 2,
                "eaves": {"north": 0, "east": 1, "south": 2, "west": 3},
                "seal": [{"x": 0, "y": 0}],
            },
        }
        parsed = parse_pixel_profile(profile, "test", 5, 5, 3)
        self.assertEqual(parsed[3:5], (2, 2))
        self.assertEqual(parsed[5], {"map": "MAP_TEST", "x": 1, "y": 2})
        self.assertEqual(parsed[6:], ("full", ((0, 0),)))
        invalid = dict(profile)
        invalid["roof"] = dict(profile["roof"], slabPixels=-1)
        with self.assertRaises(RuleError):
            parse_pixel_profile(invalid, "test", 5, 5, 3)
        invalid = dict(profile)
        invalid["roof"] = dict(profile["roof"], seal=[{"x": 80, "y": 0}])
        with self.assertRaises(RuleError):
            parse_pixel_profile(invalid, "test", 5, 5, 3)

    def test_flood_fill_profile_and_seal(self):
        visible = Pixel((1, 2, 3, 255), PixelSource(1, 0, 0, 0, 0, 1, 0, 0, False, False))
        pixels = [TRANSPARENT_PIXEL] * 64
        for x, y in ((1, 1), (2, 1), (1, 2), (2, 2)):
            pixels[y * 4 + x] = visible
        image = ComposedFootprint(4, 16, (1,), tuple(pixels), tuple(pixels), tuple(pixels))
        self.assertEqual(extract_roof_profile(image, 1, "full"), (0, 15, 15, 0, 0))
        self.assertEqual(extract_roof_profile(image, 1, "full", ((0, 1),)),
                         (15, 15, 15, 0, 0))

    def test_repository_reference_profiles(self):
        root = Path(__file__).resolve().parents[2]
        house = compose_reference(root, "MAP_LITTLEROOT_TOWN", 2, 4, 5, 5)
        lab = compose_reference(root, "MAP_LITTLEROOT_TOWN", 3, 12, 7, 5)
        self.assertEqual(house.metatiles[:5], (0x208, 0x209, 0x209, 0x209, 0x20A))
        house_profile = extract_roof_profile(house, 3, "foreground")
        lab_profile = extract_roof_profile(lab, 3, "foreground")
        self.assertEqual(house_profile,
                         (30, 31, 31, 31, 31, 31, 31, 30,
                          46, 47, 47, 47, 47, 47, 47, 46,
                          46, 46, 47, 47, 47, 47, 47, 46,
                          46, 46, 47, 47, 47, 47, 47, 46,
                          46, 46, 47, 47, 47, 47, 47, 46,
                          46, 46, 47, 47, 47, 47, 47, 46,
                          46, 46, 47, 47, 47, 47, 47, 46,
                          46, 46, 47, 47, 47, 47, 47, 46,
                          46, 46, 47, 47, 47, 47, 47, 46,
                          30, 30, 31, 31, 31, 31, 31, 30, 30))
        self.assertEqual(lab_profile, (47,) * 113)

    def test_composition_uses_both_tilesets_flips_layers_and_index_zero(self):
        root = Path(__file__).resolve().parents[2]
        house = compose_reference(root, "MAP_LITTLEROOT_TOWN", 2, 4, 5, 5)
        sources = [pixel.source for layer in (house.base, house.foreground)
                   for pixel in layer if pixel.source is not None]
        self.assertTrue(any(source.tile < 0x200 for source in sources))
        self.assertTrue(any(source.tile >= 0x200 for source in sources))
        composer = TilesetComposer(root / "data/tilesets/primary/general",
                                   root / "data/tilesets/secondary/petalburg")
        primary = composer.compose_metatile(0)
        secondary = composer.compose_metatile(0x200)
        self.assertTrue(all(pixel.source.metatile_id == 0 for layer in primary[:2]
                            for pixel in layer if pixel.source is not None))
        self.assertTrue(all(pixel.source.metatile_id == 0x200 for layer in secondary[:2]
                            for pixel in layer if pixel.source is not None))
        hflipped = [pixel.source for layer in composer.compose_metatile(0xB)[:2]
                    for pixel in layer if pixel.source is not None]
        vflipped = [pixel.source for layer in composer.compose_metatile(0xC9)[:2]
                    for pixel in layer if pixel.source is not None]
        self.assertTrue(any(source.hflip and source.u == 7 for source in hflipped))
        self.assertTrue(any(source.vflip and source.v == 7 for source in vflipped))
        self.assertTrue(any(source.color_index == 0 for source in sources))
        self.assertTrue(all(not pixel.visible and pixel.rgba[3] == 0
                            for pixel in house.foreground
                            if pixel.source is not None and pixel.source.color_index == 0))
        for base, foreground, full in zip(house.base, house.foreground, house.full):
            self.assertEqual(full, foreground if foreground.visible else base)

    def test_reference_outside_map_is_explicit(self):
        root = Path(__file__).resolve().parents[2]
        with self.assertRaisesRegex(ValueError, "outside MAP_LITTLEROOT_TOWN"):
            compose_reference(root, "MAP_LITTLEROOT_TOWN", 19, 19, 5, 5)

    def test_editor_export_uses_authoritative_map_data(self):
        root = Path(__file__).resolve().parents[2]
        document = build_editor_document(root, "MAP_LITTLEROOT_TOWN_BRENDANS_HOUSE_1F")
        self.assertEqual(document["schemaVersion"], 1)
        self.assertEqual(document["map"]["width"], 11)
        self.assertEqual(document["map"]["height"], 9)
        self.assertEqual(len(document["cells"]), 99)
        self.assertEqual(document["rules"]["camera"]["profile"], "interior")
        self.assertEqual(document["tilesets"]["primary"]["symbol"], "gTileset_Building")


if __name__ == "__main__":
    unittest.main()
