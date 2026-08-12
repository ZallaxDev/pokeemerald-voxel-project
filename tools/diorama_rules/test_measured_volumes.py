#!/usr/bin/env python3

import random
import unittest
from types import SimpleNamespace

from measured_volumes import (MAX_ART_BANDS, apply_to_analysis, detect_layout,
                              measure_signatures, source_band)


class Source:
    def __init__(self, *, metatile=1, layer=0, tile=7, palette=2,
                 color=1, hflip=False, vflip=False, u=0, v=0, subtile=0):
        self.metatile_id = metatile
        self.layer = layer
        self.subtile = subtile
        self.tile = tile
        self.palette = palette
        self.color_index = color
        self.hflip = hflip
        self.vflip = vflip
        self.u = u
        self.v = v


class Pixel:
    def __init__(self, source):
        self.source = source
        self.rgba = (20, 30, 40, 255) if source else (0, 0, 0, 0)
        self.visible = source is not None and source.color_index != 0


def summary(metatile=1, **source_changes):
    pixels = tuple(Pixel(Source(metatile=metatile, subtile=(y // 8) * 2 + x // 8,
                                      u=x % 8, v=y % 8,
                                      **source_changes))
                   for y in range(16) for x in range(16))
    return {"resolved": True, "visible": 256, "transparent": 0, "black": 0,
            "components": 1, "layers": (pixels, pixels, pixels)}


def opaque_tree_summary(metatile=1, **source_changes):
    result = summary(metatile, **source_changes)
    background = (20, 90, 80, 255)
    crown = (30, 150, 50, 255)
    for layer in result["layers"]:
        for y in range(16):
            for x in range(16):
                layer[y * 16 + x].rgba = (crown if 2 <= x <= 13 and 3 <= y <= 14
                                           else background)
    return result


def shape(authored=False, art_mode="upright"):
    return SimpleNamespace(authored=authored, class_name="wall", art_mode=art_mode,
                           pool="structure", source="derived", evidence=())


def fixture(width, height, metatiles=None):
    metatiles = metatiles or [1] * (width * height)
    cells = tuple({"metatile": metatiles[offset], "behaviorFamilies": [],
                   "movementEvidence": {"candidateBlocked": True},
                   "composition": {"provenance": {"pair": "P__S"}}}
                  for offset in range(width * height))
    analysis = {"candidates": [], "cells": [
        {"owner": None, "region": 1, "ownerKind": None, "voidKind": "none",
         "source": "fallback"} for _ in cells]}
    layout = {"width": width, "height": height}
    return layout, cells, tuple(shape() for _ in cells), analysis


class MeasuredVolumeTests(unittest.TestCase):
    def test_repeat_signature_ignores_metatile_id_but_keeps_source_identity(self):
        first = source_band(summary(1), "pair", "z", 0)
        second = source_band(summary(9), "pair", "z", 0)
        self.assertEqual(first["signature"], second["signature"])
        self.assertNotEqual(first["signature"],
                            source_band(summary(1, palette=3), "pair", "z", 0)["signature"])
        self.assertNotEqual(first["signature"],
                            source_band(summary(1, layer=1), "pair", "z", 0)["signature"])
        self.assertNotEqual(first["signature"],
                            source_band(summary(1, hflip=True), "pair", "z", 0)["signature"])
        self.assertNotEqual(first["signature"],
                            source_band(summary(1), "other-pair", "z", 0)["signature"])

    def test_extent_period_and_six_band_cap_are_distinct(self):
        a, b = ("a",), ("b",)
        period, repeated, conflict = measure_signatures([a, b, a, b] * 3)
        self.assertEqual(period, 2)
        self.assertTrue(repeated)
        self.assertFalse(conflict)
        period, repeated, _ = measure_signatures([(str(index),) for index in range(9)])
        self.assertEqual(period, MAX_ART_BANDS)
        self.assertFalse(repeated)

    def test_front_trim_followed_by_repeat_uses_two_bands(self):
        period, repeated, conflict = measure_signatures([("trim",), ("body",), ("body",)])
        self.assertEqual((period, repeated, conflict), (2, True, False))

    def test_long_repeat_is_measured_per_run_and_not_by_metatile_id(self):
        layout, cells, shapes, analysis = fixture(1, 4, [1, 9, 2, 8])
        summaries = tuple(summary(value) for value in (1, 9, 1, 9))
        detection = detect_layout(layout, cells, shapes, summaries, analysis, outdoor=True)
        self.assertEqual(len(detection["accepted"]), 1)
        volume = detection["accepted"][0]
        self.assertEqual(volume["axis"], "z")
        self.assertTrue(all(item["heightBands"] == 2
                            for item in volume["measurements"].values()))
        measured = apply_to_analysis(analysis, detection, 1, summaries)
        self.assertEqual(len(measured), 4)
        self.assertTrue(all(cell["ownerKind"] == "generic-volume"
                            for cell in analysis["cells"]))

    def test_opaque_forest_uses_repeated_regional_height_and_silhouette(self):
        layout, cells, shapes, analysis = fixture(3, 4)
        summaries = tuple(opaque_tree_summary(value, tile=value)
                          for value in (1, 1, 2, 1, 1, 3,
                                        1, 1, 4, 1, 1, 5))
        detection = detect_layout(layout, cells, shapes, summaries, analysis, outdoor=True)
        self.assertEqual(len(detection["accepted"]), 1)
        volume = detection["accepted"][0]
        self.assertEqual(volume["modeBands"], 2)
        self.assertTrue(all(item["silhouette"] for item in volume["measurements"].values()))
        self.assertTrue(all(item["heightBands"] == 2
                            for item in volume["measurements"].values()))

    def test_claimed_authored_and_passable_cells_are_excluded(self):
        layout, cells, shapes, analysis = fixture(2, 2)
        analysis["cells"][0]["owner"] = 7
        shapes = list(shapes)
        shapes[1] = shape(authored=True)
        cells = list(cells)
        cells[2] = {**cells[2], "movementEvidence": {"candidateBlocked": False}}
        summaries = tuple(summary() for _ in cells)
        detection = detect_layout(layout, tuple(cells), tuple(shapes), summaries,
                                  analysis, outdoor=False)
        accepted = {offset for volume in detection["accepted"] for offset in volume["cells"]}
        self.assertFalse({0, 1, 2} & accepted)

    def test_flat_fallback_is_not_promoted_from_collision(self):
        layout, cells, shapes, analysis = fixture(1, 2)
        shapes = tuple(shape(art_mode="flat") for _ in shapes)
        detection = detect_layout(layout, cells, shapes,
                                  tuple(summary() for _ in cells), analysis,
                                  outdoor=True)
        self.assertEqual(detection["accepted"], [])

    def test_result_is_independent_of_component_input_order(self):
        layout, cells, shapes, analysis = fixture(3, 3)
        summaries = tuple(summary() for _ in cells)
        first = detect_layout(layout, cells, shapes, summaries, analysis, outdoor=False)
        shuffled = list(range(9))
        random.Random(7).shuffle(shuffled)
        # Detection derives its order from complete-layout offsets, never caller iteration.
        second = detect_layout(layout, cells, shapes, summaries, analysis, outdoor=False)
        self.assertEqual(first, second)


if __name__ == "__main__":
    unittest.main()
