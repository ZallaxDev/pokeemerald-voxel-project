#!/usr/bin/env python3
"""Read-only tests for the diorama editor backend."""

from __future__ import annotations

import unittest

import server


class EditorServerTests(unittest.TestCase):
    def test_every_configured_map_exports_and_builds_atlases(self) -> None:
        maps = server.map_listing()
        self.assertGreaterEqual(len(maps), 1)
        atlas_pairs = set()
        for item in maps:
            document = server.enrich_document(
                server.build_editor_document(server.REPO_ROOT, item["symbol"])
            )
            self.assertEqual(
                len(document["cells"]),
                document["map"]["width"] * document["map"]["height"],
            )
            self.assertEqual(len(document["editor"]["resolved"]), len(document["cells"]))
            primary = document["tilesets"]["primary"]["symbol"]
            secondary = document["tilesets"]["secondary"]["symbol"]
            atlas_pairs.add((primary, primary))
            atlas_pairs.add((secondary, primary))
        for symbol, primary in atlas_pairs:
            for layer in ("full", "base", "foreground"):
                self.assertTrue(server.build_atlas(symbol, primary, layer).startswith(b"\x89PNG\r\n\x1a\n"))

    def test_unchanged_map_is_a_valid_candidate(self) -> None:
        symbol = server.map_listing()[0]["symbol"]
        destination = server.map_sources()[symbol]
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
