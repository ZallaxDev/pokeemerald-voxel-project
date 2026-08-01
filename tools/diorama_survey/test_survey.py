#!/usr/bin/env python3

import json
import tempfile
import unittest
from pathlib import Path

import struct

from survey import BMP_SIGNATURE, SurveyError, _spot, build_plan, record_captures


ROOT = Path(__file__).resolve().parents[2]


def write_bmp(path: Path, width: int = 960, height: int = 640) -> None:
    row_size = width * 4
    file_size = 54 + row_size * height
    header = bytearray(54)
    header[:2] = BMP_SIGNATURE
    struct.pack_into("<I", header, 2, file_size)
    struct.pack_into("<I", header, 10, 54)
    struct.pack_into("<IiiHHII", header, 14, 40, width, height, 1, 32, 0,
                     row_size * height)
    with path.open("wb") as output:
        output.write(header)
        output.seek(file_size - 1)
        output.write(b"\0")


class DioramaSurveyTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.plan = build_plan(ROOT)

    def test_global_plan_is_complete_and_deterministic(self):
        self.assertEqual(self.plan, build_plan(ROOT))
        self.assertEqual(self.plan["summary"], {
            "maps": 518, "layouts": 441, "tilesets": 75,
            "mapSpots": 518, "layoutOnly": 35, "tilesetOnly": 2,
            "captureNames": 2590,
        })
        names = [capture["file"] for entry in self.plan["entries"]
                 for capture in entry["captures"]]
        self.assertEqual(len(names), len(set(names)))
        self.assertTrue(all(len(entry["captures"]) == 5
                            for entry in self.plan["entries"] if entry["kind"] == "map"))
        route115 = next(entry for entry in self.plan["entries"]
                        if entry["id"] == "MAP_ROUTE115")
        self.assertEqual((route115["position"], route115["facing"]),
                         ({"x": 18, "y": 41}, "north"))

    def test_spot_avoids_events_and_blocked_cells(self):
        layout = {"id": "TEST", "width": 3, "height": 1}
        source = {"object_events": [{"x": 1, "y": 0}]}
        cells = [{"x": 0, "y": 0, "collision": 1},
                 {"x": 1, "y": 0, "collision": 0},
                 {"x": 2, "y": 0, "collision": 0}]
        self.assertEqual(_spot(source, layout, cells), (2, 0))

    def test_spot_skips_warp_behavior_cells(self):
        layout = {"id": "TEST", "width": 3, "height": 1}
        source = {"object_events": []}
        cells = [{"x": 0, "y": 0, "collision": 0,
                  "behaviorFamilies": ["warp"]},
                 {"x": 1, "y": 0, "collision": 0,
                  "behaviorFamilies": ["warp"]},
                 {"x": 2, "y": 0, "collision": 0,
                  "behaviorFamilies": []}]
        self.assertEqual(_spot(source, layout, cells), (2, 0))

    def test_manifest_separates_semantic_camera_and_image_hashes(self):
        entry = next(item for item in self.plan["entries"]
                     if item["id"] == "MAP_LITTLEROOT_TOWN")
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for capture in entry["captures"]:
                path = root / capture["file"]
                write_bmp(path)
                state = {
                    "schemaVersion": 1, "mapGroup": entry["mapGroup"],
                    "mapNum": entry["mapNum"], "layoutId": entry["layoutId"],
                    "x": entry["position"]["x"], "y": entry["position"]["y"],
                    "facing": entry["facing"], "view": capture["view"],
                    "width": 960, "height": 640, "captureRun": 1,
                    "sceneKind": 1, "fallbackReasons": 0, "snapshotSequence": 1,
                    "mapGeneration": 1, "mapEditGeneration": 0,
                    "paletteGeneration": 1, "objPaletteGeneration": 1,
                    "animationGeneration": 1,
                }
                path.with_suffix(path.suffix + ".json").write_text(
                    json.dumps(state), encoding="utf-8")
            manifest = record_captures(ROOT, self.plan, root, root / "manifest.json",
                                       {entry["map"]}, commit="0" * 40,
                                       current_plan=self.plan)
            self.assertEqual(len(manifest["captures"]), 5)
            self.assertEqual({item["view"] for item in manifest["captures"]},
                             {"flat", "v15", "v35", "v50", "v75"})
            self.assertTrue(all(item["semanticSha256"] != item["imageSha256"]
                                for item in manifest["captures"]))
            self.assertEqual(set(manifest["assets"]),
                             {"corpusSha256", "metatileCatalogSha256",
                              "animationCatalogSha256", "rulesArtifactSha256"})

    def test_record_rejects_incomplete_capture_set(self):
        with tempfile.TemporaryDirectory() as temporary:
            with self.assertRaisesRegex(SurveyError, "missing capture"):
                record_captures(ROOT, self.plan, Path(temporary),
                                Path(temporary) / "manifest.json",
                                {"MAP_ROUTE115"}, commit="0" * 40,
                                current_plan=self.plan)

    def test_record_rejects_truncated_bmp(self):
        entry = next(item for item in self.plan["entries"]
                     if item["id"] == "MAP_LITTLEROOT_TOWN")
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / entry["captures"][0]["file"]).write_bytes(BMP_SIGNATURE + b"\0" * 24)
            with self.assertRaisesRegex(SurveyError, "not a BMP"):
                record_captures(ROOT, self.plan, root, root / "manifest.json",
                                {entry["map"]}, commit="0" * 40,
                                current_plan=self.plan)


if __name__ == "__main__":
    unittest.main()
