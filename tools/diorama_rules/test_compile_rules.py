#!/usr/bin/env python3

import copy
import hashlib
import json
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace

from compile_rules import (ARCHETYPES, RuleError, _action, _ground_policy, _mask,
                             _pattern_placements, _patterns, _profiles, _selector,
                             _terrain_anchors, _terrain_mode, _validate_logical_ids, _validate_pattern_claim_overlaps,
                            _validate_tileset_pin_action,
                            compile_data, load_json, parse_camera, render_c, render_header)
from migrate_v1_to_v2 import MigrationError, migrate_document


ROOT = Path(__file__).resolve().parents[2]


class DioramaRuleCompilerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.data = compile_data(ROOT)

    def test_repository_ir_is_complete_and_deterministic(self):
        second = compile_data(ROOT)
        self.assertEqual(self.data, second)
        self.assertEqual(render_c(self.data), render_c(second))
        self.assertEqual(len(self.data["tilesets"]), 75)
        self.assertEqual([row["id"] for row in self.data["tilesets"]], list(range(1, 76)))
        self.assertEqual(len(self.data["layouts"]), 441)
        self.assertEqual([row["id"] for row in self.data["layouts"]], list(range(1, 442)))
        self.assertEqual(len(self.data["maps"]), 518)
        self.assertEqual(len(self.data["tilesets"]), 75)
        self.assertEqual({row["terrainClass"] for row in self.data["tilesets"]},
                         {"ground", "path", "sand", "ash", "rock", "pavement",
                          "wood", "carpet"})
        modes = {row["symbol"]: row["terrainMode"] for row in self.data["tilesets"]}
        self.assertEqual(modes["gTileset_General"], "manual")
        self.assertEqual(modes["gTileset_Cave"], "manual")
        self.assertTrue(all(row["terrainMode"] in {"automatic", "manual"}
                            for row in self.data["maps"]))
        self.assertTrue(all("terrainAnchors" in row for row in self.data["maps"]))
        self.assertEqual(sum(row["supported"] for row in self.data["maps"]), 518)
        references = {"MAP_FORTREE_CITY", "MAP_SOOTOPOLIS_CITY", "MAP_MT_CHIMNEY",
                      "MAP_JAGGED_PASS", "MAP_GRANITE_CAVE_B1F", "MAP_MT_PYRE_2F"}
        self.assertTrue(references <= {row["symbol"] for row in self.data["maps"]
                                      if row["supported"]})
        indoor = next(row for row in self.data["maps"]
                      if row["mapType"] == "MAP_TYPE_INDOOR"
                      and row["symbol"] not in {"MAP_LITTLEROOT_TOWN_BRENDANS_HOUSE_1F",
                                                "MAP_LITTLEROOT_TOWN_BRENDANS_HOUSE_2F",
                                                "MAP_LITTLEROOT_TOWN_MAYS_HOUSE_1F",
                                                "MAP_LITTLEROOT_TOWN_MAYS_HOUSE_2F",
                                                "MAP_LITTLEROOT_TOWN_PROFESSOR_BIRCHS_LAB"})
        self.assertEqual(indoor["camera"]["profile"], "interior")
        self.assertTrue(all(row["unsupportedReason"] for row in self.data["maps"]
                            if not row["supported"]))

    def test_layout_terrain_ranges_are_complete_and_deterministic(self):
        offset = 0
        records = []
        for layout in self.data["layouts"]:
            self.assertGreater(layout["width"], 0)
            self.assertGreater(layout["height"], 0)
            self.assertEqual(layout["terrainRecordOffset"], offset)
            self.assertEqual(layout["terrainRecordCount"], len(layout["terrainRecords"]))
            cell_offsets = [row["cellOffset"] for row in layout["terrainRecords"]]
            self.assertEqual(cell_offsets, sorted(cell_offsets))
            self.assertTrue(all(0 <= value < layout["width"] * layout["height"]
                                for value in cell_offsets))
            offset += layout["terrainRecordCount"]
            records.extend(layout["terrainRecords"])
        self.assertGreater(len(records), 0)
        self.assertFalse(any(row["automatic"] for row in records))
        self.assertTrue(all(len(row["volumeBackMetatiles"]) == 3
                            and len(row["volumeFrontMetatiles"]) == 3
                            for row in records))
        self.assertTrue(any(row["groundQ16"] > 0 for row in records))
        self.assertEqual(render_c(self.data), render_c(compile_data(ROOT)))

    def test_manual_reference_layouts_use_relative_terrace_levels(self):
        layouts = {row["symbol"]: row for row in self.data["layouts"]}
        chimney = layouts["LAYOUT_MT_CHIMNEY"]["terrainRecords"]
        granite = layouts["LAYOUT_GRANITE_CAVE_B1F"]["terrainRecords"]

        for records in (chimney, granite):
            self.assertTrue(records)
            self.assertFalse(any(row["automatic"] for row in records))
            self.assertEqual({row["heightQ16"] for row in records
                              if row["shape"] == "cliff"}, {16})
        chimney_levels = {row["groundQ16"] for row in chimney if row["shape"] == "flat"}
        granite_levels = {row["groundQ16"] for row in granite if row["shape"] == "flat"}
        self.assertIn(16, chimney_levels)
        self.assertGreaterEqual(max(chimney_levels), 32)
        self.assertIn(16, granite_levels)

    def test_terrain_anchor_is_guarded_by_the_layout_metatile(self):
        layout = {"width": 2, "height": 1}
        cells = ({"metatile": 7}, {"metatile": 7})
        anchor = {"id": "upper-region", "x": 1, "y": 0,
                  "level": -1, "height": 1.5, "expectedMetatile": 7}
        parsed = _terrain_anchors([anchor], "anchors", layout, cells)[0]
        self.assertEqual(parsed["level"], -1.0)
        self.assertEqual(parsed["height"], 1.5)
        with self.assertRaisesRegex(RuleError, "expected 7"):
            _terrain_anchors([{**anchor, "expectedMetatile": 8}],
                             "anchors", layout, cells)

    def test_sha256_is_canonical_and_covers_every_ir_section(self):
        canonical_keys = ("schemaVersion", "tilesets", "layouts", "pools", "profiles", "default",
                          "behaviorRules", "tilesetPins", "contextualRules", "exactPatterns",
                          "eventPresets", "maps")
        canonical = {key: self.data[key] for key in canonical_keys}
        encoded = json.dumps(canonical, sort_keys=True, separators=(",", ":"),
                             ensure_ascii=True).encode("ascii")
        self.assertEqual(self.data["sha256"], hashlib.sha256(encoded).hexdigest())
        self.assertEqual(len(self.data["sha256"]), 64)
        changed = copy.deepcopy(canonical)
        changed["profiles"][0]["masks"]["claim"]["rows"][0] = "fffe"
        changed_hash = hashlib.sha256(json.dumps(changed, sort_keys=True, separators=(",", ":"),
                                                 ensure_ascii=True).encode("ascii")).hexdigest()
        self.assertNotEqual(changed_hash, self.data["sha256"])

    def test_migrated_behavior_is_live_or_has_structured_exception(self):
        for rule in self.data["behaviorRules"]:
            if rule["placementCount"] == 0:
                self.assertEqual(set(rule["allowedUnused"]), {"reason"})
                self.assertTrue(rule["allowedUnused"]["reason"])
            else:
                self.assertIsNone(rule["allowedUnused"])

    def test_all_g3_archetypes_are_closed(self):
        self.assertEqual(len(ARCHETYPES), len(set(ARCHETYPES)))
        self.assertIn("ground", ARCHETYPES)
        self.assertIn("stairs-down-w", ARCHETYPES)
        self.assertIn("animated-cutout", ARCHETYPES)
        with self.assertRaisesRegex(RuleError, "unknown archetype"):
            _action({"archetype": "sphere", "pool": "terrain"}, "test", {"terrain"}, {})

    def test_action_heights_align_to_voxel_grid(self):
        action = _action({"archetype": "ground", "pool": "terrain",
                          "groundOffset": -0.125, "height": 0.375},
                         "test", {"terrain"}, {})
        self.assertEqual(action["height"], 0.375)
        with self.assertRaisesRegex(RuleError, "1/16-cell"):
            _action({"archetype": "ground", "pool": "terrain", "height": 0.1},
                    "test", {"terrain"}, {})

    def test_action_terrain_class_is_closed(self):
        action = _action({"archetype": "ground", "pool": "terrain",
                          "terrainClass": "pavement"}, "test", {"terrain"}, {})
        self.assertEqual(action["terrainClass"], "pavement")
        with self.assertRaisesRegex(RuleError, "unknown terrain class"):
            _action({"archetype": "ground", "pool": "terrain",
                     "terrainClass": "generic-stuff"}, "test", {"terrain"}, {})

    def test_action_shape_can_override_archetype_geometry(self):
        action = _action({"shape": "cliff", "archetype": "rock", "pool": "prop",
                          "terrainClass": "rock", "height": 1},
                         "test", {"prop"}, {})
        self.assertEqual(action["shape"], "cliff")
        self.assertEqual(action["archetype"], "rock")
        with self.assertRaisesRegex(RuleError, "unknown shape"):
            _action({"shape": "sphere", "archetype": "rock", "pool": "prop"},
                    "test", {"prop"}, {})

    def test_action_face_materials_are_closed(self):
        action = _action({"archetype": "cliff", "pool": "terrain",
                          "faces": {"top": {"metatile": "self", "layer": "base",
                                             "rotation": 90, "flipX": True},
                                    "south": {"metatile": 23, "layer": "full"},
                                    "north": "none"}},
                         "test", {"terrain"}, {})
        self.assertEqual(action["faces"]["south"]["metatile"], 23)
        self.assertEqual(action["faces"]["north"], "none")
        self.assertEqual(action["faces"]["top"]["rotation"], 90)
        self.assertTrue(action["faces"]["top"]["flipX"])
        with self.assertRaisesRegex(RuleError, "unknown fields"):
            _action({"archetype": "ground", "pool": "terrain",
                     "faces": {"bottom": "none"}}, "test", {"terrain"}, {})

    def test_reusable_rock_terrain_rejects_absolute_floor(self):
        with self.assertRaisesRegex(RuleError, "absolute floor"):
            _validate_tileset_pin_action(
                {"archetype": "ground", "terrainClass": "rock", "groundOffset": 2},
                "pin.action")
        _validate_tileset_pin_action(
            {"archetype": "cliff", "terrainClass": "rock", "height": 1},
            "pin.action")

    def test_rejects_v1_and_duplicate_json_keys(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "rules.json"
            path.write_text('{"version":1}', encoding="utf-8")
            with self.assertRaisesRegex(RuleError, "schemaVersion must be 2"):
                load_json(path)
            path.write_text('{"schemaVersion":2,"kind":"global","kind":"map"}', encoding="utf-8")
            with self.assertRaisesRegex(RuleError, "duplicate key"):
                load_json(path)

    def test_profile_masks_samples_ranges_and_references(self):
        tilesets = {"gTileset_Test": SimpleNamespace(metatile_count=2)}
        values = [{"id": "tree-profile", "archetype": "tree",
                   "masks": {"occupancy": {"width": 3, "height": 1, "rows": ["7"]}},
                   "samples": [{"tileset": "gTileset_Test", "metatile": 1, "layer": "full"}]}]
        profiles, by_id = _profiles(values, "profiles", tilesets)
        self.assertEqual(profiles[0]["masks"]["occupancy"]["rows"], ["7"])
        self.assertIn("tree-profile", by_id)
        bad = copy.deepcopy(values)
        bad[0]["samples"][0]["metatile"] = 2
        with self.assertRaisesRegex(RuleError, "0 through 1"):
            _profiles(bad, "profiles", tilesets)
        with self.assertRaisesRegex(RuleError, "outside mask width"):
            _mask({"width": 3, "height": 1, "rows": ["f"]}, "mask")

    def test_context_selector_accepts_every_context_dimension(self):
        tilesets = {"gTileset_Test": object()}
        behaviors = {"MB_NORMAL": 0}
        selector = _selector({
            "tilesets": ["gTileset_Test"], "metatiles": [4], "behaviors": ["MB_NORMAL"],
            "layerTypes": ["split"], "elevations": [3], "mapTypes": ["MAP_TYPE_ROUTE"],
            "neighbors": {"n": {"behavior": "MB_NORMAL", "layerType": "normal",
                                  "elevation": 2, "metatile": 7}},
            "event": {"kind": "object", "class": "berry-tree"},
        }, "selector", tilesets, behaviors)
        self.assertEqual(set(selector["neighbors"]), {"n"})
        self.assertEqual(selector["event"]["class"], "berry-tree")
        with self.assertRaisesRegex(RuleError, "unknown fields"):
            _selector({"collision": [1]}, "selector", tilesets, behaviors)

    def test_exact_pattern_dimensions_claim_and_placement_scan(self):
        tilesets = {"P": SimpleNamespace(role="primary", metatile_count=5),
                    "S": SimpleNamespace(role="secondary", metatile_count=4)}
        raw = [{"id": "door-pattern", "dimensions": {"width": 2, "height": 2},
                "tilesets": {"primary": "P", "secondary": "S"},
                "cells": [[1, 2], [3, 4]], "priority": 10, "claimMask": ["11", "01"],
                "action": {"archetype": "building", "pool": "structure"}}]
        pattern = _patterns(raw, "patterns", tilesets, {"structure"}, {})[0]
        layout = {"id": "L", "primary_tileset": "P", "secondary_tileset": "S",
                  "width": 3, "height": 2}
        cells = {"L": tuple({"metatile": value} for value in (1, 2, 9, 3, 4, 9))}
        self.assertEqual(_pattern_placements(pattern, [layout], cells), 1)
        bad = copy.deepcopy(raw)
        bad[0]["claimMask"] = ["00", "00"]
        with self.assertRaisesRegex(RuleError, "claim at least one"):
            _patterns(bad, "patterns", tilesets, {"structure"}, {})

    def test_exact_pattern_rejects_cells_outside_declared_tilesets(self):
        tilesets = {"P": SimpleNamespace(role="primary", metatile_count=2),
                    "S": SimpleNamespace(role="secondary", metatile_count=1)}
        base = {"id": "range-pattern", "dimensions": {"width": 1, "height": 1},
                "tilesets": {"primary": "P", "secondary": "S"}, "priority": 1,
                "claimMask": ["1"],
                "action": {"archetype": "building", "pool": "structure"}}
        for metatile in (2, 0x201):
            raw = [{**base, "cells": [[metatile]]}]
            with self.assertRaisesRegex(RuleError, "outside the declared"):
                _patterns(raw, "patterns", tilesets, {"structure"}, {})

    def test_equal_priority_concrete_pattern_claim_overlap_is_rejected(self):
        action = {"archetype": "building", "pool": "structure"}
        patterns = [
            {"id": name, "dimensions": {"width": 1, "height": 1},
             "tilesets": {"primary": "P", "secondary": "S"}, "cells": [[1]],
             "priority": priority, "claimMask": ["1"], "action": action}
            for name, priority in (("first", 4), ("second", 4))
        ]
        layout = {"id": "L", "primary_tileset": "P", "secondary_tileset": "S",
                  "width": 1, "height": 1}
        maps = [{"symbol": "MAP_TEST", "layout": "L"}]
        cells = {"L": ({"metatile": 1},)}
        with self.assertRaisesRegex(RuleError, "equal-priority pattern claim overlap"):
            _validate_pattern_claim_overlaps(patterns, maps, {"L": layout}, cells, {})
        patterns[1]["priority"] = 3
        _validate_pattern_claim_overlaps(patterns, maps, {"L": layout}, cells, {})

    def test_duplicate_logical_ids_are_global_across_sections_and_scopes(self):
        with self.assertRaisesRegex(RuleError, "duplicate logical ID 'shared'"):
            _validate_logical_ids([("shared", "profiles"),
                                   ("shared", "MAP_TEST.contextualRules")])

    def test_ground_policy_is_closed(self):
        self.assertEqual(_ground_policy({"mode": "automatic"}, "ground"), {"mode": "automatic"})
        self.assertEqual(_ground_policy({"mode": "manual", "metatile": 17}, "ground")["metatile"], 17)
        with self.assertRaisesRegex(RuleError, "unknown fields"):
            _ground_policy({"mode": "automatic", "metatile": 1}, "ground")

    def test_terrain_mode_is_closed(self):
        self.assertEqual(_terrain_mode("manual", "terrainMode"), "manual")
        self.assertEqual(_terrain_mode("automatic", "terrainMode"), "automatic")
        with self.assertRaisesRegex(RuleError, "automatic.*manual"):
            _terrain_mode("hybrid", "terrainMode")

    def test_camera_ranges(self):
        profile, pitch, focal = parse_camera(
            {"profile": "interior", "pitch": 55, "focalLength": 150}, "test")
        self.assertEqual(profile, "interior")
        self.assertAlmostEqual(pitch, 0.959931, places=5)
        self.assertEqual(focal, 150)
        with self.assertRaises(RuleError):
            parse_camera({"profile": "interior", "pitch": 90}, "test")

    def test_explicit_migration_never_revives_retired_prototypes(self):
        migrated = migrate_document({"version": 1, "map": "MAP_X", "layout": "LAYOUT_X",
                                     "supported": True, "overrides": [], "buildings": []})
        self.assertEqual(migrated["schemaVersion"], 2)
        self.assertEqual(migrated["contextualRules"], [])
        with self.assertRaisesRegex(MigrationError, "retired"):
            migrate_document({"version": 1, "map": "MAP_X", "layout": "LAYOUT_X",
                              "supported": True, "overrides": [{"x": 1}], "buildings": []})
        with self.assertRaisesRegex(MigrationError, "retired"):
            migrate_document({"version": 1, "templates": {"house": {}}})

    def test_c_contract_contains_full_catalog_base_tables(self):
        header, source = render_header(), render_c(self.data)
        self.assertIn("struct DioramaGeneratedTilesetV2", header)
        self.assertIn("struct DioramaGeneratedLayoutV2", header)
        self.assertIn("struct DioramaGeneratedTerrainV2", header)
        self.assertIn("role, terrainClass", header)
        self.assertIn("struct DioramaGeneratedMapV2", header)
        self.assertEqual(source.count("gTileset_"), 75)
        self.assertIn("gDioramaBehaviorRulesV2", source)
        self.assertIn("gDioramaBehaviorRuleV2Count", source)
        self.assertIn("gDioramaTerrainV2Count", source)
        self.assertIn(self.data["sha256"], source)

    def test_c_contract_packs_every_normalized_g3_section_and_map_scope(self):
        packed = copy.deepcopy(self.data)
        action = {"archetype": "ground", "pool": "terrain", "profile": "flat-cell",
                   "axis": "cross", "groundOffset": 0.25, "height": 1.0,
                  "faces": {"top": {"metatile": "self", "layer": "base"},
                            "south": {"metatile": 7, "layer": "full"},
                            "north": "none"},
                   "groundPolicy": {"mode": "manual", "metatile": 1}}
        selector = {"tilesets": [packed["tilesets"][0]["symbol"]], "metatiles": [1],
                    "behaviors": ["MB_NORMAL"], "behaviorIds": [0],
                    "layerTypes": ["normal"], "elevations": [2],
                    "mapTypes": ["MAP_TYPE_TOWN"],
                    "neighbors": {"nw": {"metatile": 1, "behavior": "MB_NORMAL",
                                             "behaviorId": 0, "layerType": "normal",
                                             "elevation": 2}},
                    "event": {"kind": "object", "class": "berry-tree"}}
        rule = {"id": "packed-rule", "selector": selector, "action": action,
                "priority": 7, "placementCount": 1, "allowedUnused": None}
        primary = next(row for row in packed["tilesets"] if row["role"] == "primary")
        secondary = next(row for row in packed["tilesets"] if row["role"] == "secondary")
        pattern = {"id": "packed-pattern", "dimensions": {"width": 1, "height": 1},
                   "tilesets": {"primary": primary["symbol"], "secondary": secondary["symbol"]},
                   "cells": [[0]], "priority": 8, "claimMask": ["1"], "action": action,
                   "placementCount": 0, "allowedNoPlacements": {"reason": "fixture"}}
        packed["contextualRules"] = [rule]
        packed["exactPatterns"] = [pattern]
        packed["eventPresets"] = [{"id": "packed-preset",
                                    "event": {"kind": "object", "class": "berry-tree"},
                                    "action": action, "placementCount": 1,
                                    "allowedUnused": None}]
        packed["maps"][0]["contextualRules"] = [{**rule, "id": "local-rule"}]
        packed["maps"][0]["exactPatterns"] = [{**pattern, "id": "local-pattern"}]
        header, source = render_header(), render_c(packed)
        for name in ("Pools", "Profiles", "Masks", "MaskRows", "Samples", "TilesetPins",
                     "Selectors", "Neighbors", "ContextualRules", "PatternCells",
                     "PatternClaims", "ExactPatterns", "EventPresets"):
            self.assertIn(f"gDiorama{name}V2", header)
            self.assertIn(f"gDiorama{name}V2", source)
        for content in ("packed-rule", "local-rule", "packed-pattern", "local-pattern",
                        "packed-preset", "berry-tree"):
            self.assertIn(content, source)
        self.assertIn("UINT64_C(0xFFFF)", source)
        self.assertIn("{ 65535, 65535, 65535, 7, 65535, 65535 }", source)
        self.assertIn("{ 1, 255, 0, 0, 0, 2 }", source)


if __name__ == "__main__":
    unittest.main()
