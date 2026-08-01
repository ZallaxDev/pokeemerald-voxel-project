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
        references = {"MAP_LITTLEROOT_TOWN", "MAP_ROUTE101", "MAP_ROUTE115", "MAP_FORTREE_CITY",
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
            self.assertIn(document["geometry"]["modelVersion"], {0, 3})
            self.assertEqual(document["geometry"]["unitsPerCell"], 16)
            if len(document["cells"]) <= 1024:
                self.assertTrue(document["geometry"]["spans"])
                self.assertTrue(document["geometry"]["faces"])
            self.assertTrue(all(item["rule"].get("terrainClass")
                                for item in document["editor"]["resolved"]))
            self.assertIn(document["rules"]["terrainMode"], {"automatic", "manual"})
            self.assertEqual(set(document["editor"]["tilesetTerrainModes"]),
                             {"primary", "secondary"})
            self.assertEqual(document["rules"]["status"], {"supported": True})
            self.assertFalse(document["editor"]["readOnly"])
            if item["symbol"] == "MAP_SOOTOPOLIS_CITY" and document["geometry"]["modelVersion"] == 3:
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

    def test_unconfigured_map_can_create_a_scoped_rule_document(self) -> None:
        entry = next(item for item in server.map_listing() if item["file"] is None)
        symbol = entry["symbol"]
        document = server.enrich_document(server.build_editor_document(server.REPO_ROOT, symbol))
        destination, candidate = server.validate_payload({
            "symbol": symbol,
            "revision": None,
            "rules": document["rules"],
        })
        self.assertEqual(destination, server.map_destination(symbol))
        self.assertIn(b'"terrainAnchors": []', candidate)

    def test_tileset_payload_targets_only_cataloged_tilesets(self) -> None:
        document = server.tileset_rule_document("gTileset_General")
        destination, candidate = server.validate_tileset_payload({
            "symbol": "gTileset_General",
            "revision": "irrelevant",
            "rules": document,
        })
        self.assertEqual(destination, server.tileset_destination("gTileset_General"))
        self.assertIn(b'"kind": "tileset"', candidate)
        with self.assertRaises(server.RuleError):
            server.validate_tileset_payload({
                "symbol": "../../README",
                "revision": None,
                "rules": document,
            })

    def test_browser_uses_the_shared_shell_model(self) -> None:
        source = (server.EDITOR_ROOT / "editor.js").read_text(encoding="utf-8")
        self.assertIn("geometry?.modelVersion>=1", source)


if __name__ == "__main__":
    unittest.main()
