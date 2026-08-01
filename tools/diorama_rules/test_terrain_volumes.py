#!/usr/bin/env python3

from __future__ import annotations

import unittest

from terrain_volumes import resolve_layout_terrain, resolve_volumes


def cell(metatile: int, collision: int) -> dict:
    return {"metatile": metatile, "collision": collision,
            "behavior": 0, "elevation": 0}


def resolution(source: str = "behavior:MB_MOUNTAIN_TOP", shape: str = "flat",
               terrain_class: str = "rock") -> dict:
    return {"source": source, "rule": {"shape": shape, "height": 0,
                                          "groundHeight": 0,
                                          "terrainClass": terrain_class}}


class TerrainVolumeTests(unittest.TestCase):
    def test_repeat_height_and_walkable_precedence(self) -> None:
        cells = [cell(1, 0) for _ in range(25)]
        resolved = [resolution() for _ in cells]
        for y, metatile in enumerate((10, 11, 20, 12, 20)):
            cells[y * 5 + 2] = cell(metatile, 1)
        resolve_volumes(cells, resolved, 5, 5)
        self.assertTrue(all(resolved[y * 5 + 2]["rule"]["height"] == 2
                            for y in range(5)))
        north_end = resolved[2]["rule"]
        self.assertTrue(north_end["cliffEdgeMask"])
        self.assertTrue(north_end["cliffBaseMask"])
        self.assertTrue(north_end["cliffCornerMask"])
        self.assertEqual(resolved[1]["rule"]["shape"], "flat")

    def test_authored_and_specialized_cells_are_preserved(self) -> None:
        cells = [cell(1, 1), cell(2, 1)]
        resolved = [resolution("tileset pin"),
                    resolution("behavior:water", "water", "water")]
        resolve_volumes(cells, resolved, 2, 1)
        self.assertEqual(resolved[0]["rule"]["shape"], "flat")
        self.assertEqual(resolved[1]["rule"]["shape"], "water")

    def test_generic_blocked_art_stays_flat(self) -> None:
        cells = [cell(7, 1) for _ in range(3)]
        resolved = [resolution("fallback", terrain_class="ground") for _ in cells]
        resolve_volumes(cells, resolved, 1, 3)
        self.assertTrue(all(item["rule"]["shape"] == "flat" for item in resolved))
        self.assertTrue(all(item["rule"]["height"] == 0 for item in resolved))

    def test_identical_metatiles_use_one_emerald_cell_height(self) -> None:
        cells = [cell(7, 1) for _ in range(3)]
        resolved = [resolution() for _ in cells]
        resolve_volumes(cells, resolved, 1, 3)
        self.assertTrue(all(item["rule"]["height"] == 1 for item in resolved))

    def test_ledge_resolves_plateau_and_conflict_falls_back(self) -> None:
        cells = [cell(1, 0) for _ in range(25)]
        resolved = [resolution("fallback", terrain_class="ground") for _ in cells]
        for y in range(5):
            index = y * 5 + 2
            cells[index] = cell(1, 1)
            cells[index]["behavior"] = 0x38
            resolved[index] = resolution("behavior:MB_JUMP_EAST", "ledge",
                                         "ground")
        resolve_volumes(cells, resolved, 5, 5)
        self.assertEqual(resolved[2 * 5 + 1]["rule"]["groundHeight"], 1)
        self.assertEqual(resolved[2 * 5 + 3]["rule"]["groundHeight"], 0)
        self.assertEqual(resolved[2 * 5 + 2]["rule"]["groundHeight"], 1)

        resolved = [resolution("fallback", terrain_class="ground") for _ in cells]
        for y in range(5):
            index = y * 5 + 2
            resolved[index] = resolution("behavior:MB_JUMP_EAST", "ledge",
                                         "ground")
        cells[2 * 5 + 2]["behavior"] = 0x39
        resolve_volumes(cells, resolved, 5, 5)
        self.assertEqual(resolved[2 * 5 + 1]["rule"]["groundHeight"], 0)
        self.assertEqual(resolved[2 * 5 + 3]["rule"]["groundHeight"], 0)

    def test_measured_cliff_raises_north_ground(self) -> None:
        cells = [cell(1, 0) for _ in range(25)]
        resolved = [resolution("fallback", terrain_class="ground") for _ in cells]
        for x in range(5):
            index = 2 * 5 + x
            cells[index] = cell(10 + x, 1)
            cells[index]["behavior"] = 0x08
            resolved[index] = resolution()
        resolve_volumes(cells, resolved, 5, 5)
        self.assertEqual(resolved[1 * 5 + 2]["rule"]["groundHeight"], 1)
        self.assertEqual(resolved[3 * 5 + 2]["rule"]["groundHeight"], 0)
        cliff = resolved[2 * 5 + 2]["rule"]
        self.assertEqual(cliff["groundHeight"], 0)
        self.assertEqual(cliff["groundHeight"] + cliff["height"], 1)

    def test_bridge_anchors_matching_gameplay_plane(self) -> None:
        cells = [cell(1, 0) for _ in range(9)]
        resolved = [resolution("fallback", terrain_class="ground") for _ in cells]
        for item in cells:
            item["elevation"] = 4
        resolved[4] = resolution("behavior:MB_FORTREE_BRIDGE", "bridge", "wood")
        resolved[4]["rule"]["groundHeight"] = 0.75
        resolve_volumes(cells, resolved, 3, 3)
        self.assertEqual(resolved[3]["rule"]["groundHeight"], 0.75)
        self.assertEqual(resolved[5]["rule"]["groundHeight"], 0.75)

    def test_manual_cells_are_never_inferred(self) -> None:
        cells = [cell(7, 1) for _ in range(3)]
        resolved = [resolution() for _ in cells]
        resolve_volumes(cells, resolved, 1, 3, [False, False, False])
        self.assertTrue(all(item["rule"]["shape"] == "flat" for item in resolved))
        self.assertTrue(all(item["rule"]["height"] == 0 for item in resolved))

    def test_manual_pin_keeps_authored_cliff_height(self) -> None:
        cells = [cell(1, 0) for _ in range(9)]
        resolved = [resolution("fallback", terrain_class="ground") for _ in cells]
        resolved[4] = resolution("tileset pin", "cliff")
        resolved[4]["rule"]["height"] = 1
        resolve_volumes(cells, resolved, 3, 3,
                        [True, True, True, True, False, True, True, True, True])
        self.assertEqual(resolved[4]["rule"]["groundHeight"], 0)
        self.assertEqual(resolved[4]["rule"]["height"], 1)
        self.assertTrue(all(resolved[index]["rule"]["groundHeight"] == 0
                            for index in range(9) if index != 4))

    def test_automatic_mask_length_must_match_layout(self) -> None:
        with self.assertRaisesRegex(ValueError, "mask must match"):
            resolve_volumes([cell(1, 0)], [resolution()], 1, 1, [])

    def test_manual_cliff_courses_create_cumulative_terraces(self) -> None:
        cells = [cell(1, 0) for _ in range(35)]
        resolved = [resolution("fallback", terrain_class="ground") for _ in cells]
        for y in (2, 4):
            for x in range(5):
                index = y * 5 + x
                cells[index] = cell(10 + x, 1)
                resolved[index] = resolution("tileset pin", "cliff")
                resolved[index]["rule"].update({"axis": "x", "height": 1})
        resolve_volumes(cells, resolved, 5, 7, [False] * len(cells))
        self.assertEqual(resolved[1 * 5 + 2]["rule"]["groundHeight"], 2)
        self.assertEqual(resolved[3 * 5 + 2]["rule"]["groundHeight"], 1)
        self.assertEqual(resolved[5 * 5 + 2]["rule"]["groundHeight"], 0)
        self.assertEqual(resolved[2 * 5 + 2]["rule"]["groundHeight"], 1)
        self.assertEqual(resolved[4 * 5 + 2]["rule"]["groundHeight"], 0)

    def test_plateau_anchors_distinguish_identical_metatiles(self) -> None:
        cells = [cell(7, 0) for _ in range(5)]
        resolved = [resolution("fallback", terrain_class="ground") for _ in cells]
        resolved[2] = resolution("behavior:water", "water", "water")
        resolve_volumes(cells, resolved, 5, 1, terrain_anchors={0: 1, 3: 2})
        self.assertEqual([resolved[index]["rule"]["groundHeight"] for index in (0, 1)],
                         [1, 1])
        self.assertEqual([resolved[index]["rule"]["groundHeight"] for index in (3, 4)],
                         [2, 2])
        self.assertEqual(resolved[0]["rule"]["plateauRegion"],
                         resolved[1]["rule"]["plateauRegion"])
        self.assertNotEqual(resolved[1]["rule"]["plateauRegion"],
                            resolved[3]["rule"]["plateauRegion"])

    def test_negative_ground_and_water_region_levels(self) -> None:
        cells = [cell(7, 0) for _ in range(4)]
        resolved = [resolution("fallback", terrain_class="sand") for _ in cells]
        resolve_volumes(cells, resolved, 2, 2,
                        terrain_anchors={0: {"level": -1}})
        self.assertTrue(all(item["rule"]["groundHeight"] == -1 for item in resolved))

        resolved = [resolution("behavior:water", "water", "water") for _ in cells]
        resolve_volumes(cells, resolved, 2, 2,
                        terrain_anchors={0: {"level": -0.5}})
        self.assertTrue(all(item["rule"]["groundHeight"] == -0.5 for item in resolved))
        self.assertTrue(all(item["rule"]["visualRegion"] ==
                            resolved[0]["rule"]["visualRegion"] for item in resolved))

    def test_cliff_region_accepts_local_base_and_relative_height(self) -> None:
        cells = [cell(9, 1) for _ in range(2)]
        resolved = [resolution("tileset pin", "cliff") for _ in cells]
        for item in resolved:
            item["rule"].update({"archetype": "cliff", "height": 1})
        resolve_volumes(cells, resolved, 2, 1, [False, False],
                        {0: {"level": 2, "height": 1.5}})
        self.assertTrue(all(item["rule"]["groundHeight"] == 2 for item in resolved))
        self.assertTrue(all(item["rule"]["height"] == 1.5 for item in resolved))

    def test_targeted_flat_cells_persist_as_authored_cliff_rock(self) -> None:
        cells = [{**cell(0x249, 0), "tileset": "test", "localMetatile": 0x49,
                  "behaviorName": "MB_NORMAL", "layerType": "normal"}
                 for _ in range(5)]
        anchor = {"level": 0, "height": 1, "shape": "cliff", "archetype": "rock",
                  "terrainClass": "rock", "axis": "x", "group": 1}
        resolved = resolve_layout_terrain(
            cells, 5, 1, {"archetype": "ground", "pool": "terrain"},
            {"test": "ash"}, {}, {}, [False] * 5,
            {index: anchor for index in (1, 2, 3)})
        self.assertEqual([item["rule"]["shape"] for item in resolved],
                         ["flat", "cliff", "cliff", "cliff", "flat"])
        self.assertTrue(all(resolved[index]["rule"]["archetype"] == "rock"
                            and resolved[index]["rule"]["terrainClass"] == "rock"
                            and resolved[index]["rule"]["height"] == 1
                            for index in (1, 2, 3)))


if __name__ == "__main__":
    unittest.main()
