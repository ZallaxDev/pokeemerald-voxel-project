#!/usr/bin/env python3

import unittest
from pathlib import Path

from compile_rules import RuleError, array, compile_data, parse_camera, parse_definition, render_c
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
