#!/usr/bin/env python3

import unittest
from types import SimpleNamespace

from mountain_art import (MountainArtError, detect_layout, mountain_art_mask,
                          mountain_heightmap, validate_layout)


def pixel(color=None, visible=True):
    return SimpleNamespace(rgba=color or (0, 0, 0, 255), visible=visible)


class MountainArtTests(unittest.TestCase):
    def test_partial_foreground_is_authoritative(self):
        foreground = tuple(pixel(visible=3 <= index % 16 <= 12)
                           for index in range(256))
        full = tuple(pixel() for _ in range(256))
        rows, mode = mountain_art_mask(
            {"resolved": True, "layers": (full, foreground, full)})
        self.assertEqual(mode, "foreground")
        self.assertEqual(set(rows), {0x1FF8})

    def test_opaque_tile_uses_corner_connected_background(self):
        full = tuple(pixel((1, 1, 1, 255) if index % 16 in (0, 15)
                           or index // 16 in (0, 15) else (2, 2, 2, 255))
                     for index in range(256))
        empty = tuple(pixel(visible=False) for _ in range(256))
        rows, mode = mountain_art_mask(
            {"resolved": True, "layers": (full, empty, full)})
        self.assertEqual(mode, "silhouette")
        self.assertEqual(rows[0], 0)
        self.assertEqual(rows[1], 0x7FFE)

    def test_square_and_unresolved_art_are_rejected(self):
        full = tuple(pixel((index, 0, 0, 255)) for index in range(256))
        empty = tuple(pixel(visible=False) for _ in range(256))
        with self.assertRaisesRegex(MountainArtError, "non-rectangular"):
            mountain_art_mask({"resolved": True, "layers": (full, empty, full)})
        with self.assertRaisesRegex(MountainArtError, "unresolved"):
            validate_layout(({"behavior": "MB_MOUNTAIN_TOP"},),
                            ({"resolved": False},), 1, 1)

    def test_heightmap_curves_from_low_edge_to_one_cell_peak(self):
        rows = (0, 0) + (0x3FFC,) * 12 + (0, 0)
        heights = mountain_heightmap(rows)
        occupied = {height for height in heights if height != 0}
        self.assertEqual((min(occupied), max(occupied)), (4, 16))
        self.assertGreater(len(occupied), 2)
        self.assertEqual(heights[8 * 16 + 8], 16)
        self.assertEqual(heights[2 * 16 + 2], 4)

    def test_normal_art_requires_family_tokens_and_seed_connection(self):
        def source_pixel(tile, palette=10):
            return SimpleNamespace(rgba=(2, 2, 2, 255), visible=True,
                                   source=SimpleNamespace(tile=tile, palette=palette))

        full_seed = tuple(source_pixel(65 + index % 2) for index in range(256))
        full_edge = tuple(source_pixel(66) for _ in range(256))
        foreground = tuple(pixel(visible=3 <= index % 16 <= 12)
                           for index in range(256))
        summaries = tuple({"resolved": True, "layers": (full, foreground, composed)}
                          for full, composed in ((full_seed, full_seed),
                                                 (full_edge, full_edge),
                                                 (full_edge, full_edge)))
        cells = ({"behavior": "MB_MOUNTAIN_TOP"},
                 {"behavior": "MB_NORMAL"}, {"behavior": "MB_NORMAL"})
        self.assertEqual(detect_layout(cells, summaries, 3, 1), frozenset((0, 1, 2)))

        disconnected = cells + ({"behavior": "MB_NORMAL"},)
        unrelated = tuple(source_pixel(200) for _ in range(256))
        summaries += ({"resolved": True,
                       "layers": (unrelated, foreground, unrelated)},)
        self.assertEqual(detect_layout(disconnected, summaries, 4, 1),
                         frozenset((0, 1, 2)))


if __name__ == "__main__":
    unittest.main()
