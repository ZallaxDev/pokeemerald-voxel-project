#!/usr/bin/env python3

import tempfile
import unittest
import json
from pathlib import Path

from catalog import _structure_candidates, build_catalog, load_tilesets, write_catalog
from emerald_compositor import TilesetComposer, expand_5bit


ROOT = Path(__file__).resolve().parents[2]


class DioramaCatalogTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.catalog = build_catalog(ROOT)

    def test_global_inventory_counts(self):
        expected = {
            "maps": 518,
            "layouts": 441,
            "referencedLayouts": 406,
            "unreferencedLayouts": 35,
            "tilesets": 75,
            "logicalMetatiles": 18318,
            "primaryTilesets": 3,
            "secondaryTilesets": 72,
            "blocks": 324579,
            "signEvents": 533,
        }
        self.assertEqual({key: self.catalog["summary"][key] for key in expected}, expected)
        self.assertEqual(self.catalog["summary"]["usedBehaviors"], 120)
        self.assertGreater(self.catalog["summary"]["events"], 5000)
        self.assertEqual(self.catalog["summary"]["decorations"], 120)
        self.assertGreater(self.catalog["summary"]["runtimeAlternateLayouts"], 0)
        self.assertGreater(self.catalog["summary"]["structureCandidates"], 0)

    def test_tilesets_come_from_headers_and_preserve_resource_aliases(self):
        tilesets = load_tilesets(ROOT)
        brown = tilesets["gTileset_SecretBaseBrownCave"]
        tree = tilesets["gTileset_SecretBaseTree"]
        self.assertNotEqual(brown.graphics_key, tree.graphics_key)
        self.assertEqual(brown.metatiles_key, tree.metatiles_key)
        self.assertEqual(brown.metatiles_root, "data/tilesets/secondary/secret_base")

    def test_compositor_matches_runtime_color_expansion_and_provenance(self):
        self.assertEqual([expand_5bit(value) for value in (0, 1, 15, 16, 31)],
                         [0, 8, 123, 132, 255])
        composer = TilesetComposer(ROOT / "data/tilesets/primary/general",
                                   ROOT / "data/tilesets/secondary/petalburg")
        base, foreground, full = composer.compose_metatile(0x200)
        source = next(pixel.source for pixel in base + foreground if pixel.source is not None)
        self.assertEqual(source.metatile_id, 0x200)
        self.assertIn(source.layer, (0, 1))
        self.assertEqual(len(full), 16 * 16)

    def test_cells_keep_collision_elevation_and_context(self):
        usage = self.catalog["usage"]["LAYOUT_ROUTE103"]
        self.assertTrue(all("collision" in item and "elevation" in item and "contexts" in item
                            for item in usage.values()))
        layout = next(item for item in self.catalog["layouts"] if item["id"] == "LAYOUT_ROUTE103")
        self.assertEqual(sum(item["count"] for item in usage.values()), layout["blockCount"])
        cells = self.catalog["cells"]["LAYOUT_ROUTE103"]
        self.assertEqual(len(cells), layout["blockCount"])
        self.assertEqual([(cell["x"], cell["y"]) for cell in cells[:3]],
                         [(0, 0), (1, 0), (2, 0)])
        self.assertTrue(all({"metatile", "collision", "elevation", "behavior",
                             "layerType", "tileset", "source", "composition",
                             "movementEvidence"} <= set(cell) for cell in cells))
        self.assertTrue(all(cell["movementEvidence"]["authoritative"] is False
                            for cell in cells))

    def test_events_behaviors_animations_and_runtime_changes_are_inventoried(self):
        events = self.catalog["events"]
        self.assertEqual(events["counts"]["sign"], 533)
        self.assertGreater(events["counts"]["cutObstacle"], 0)
        self.assertGreater(events["counts"]["rockSmashObstacle"], 0)
        self.assertGreater(events["counts"]["berryTree"], 0)
        self.assertGreater(events["counts"]["door"], 0)
        behaviors = {item["name"]: item for item in self.catalog["behaviors"]}
        self.assertIn("water", behaviors["MB_OCEAN_WATER"]["families"])
        self.assertIn("ledge", behaviors["MB_JUMP_EAST"]["families"])
        self.assertIn("bridge", behaviors["MB_BRIDGE_OVER_OCEAN"]["families"])
        families = {family for item in behaviors.values() for family in item["families"]}
        self.assertTrue({"water", "ledge", "stairs", "bridge", "hole", "movement"}
                        <= families)
        slots = self.catalog["animations"]["tileSlots"]
        self.assertEqual(len(slots), 75)
        self.assertEqual(sum(len(slot["absentStaticTiles"]) for slot in slots), 20)
        self.assertEqual(sum(len(slot["absentPngTiles"]) for slot in slots), 12)
        self.assertTrue(all(slot["frames"] for slot in slots))
        self.assertEqual({slot["source"] for slot in slots
                          if "gTileset_MauvilleGym" in slot["tilesets"]},
                         {"gTilesetAnims_MauvilleGym_ElectricGates"})
        self.assertEqual(len(self.catalog["animations"]["paletteSlots"]), 1)
        self.assertGreater(len(self.catalog["runtime"]["metatileChanges"]), 0)
        self.assertTrue(any(item["sourceType"] == "global-script"
                            for item in self.catalog["runtime"]["metatileChanges"]))
        littleroot = next(item for item in self.catalog["maps"]
                          if item["symbol"] == "MAP_LITTLEROOT_TOWN")
        self.assertEqual(littleroot["connections"][0]["map"], "MAP_ROUTE101")
        self.assertTrue(any(item["map"] == "MAP_CONTEST_HALL_TOUGH"
                            and item["eventSourceMap"] == "MAP_CONTEST_HALL"
                            for item in events["events"]))

    def test_metatile_metadata_exposes_subtile_provenance(self):
        pair = next(item for item in self.catalog["metatiles"]
                    if item["id"] == "gTileset_General__gTileset_Petalburg")
        metatile = next(item for item in pair["metatiles"] if item["globalId"] == 0x200)
        self.assertEqual(metatile["localId"], 0)
        self.assertEqual(len(metatile["subtiles"]), 8)
        self.assertTrue(all({"tile", "palette", "hflip", "vflip", "sourceRole",
                             "animationSlots"} <= set(item) for item in metatile["subtiles"]))

    def test_structure_candidates_are_exact_and_order_independent(self):
        layouts = [{"id": "A", "width": 3, "height": 2,
                    "primary_tileset": "P", "secondary_tileset": "S"},
                   {"id": "B", "width": 3, "height": 2,
                    "primary_tileset": "P", "secondary_tileset": "S"}]
        cells = tuple((value, 0, 3) for value in (1, 2, 3, 4, 5, 6))
        forward = _structure_candidates(layouts, {"A": cells, "B": cells})
        reverse = _structure_candidates(list(reversed(layouts)), {"A": cells, "B": cells})
        self.assertEqual(forward, reverse)
        self.assertTrue(any(item["matrix"] == [[1, 2, 3], [4, 5, 6]]
                            for item in forward["candidates"]))

    def test_writes_required_artifacts(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary)
            write_catalog(ROOT, output, catalog=self.catalog)
            for name in ("maps.json", "layouts.json", "tilesets.json",
                          "metatile_usage.json", "layout_cells.json", "metatiles.json", "events.json",
                          "behaviors.json", "animations.json", "structures.json",
                          "ambiguities.json", "runtime_layouts.json", "coverage.json",
                          "index.html"):
                self.assertGreater((output / name).stat().st_size, 0)
            coverage = json.loads((output / "coverage.json").read_text(encoding="utf-8"))
            self.assertEqual(coverage["resourceCoverage"]["blocks"]["cataloged"], 324579)
            self.assertIn("unusedRules", coverage)


if __name__ == "__main__":
    unittest.main()
