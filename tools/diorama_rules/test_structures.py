#!/usr/bin/env python3

import copy
import unittest

from profiles import shape_from_action
from structures import analyze_layout, overlay_rows


def shape(class_name="ground", *, pool="terrain", authored=False,
          art_mode=None, source="fallback", evidence=()):
    action = {"class": class_name, "pool": pool}
    if art_mode is not None:
        action["artMode"] = art_mode
    return shape_from_action(action, source=source, authored=authored,
                             confidence=1.0 if authored else 0.5, evidence=evidence)


def pattern(pattern_id, values, mask, priority=1):
    return {"id": pattern_id,
            "dimensions": {"width": len(values[0]), "height": len(values)},
            "cells": values, "claimMask": mask, "priority": priority,
            "action": {"class": "building", "pool": "structure"}}


def analyze(width, height, metatiles, shapes, *, patterns=(), pixels=None,
            doors=frozenset(), events=None, cells=None):
    layout = {"id": "L", "width": width, "height": height}
    cells = tuple(cells or ({"metatile": value} for value in metatiles))
    summaries = tuple(pixels or ({"resolved": False},) * len(cells))
    return analyze_layout(layout, cells, tuple(shapes), list(patterns), summaries,
                          doors, events)


class StructureTests(unittest.TestCase):
    def test_pattern_iteration_order_is_irrelevant(self):
        patterns = [pattern("low", [[1]], ["1"], 1),
                    pattern("high", [[1]], ["1"], 2)]
        first = analyze(1, 1, [1], [shape()], patterns=patterns)
        second = analyze(1, 1, [1], [shape()], patterns=reversed(copy.deepcopy(patterns)))
        self.assertEqual(first, second)
        self.assertEqual(first["candidates"][0]["owner"], "high")

    def test_overlapping_pattern_rejects_whole_placement_first_claim_wins(self):
        patterns = [pattern("first", [[1]], ["1"], 2),
                    pattern("overlap", [[1, 2]], ["11"], 1)]
        result = analyze(2, 1, [1, 2], [shape(), shape()], patterns=patterns)
        self.assertEqual(result["candidates"][0]["cells"], [0])
        self.assertFalse(any(candidate["owner"] == "overlap"
                             for candidate in result["candidates"]))
        self.assertIsNone(result["cells"][1]["owner"])

    def test_large_tree_claim_precedes_nested_small_tree(self):
        large = pattern("large", [[10, 11], [20, 21], [30, 31]],
                        ["11", "11", "11"], 2)
        large["action"] = {"class": "grouped-hull", "pool": "vegetation"}
        small = pattern("small", [[30, 31]], ["11"], 1)
        small["action"] = {"class": "grouped-hull", "pool": "vegetation"}
        result = analyze(2, 3, [10, 11, 20, 21, 30, 31], [shape()] * 6,
                         patterns=[small, large])
        owner = result["candidates"][0]
        self.assertEqual(owner["owner"], "large")
        self.assertEqual(owner["cells"], list(range(6)))
        self.assertEqual(owner["bbox"], {"x": 0, "y": 0, "width": 2, "height": 3})
        self.assertFalse(any(candidate["owner"] == "small"
                             for candidate in result["candidates"]))

    def test_repeated_tree_body_precedes_bottom_row_fallback(self):
        body = pattern("body", [[20, 21], [30, 31]], ["11", "11"], 2)
        body["action"] = {"class": "grouped-hull", "pool": "vegetation"}
        small = pattern("small", [[30, 31]], ["11"], 1)
        small["action"] = {"class": "grouped-hull", "pool": "vegetation"}
        result = analyze(2, 2, [20, 21, 30, 31], [shape()] * 4,
                         patterns=[small, body])
        self.assertEqual(result["candidates"][0]["owner"], "body")
        self.assertEqual(result["candidates"][0]["bbox"],
                         {"x": 0, "y": 0, "width": 2, "height": 2})
        self.assertFalse(any(candidate["owner"] == "small"
                             for candidate in result["candidates"]))

    def test_claimed_cell_is_excluded_from_later_detector(self):
        result = analyze(2, 1, [1, 2],
                         [shape("wall", pool="structure", art_mode="upright"),
                          shape("wall", pool="structure", art_mode="upright")],
                         patterns=[pattern("claim", [[1]], ["1"], 1)])
        generic = next(candidate for candidate in result["candidates"]
                       if candidate["kind"] == "generic-volume")
        self.assertEqual(generic["cells"], [1])

    def test_regions_never_merge_different_pools(self):
        result = analyze(2, 1, [1, 1],
                         [shape(pool="terrain"), shape(pool="water")])
        regions = [candidate for candidate in result["candidates"]
                   if candidate["kind"] == "region"]
        self.assertEqual([candidate["cells"] for candidate in regions], [[0], [1]])

    def test_diagonal_and_layout_bounds_do_not_connect(self):
        result = analyze(2, 2, [1, 9, 9, 1],
                         [shape(), shape(pool="water"), shape(pool="water"), shape()])
        terrain = [candidate["cells"] for candidate in result["candidates"]
                   if candidate["kind"] == "region" and candidate["pool"] == "terrain"]
        self.assertEqual(terrain, [[0], [3]])

    def test_void_transparent_black_and_authored_precedence(self):
        pixels = ({"resolved": True, "visible": 0, "transparent": 64, "black": 0},
                  {"resolved": True, "visible": 64, "transparent": 0, "black": 64},
                  {"resolved": True, "visible": 0, "transparent": 64, "black": 0})
        result = analyze(3, 1, [1, 2, 3],
                         [shape(), shape(), shape("wall", pool="structure", authored=True)],
                         pixels=pixels)
        self.assertEqual([cell["voidKind"] for cell in result["cells"]],
                         ["transparent", "black", "none"])
        self.assertEqual(result["candidates"][0]["class"], "wall")
        void_regions = [candidate for candidate in result["candidates"]
                        if candidate["class"] == "void"]
        self.assertEqual([candidate["cells"] for candidate in void_regions], [[0, 1]])

    def test_door_fold_is_conservative_and_does_not_claim(self):
        upright = shape("wall", pool="structure", art_mode="upright")
        ground = shape(pool="structure")
        result = analyze(3, 2, range(6),
                         [upright, shape("wall", pool="structure", authored=True,
                                         art_mode="upright"), shape("wall", pool="prop",
                                                                    art_mode="upright"),
                          ground, ground, ground], doors=frozenset((0, 3, 4, 5, 99)))
        self.assertEqual([cell["doorFold"] for cell in result["cells"]],
                         [False, False, False, True, False, False])
        self.assertIsNone(result["cells"][3]["owner"])

    def test_pixel_components_are_aggregated_not_used_as_connectivity(self):
        pixels = ({"resolved": True, "visible": 5, "transparent": 59,
                   "black": 1, "componentCount": 2},
                  {"resolved": True, "visible": 7, "transparent": 57,
                   "black": 2, "components": [1, 2, 3]})
        result = analyze(2, 1, [1, 1], [shape(), shape()], pixels=pixels)
        region = result["candidates"][0]
        self.assertEqual(region["cells"], [0, 1])
        self.assertEqual(region["pixels"], {"visible": 12, "transparent": 116,
                                             "black": 3, "components": 5})

    def test_background_event_claims_prop_before_regions(self):
        result = analyze(1, 1, [1], [shape()], events={
            0: {"class": "signpost", "pool": "prop", "source": "event:sign"},
        })
        self.assertEqual(result["candidates"][0]["kind"], "prop-candidate")
        self.assertEqual(result["candidates"][0]["class"], "signpost")
        self.assertEqual(result["cells"][0]["owner"], 1)

    def test_animation_metadata_prevents_derived_void(self):
        result = analyze(1, 1, [1], [shape()],
                         pixels=({"resolved": True, "visible": 0},),
                         cells=({"metatile": 1, "animationSources": ("flower",)},))
        self.assertEqual(result["cells"][0]["voidKind"], "none")

    def test_headless_overlays_expose_claim_region_and_source(self):
        result = analyze(2, 1, [1, 2], [shape(), shape()],
                         patterns=[pattern("claim", [[1]], ["1"])])
        self.assertEqual(overlay_rows(result, 2, "claim"), ((1, 0),))
        self.assertNotEqual(overlay_rows(result, 2, "region"), ((1, 0),))
        self.assertEqual(overlay_rows(result, 2, "source")[0][0], "pattern:claim")
        with self.assertRaises(ValueError):
            overlay_rows(result, 3, "claim")


if __name__ == "__main__":
    unittest.main()
