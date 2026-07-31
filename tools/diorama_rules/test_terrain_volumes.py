#!/usr/bin/env python3

from __future__ import annotations

import unittest

from terrain_volumes import resolve_volumes


def cell(metatile: int, collision: int) -> dict:
    return {"metatile": metatile, "collision": collision}


def resolution(source: str = "fallback", shape: str = "flat") -> dict:
    return {"source": source, "rule": {"shape": shape, "height": 0}}


class TerrainVolumeTests(unittest.TestCase):
    def test_repeat_height_and_walkable_precedence(self) -> None:
        cells = [cell(1, 0) for _ in range(25)]
        resolved = [resolution() for _ in cells]
        for y, metatile in enumerate((10, 11, 20, 12, 20)):
            cells[y * 5 + 2] = cell(metatile, 1)
        resolve_volumes(cells, resolved, 5, 5)
        self.assertTrue(all(resolved[y * 5 + 2]["rule"]["height"] == 2
                            for y in range(5)))
        self.assertEqual(resolved[1]["rule"]["shape"], "flat")

    def test_authored_and_specialized_cells_are_preserved(self) -> None:
        cells = [cell(1, 1), cell(2, 1)]
        resolved = [resolution("tileset pin"), resolution("behavior:water", "water")]
        resolve_volumes(cells, resolved, 2, 1)
        self.assertEqual(resolved[0]["rule"]["shape"], "flat")
        self.assertEqual(resolved[1]["rule"]["shape"], "water")

    def test_identical_metatiles_use_one_emerald_cell_height(self) -> None:
        cells = [cell(7, 1) for _ in range(3)]
        resolved = [resolution() for _ in cells]
        resolve_volumes(cells, resolved, 1, 3)
        self.assertTrue(all(item["rule"]["height"] == 1 for item in resolved))


if __name__ == "__main__":
    unittest.main()
