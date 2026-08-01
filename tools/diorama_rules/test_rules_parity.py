#!/usr/bin/env python3

import subprocess
import tempfile
import unittest
from pathlib import Path

from compile_rules import ARCHETYPES, TERRAIN_CLASSES, compile_data


ROOT = Path(__file__).resolve().parents[2]


def assert_generated_parity(testcase: unittest.TestCase, data=None):
    data = data or compile_data(ROOT)
    with tempfile.TemporaryDirectory() as directory:
        driver = Path(directory) / "rules_parity"
        subprocess.run([
            "gcc", "-m32", "-std=gnu99", "-Wall", "-Wextra", "-Werror",
            "-D", "ENABLE_DIORAMA", "-iquote", str(ROOT / "include"),
            str(ROOT / "tests/diorama/rules_parity_driver.c"),
            str(ROOT / "src/diorama/rules.c"),
            str(ROOT / "src/data/diorama/diorama_rules.generated.c"),
            "-lm", "-o", str(driver),
        ], check=True, cwd=ROOT)
        output = subprocess.run([str(driver)], check=True, capture_output=True,
                                text=True, cwd=ROOT).stdout.splitlines()

    expected = [(layout["id"], record) for layout in data["layouts"]
                for record in layout["terrainRecords"]]
    testcase.assertEqual(len(output), len(expected))
    shape_ids = {name: index for index, name in enumerate(
        ("flat", "extruded", "cliff", "ledge", "stairs", "water", "bridge",
         "billboard", "cutout", "roof", "building-part", "hidden"))}
    archetype_ids = {name: index for index, name in enumerate(ARCHETYPES, 1)}
    terrain_ids = {name: index for index, name in enumerate(TERRAIN_CLASSES, 1)}
    source_kinds = {"pin:": 1, "behavior:": 3, "pattern:": 7,
                    "context:": 8, "animation:": 10}
    for line, (layout_id, record) in zip(output, expected):
        fields = line.split("\t")
        testcase.assertEqual(len(fields), 27)
        source_kind = next((value for prefix, value in source_kinds.items()
                            if record["source"].startswith(prefix)), 6)
        exact = [
            layout_id, record["cellOffset"], record["expectedMetatile"],
            record["mapGroup"], record["mapNumber"], record["class"],
            record["heightQ16"], record["artMode"], record["pool"],
            int(record["authored"]), record["source"],
        ]
        testcase.assertEqual(fields[:11], [str(value) for value in exact])
        testcase.assertAlmostEqual(float(fields[11]), record["confidence"], places=6)
        tail = [
            record["evidenceFlags"], "|".join(record["evidence"]),
            record["ambiguityFlags"], "|".join(record["ambiguity"]),
            int(record["propGroundMode"] == "manual"), record["propGroundMetatile"],
            shape_ids[record["shape"]], archetype_ids[record["archetype"]],
            terrain_ids[record["terrainClass"]], source_kind,
            {"x": 0, "z": 1, "cross": 2}[record["axis"]],
            record["cliffEdgeMask"], record["cliffBaseMask"],
            record["cliffTransitionMask"], record["cliffCornerMask"],
        ]
        testcase.assertEqual(fields[12:], [str(value) for value in tail])


class GeneratedRulesParityTests(unittest.TestCase):
    def test_every_compiled_classifier_record_matches_c(self):
        assert_generated_parity(self)


if __name__ == "__main__":
    unittest.main()
