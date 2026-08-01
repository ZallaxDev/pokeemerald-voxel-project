#!/usr/bin/env python3

import json
import unittest
from pathlib import Path

from check_red_parity_gate import validate


ROOT = Path(__file__).resolve().parents[2]


class RedParityGateTests(unittest.TestCase):
    def test_r0_metadata_and_retirements_are_valid(self) -> None:
        validate(ROOT, "R0", True)

    def test_r1_is_blocked_until_r0_approval(self) -> None:
        gates = json.loads((ROOT / "data/diorama/red_parity_gates.json").read_text(
            encoding="utf-8"))["phases"]
        if gates[0]["state"] == "approved":
            validate(ROOT, "R1", True)
        else:
            with self.assertRaisesRegex(ValueError, "R0 is not approved"):
                validate(ROOT, "R1", True)


if __name__ == "__main__":
    unittest.main()
