#!/usr/bin/env python3
"""Read-only tests for the diorama editor backend."""

from __future__ import annotations

import unittest

import server


class EditorServerTests(unittest.TestCase):
    def test_every_configured_map_exports_and_builds_atlases(self) -> None:
        maps = server.map_listing()
        self.assertEqual(len(maps), 518)
        atlas_pairs = set()
        references = {"MAP_LITTLEROOT_TOWN", "MAP_ROUTE101", "MAP_FORTREE_CITY",
                      "MAP_SOOTOPOLIS_CITY", "MAP_MT_CHIMNEY", "MAP_JAGGED_PASS",
                      "MAP_GRANITE_CAVE_B1F", "MAP_MT_PYRE_2F"}
        for item in (row for row in maps if row["symbol"] in references):
            document = server.enrich_document(
                server.build_editor_document(server.REPO_ROOT, item["symbol"])
            )
            self.assertEqual(
                len(document["cells"]),
                document["map"]["width"] * document["map"]["height"],
            )
            self.assertEqual(len(document["editor"]["resolved"]), len(document["cells"]))
            self.assertEqual(document["geometry"]["modelVersion"], 3)
            self.assertEqual(document["geometry"]["unitsPerCell"], 16)
            self.assertTrue(document["geometry"]["spans"])
            self.assertTrue(document["geometry"]["faces"])
            self.assertEqual(document["rules"]["status"], {"supported": True})
            self.assertEqual(document["editor"]["readOnly"], item["file"] is None)
            if item["symbol"] == "MAP_SOOTOPOLIS_CITY":
                maximum = max(span["y_max"] for span in document["geometry"]["spans"])
                self.assertGreater(maximum, 0)
                self.assertLessEqual(maximum, 3 * document["geometry"]["unitsPerCell"])
            provenance_ids = {item["id"] for item in document["geometry"]["provenance"]}
            self.assertTrue(all(face["material"] in provenance_ids
                                for face in document["geometry"]["faces"]))
            for template in document["editor"]["templates"].values():
                if "pixelProfile" in template:
                    self.assertGreater(template["pixelProfile"]["facades"]["south"]["rows"], 0)
                    self.assertEqual(len(template["compiledRoofProfile"]),
                                     template["width"] * 16 + 1)
            primary = document["tilesets"]["primary"]["symbol"]
            secondary = document["tilesets"]["secondary"]["symbol"]
            atlas_pairs.add((primary, primary))
            atlas_pairs.add((secondary, primary))
        for symbol, primary in atlas_pairs:
            for layer in ("full", "base", "foreground"):
                self.assertTrue(server.build_atlas(symbol, primary, layer).startswith(b"\x89PNG\r\n\x1a\n"))

    def test_unchanged_map_is_a_valid_candidate(self) -> None:
        symbol, destination = next(iter(server.map_sources().items()))
        server.validate_candidate(destination, destination.read_bytes())

    def test_payload_cannot_select_an_arbitrary_path(self) -> None:
        with self.assertRaises(server.RuleError):
            server.validate_payload({
                "symbol": "../../README",
                "revision": "irrelevant",
                "rules": {},
            })


if __name__ == "__main__":
    unittest.main()
