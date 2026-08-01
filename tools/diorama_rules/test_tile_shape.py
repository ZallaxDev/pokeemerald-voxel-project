#!/usr/bin/env python3

import copy
import random
import unittest

from profiles import ART_MODES, SEMANTIC_POOLS, ProfileError
from tile_shape import ClassificationError, TileShapeClassifier


def cell(metatile=1, behavior="MB_NORMAL", **values):
    return {"tileset": "gTileset_Test", "metatile": metatile,
            "localMetatile": metatile, "behavior": behavior,
            "collision": 0, "elevation": 0, "layerType": "normal", **values}


class TileShapeTests(unittest.TestCase):
    def test_precedence_context_pin_behavior_fallback(self):
        context = {"id": "context", "priority": 4,
                   "selector": {"metatiles": [1], "neighbors": {"n": {"metatile": 9}}},
                   "action": {"class": "counter", "pool": "furniture"}}
        pin = {"tileset": "gTileset_Test", "metatile": 1,
               "action": {"class": "wall", "pool": "structure"}}
        classifier = TileShapeClassifier(contextual_rules=[context], tileset_pins=[pin])
        self.assertEqual(classifier.classify_cell(cell(), neighbors={"n": cell(9)})["class"],
                         "counter")
        self.assertEqual(classifier.classify_cell(cell())["class"], "wall")
        self.assertEqual(TileShapeClassifier().classify_cell(cell(2, "MB_POND_WATER"))["class"],
                         "water")
        self.assertEqual(TileShapeClassifier().classify_cell(cell(2))["class"], "ground")

    def test_reused_tile_has_two_contextual_meanings_with_when_above(self):
        rules = [
            {"id": "as-counter", "selector": {"metatiles": [1],
                                                "when_above": {"metatile": 8}},
             "action": {"class": "counter", "pool": "furniture"}},
            {"id": "as-wall", "selector": {"metatiles": [1],
                                             "when_above": {"metatile": 9}},
             "action": {"class": "wall", "pool": "structure"}},
        ]
        classifier = TileShapeClassifier(contextual_rules=rules)
        cells = [cell(8), cell(9), cell(1), cell(1)]
        result = classifier.classify_layout(cells, 2, 2)
        self.assertEqual([result[2]["class"], result[3]["class"]], ["counter", "wall"])

    def test_collision_elevation_and_layer_never_invent_wall(self):
        result = TileShapeClassifier().classify_cell(
            cell(3, "MB_NORMAL", collision=3, elevation=15, layerType="split"))
        self.assertEqual(result["class"], "ground")
        self.assertEqual(result["source"], "fallback:flat")
        self.assertTrue(result["ambiguity"])
        self.assertIn("collision:3", result["evidence"])

    def test_rule_and_json_list_order_do_not_change_result(self):
        rules = [
            {"id": "lower", "priority": 1, "selector": {"metatiles": [1]},
             "action": {"class": "ledge", "pool": "terrain"}},
            {"id": "higher", "priority": 2, "selector": {"metatiles": [1]},
             "action": {"class": "counter", "pool": "furniture"}},
        ]
        expected = TileShapeClassifier(contextual_rules=rules).classify_cell(cell()).as_dict()
        shuffled = copy.deepcopy(rules)
        random.Random(7).shuffle(shuffled)
        self.assertEqual(TileShapeClassifier(contextual_rules=shuffled).classify_cell(cell()).as_dict(),
                         expected)
        conflicting = copy.deepcopy(rules)
        conflicting[0]["priority"] = 2
        with self.assertRaisesRegex(ClassificationError, "conflicting equal-tier"):
            TileShapeClassifier(contextual_rules=conflicting).classify_cell(cell())

    def test_manual_and_automatic_pending_prop_ground(self):
        manual = [{"tileset": "gTileset_Test", "metatile": 1,
                   "action": {"class": "cutout", "pool": "prop",
                              "groundPolicy": {"mode": "manual", "metatile": 7}}}]
        result = TileShapeClassifier(tileset_pins=manual).classify_cell(cell())
        self.assertEqual(result["propGround"], {"mode": "manual", "metatile": 7})
        pending = TileShapeClassifier().classify_cell(cell())
        self.assertEqual(pending["propGround"], {"mode": "automatic-pending"})

    def test_reliable_behavior_classes_and_nominal_heights(self):
        cases = {
            "MB_POND_WATER": ("water", -0.125),
            "MB_WATERFALL": ("waterfall", -0.125),
            "MB_TALL_GRASS": ("grass", 0.0),
            "MB_ANIMATED_FLOWERS": ("flower", 0.0),
            "MB_JUMP_NORTH": ("ledge", 0.375),
            "MB_JUMP_SOUTHEAST": ("ledge", 0.375),
            "MB_STAIRS_OUTSIDE_ABANDONED_SHIP": ("stairs", 1.0),
        }
        for behavior, expected in cases.items():
            with self.subTest(behavior=behavior):
                result = TileShapeClassifier().classify_cell(cell(2, behavior))
                self.assertEqual((result["class"], result["height"]), expected)
                self.assertIn(result["artMode"], ART_MODES)
                self.assertIn(result["pool"], SEMANTIC_POOLS)

    def test_ambiguous_behavior_is_flat_and_recorded(self):
        result = TileShapeClassifier().classify_cell(cell(4, "MB_SECRET_BASE_WALL"))
        self.assertEqual((result["class"], result["height"], result["artMode"]),
                         ("ground", 0.0, "flat"))
        self.assertEqual(result["confidence"], 0.0)
        self.assertTrue(result["ambiguity"])

    def test_records_are_immutable_and_vocabularies_are_closed(self):
        result = TileShapeClassifier().classify_cell(cell())
        with self.assertRaises((AttributeError, TypeError)):
            result.height = 4
        with self.assertRaises(ProfileError):
            TileShapeClassifier(default_action={"class": "ground", "pool": "misc"}).classify_cell(cell())


if __name__ == "__main__":
    unittest.main()
