#!/usr/bin/env python3

import unittest
from pathlib import Path

from compile_rules import RuleError, compile_data, parse_definition, render_c


class DioramaRuleCompilerTests(unittest.TestCase):
    def test_repository_rules_are_deterministic(self):
        root = Path(__file__).resolve().parents[2]
        first = compile_data(root)
        second = compile_data(root)
        self.assertEqual(first, second)
        self.assertEqual(render_c(first), render_c(second))
        self.assertEqual(len(first["map_rules"]), 2)
        self.assertEqual(len(first["map_overrides"]), 5)
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


if __name__ == "__main__":
    unittest.main()
