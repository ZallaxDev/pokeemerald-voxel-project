#!/usr/bin/env python3

import unittest
from dataclasses import dataclass

from emerald_compositor import Pixel, PixelSource, TRANSPARENT_PIXEL
from pixel_objects import (CONSOLE_DEPTH_Q32, GROUND_REPLACEMENT, GROUND_SOURCE_BASE,
                           LAYERED_CUTOUT_DEPTH_Q32, RELIEF_DEPTH_Q32,
                           ExtractionThresholds, _is_repetitive, extract_candidate)
from profiles import AUTOMATIC_PROP_GROUND, PropGround


@dataclass(frozen=True)
class Shape:
    class_name: str = "ground"
    art_mode: str = "flat"
    pool: str = "terrain"
    prop_ground: PropGround = AUTOMATIC_PROP_GROUND

    def __getitem__(self, key):
        return {"class": self.class_name, "artMode": self.art_mode, "pool": self.pool}[key]


def pixel(color, x=0, y=0, metatile=3, value=None, source_layer=1):
    value = color * 8 if value is None else value
    source = PixelSource(metatile, source_layer, (y // 8) * 2 + x // 8, 12, 2, color,
                         x % 8, y % 8, False, False)
    return Pixel((value, value, value, 255), source)


def layer(metatile, foreground):
    base = tuple(pixel(6, x, y, metatile, 120, 0) for y in range(16) for x in range(16))
    overlay = tuple(foreground.get((x, y), TRANSPARENT_PIXEL)
                    for y in range(16) for x in range(16))
    full = tuple(foreground.get((x, y), base[y * 16 + x])
                 for y in range(16) for x in range(16))
    return base, overlay, full


class PixelObjectTests(unittest.TestCase):
    def fixture(self, class_name="signpost", foreground=None):
        foreground = foreground or {(7, 4): pixel(7, 7, 4, value=60),
                                    (8, 4): pixel(7, 8, 4, value=60),
                                    (7, 5): pixel(15, 7, 5, value=240),
                                    (8, 5): pixel(15, 8, 5, value=240),
                                    (7, 6): pixel(7, 7, 6, value=60),
                                    (8, 6): pixel(7, 8, 6, value=60)}
        cells = tuple({"metatile": value} for value in (1, 1, 1, 1, 3, 1, 1, 1, 1))
        shapes = tuple(Shape() for _ in cells)
        summaries = []
        ground = layer(1, {})
        prop = layer(3, foreground)
        for cell in cells:
            layers = prop if cell["metatile"] == 3 else ground
            summaries.append({"metatile": cell["metatile"], "layers": layers})
        candidate = {"id": 9, "cells": [4], "bbox": {"x": 1, "y": 1,
                     "width": 1, "height": 1}, "class": class_name, "pool": "prop"}
        return candidate, cells, shapes, tuple(summaries)

    def test_outline_preserves_enclosed_light_pixels(self):
        result = extract_candidate(*self.fixture(), 3, 3)
        self.assertIsNotNone(result)
        self.assertEqual(len(result["pixels"]), 6)
        self.assertIn(15, {item["sourceColor"] for item in result["pixels"]})
        self.assertEqual(result["groundMode"], GROUND_REPLACEMENT)
        expected = [0] * 16
        expected[4:7] = [0x180, 0x180, 0x180]
        self.assertEqual(result["maskRows"], expected)
        self.assertEqual({(item["sourceX"], item["sourceY"])
                          for item in result["pixels"]},
                         {(7, 4), (8, 4), (7, 5), (8, 5), (7, 6), (8, 6)})

    def test_every_pixel_has_exact_provenance(self):
        result = extract_candidate(*self.fixture(), 3, 3)
        for item in result["pixels"]:
            self.assertEqual(item["sourceCellOffset"], 4)
            self.assertEqual(item["expectedMetatile"], 3)
            self.assertNotEqual(item["sourceColor"], 0)
        self.assertIn(item["sourceLayer"], (0, 1))

    def test_black_outline_is_not_selected_as_background_tone(self):
        foreground = {(x, y): pixel(7, x, y, value=60)
                      for y in range(4, 8) for x in range(5, 11)
                      if x in (5, 10) or y in (4, 7)}
        foreground.update({(x, y): pixel(15, x, y, value=240)
                           for y in range(5, 7) for x in range(6, 10)})
        result = extract_candidate(*self.fixture(foreground=foreground), 3, 3)
        self.assertEqual(len(result["pixels"]), 24)
        self.assertEqual(sum(item["sourceColor"] == 7 for item in result["pixels"]), 16)

    def test_exact_foreground_layer_drives_layered_cutout(self):
        foreground = {(2, 2): pixel(3, 2, 2, value=180),
                      (3, 2): pixel(3, 3, 2, value=180),
                      (13, 13): pixel(4, 13, 13, value=210)}
        result = extract_candidate(*self.fixture(class_name="cutout", foreground=foreground), 3, 3)
        self.assertEqual(result["groundMode"], GROUND_SOURCE_BASE)
        self.assertEqual(result["groundMetatile"], 0xFFFF)
        self.assertEqual({(item["sourceX"], item["sourceY"]) for item in result["pixels"]},
                         set(foreground))
        self.assertEqual({item["sourceLayer"] for item in result["pixels"]}, {1})
        self.assertEqual({item["sizeZQ32"] for item in result["pixels"]},
                         {LAYERED_CUTOUT_DEPTH_Q32})

    def test_components_stand_on_their_own_feet(self):
        foreground = {(2, 2): pixel(7, 2, 2, value=60), (2, 3): pixel(7, 2, 3, value=60),
                      (12, 8): pixel(7, 12, 8, value=60), (12, 9): pixel(7, 12, 9, value=60)}
        result = extract_candidate(*self.fixture(foreground=foreground), 3, 3)
        self.assertEqual(result["componentCount"], 2)
        self.assertEqual({item["yQ32"] for item in result["pixels"]}, {0, 2})

    def test_manual_class_rejects_unresolved_ground_tie(self):
        candidate, cells, shapes, summaries = self.fixture()
        cells = list(cells)
        cells[1] = cells[3] = {"metatile": 2}
        summaries = list(summaries)
        summaries[1] = summaries[3] = {"metatile": 2, "layers": layer(2, {})}
        self.assertIsNone(extract_candidate(candidate, tuple(cells), shapes,
                                            tuple(summaries), 3, 3))

    def test_automatic_edge_to_edge_is_rejected(self):
        foreground = {(x, y): pixel(7, x, y, value=60) for y in range(16) for x in range(16)}
        fixture = self.fixture(class_name="rock", foreground=foreground)
        self.assertIsNone(extract_candidate(*fixture, 3, 3,
            ExtractionThresholds(max_solid_ratio=0.5)))

    def test_height_and_repetition_guards_are_configurable(self):
        self.assertIsNone(extract_candidate(*self.fixture(), 3, 3,
            ExtractionThresholds(max_upright_height=8)))
        row = tuple(index in (7, 8) for index in range(16))
        self.assertTrue(_is_repetitive(row * 48, 16, 48, 0.75))

    def test_relief_maps_source_y_to_world_z(self):
        result = extract_candidate(*self.fixture(class_name="relief"), 3, 3)
        self.assertEqual(result["kind"], "relief")
        self.assertGreater(len({item["zQ32"] for item in result["pixels"]}), 1)
        self.assertEqual({item["sizeYQ32"] for item in result["pixels"]}, {RELIEF_DEPTH_Q32})

    def test_console_uses_authored_support_height(self):
        result = extract_candidate(*self.fixture(class_name="console"), 3, 3)
        self.assertEqual(result["supportOffsetQ16"], 12)
        self.assertEqual(result["groundMetatile"], 1)
        self.assertEqual(len(result["pixels"]), 256)
        self.assertEqual({item["sizeZQ32"] for item in result["pixels"]},
                         {CONSOLE_DEPTH_Q32})

    def test_authored_object_offset_positions_rear_piece(self):
        candidate, cells, shapes, summaries = self.fixture(class_name="cutout")
        candidate["objectOffsetQ16"] = 16
        candidate["objectDepthOffsetQ32"] = 32
        result = extract_candidate(candidate, cells, shapes, summaries, 3, 3)
        self.assertEqual(result["supportOffsetQ16"], 16)
        self.assertEqual(min(item["zQ32"] for item in result["pixels"]), 79)

    def test_explicit_zero_object_offset_overrides_console_default(self):
        candidate, cells, shapes, summaries = self.fixture(class_name="console")
        candidate["objectOffsetQ16"] = 0
        result = extract_candidate(candidate, cells, shapes, summaries, 3, 3)
        self.assertEqual(result["supportOffsetQ16"], 0)

    def test_authored_pixel_rows_exclude_floor_overlay(self):
        foreground = {(2, 2): pixel(3, 2, 2, value=180),
                      (3, 2): pixel(3, 3, 2, value=180),
                      (4, 2): pixel(3, 4, 2, value=180),
                      (2, 12): pixel(4, 2, 12, value=210),
                      (3, 12): pixel(4, 3, 12, value=210),
                      (4, 12): pixel(4, 4, 12, value=210)}
        candidate, cells, shapes, summaries = self.fixture(
            class_name="cutout", foreground=foreground)
        candidate["objectPixelRows"] = [0, 8]
        candidate["objectThicknessQ32"] = 16
        result = extract_candidate(candidate, cells, shapes, summaries, 3, 3)
        self.assertEqual({item["sourceY"] for item in result["pixels"]}, {2})
        self.assertEqual({item["sizeZQ32"] for item in result["pixels"]}, {16})

    def test_layered_crop_can_use_exact_residual_metatile(self):
        candidate, cells, shapes, summaries = self.fixture(class_name="cutout")
        candidate["objectGroundMode"] = GROUND_REPLACEMENT
        candidate["propGround"] = {"mode": "manual", "metatile": 2}
        result = extract_candidate(candidate, cells, shapes, summaries, 3, 3)
        self.assertEqual(result["groundMode"], GROUND_REPLACEMENT)
        self.assertEqual(result["groundMetatile"], 2)

    def test_manual_ground_override_wins_neighbor_vote(self):
        candidate, cells, shapes, summaries = self.fixture()
        shapes = list(shapes)
        shapes[4] = Shape("ground", "flat", "terrain", PropGround("manual", 2))
        summaries = list(summaries)
        summaries[0] = {"metatile": 2, "layers": layer(2, {})}
        result = extract_candidate(candidate, cells, tuple(shapes), tuple(summaries), 3, 3)
        self.assertEqual(result["groundMetatile"], 2)


if __name__ == "__main__":
    unittest.main()
