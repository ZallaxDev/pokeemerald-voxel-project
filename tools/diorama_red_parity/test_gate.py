#!/usr/bin/env python3

import json
import unittest
from pathlib import Path

from check_red_parity_gate import requires_r0_neutrality, validate


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

    def test_r1_global_survey_contract_is_valid(self) -> None:
        validate(ROOT, "R1", True)

    def test_r2_full_metadata_contract_is_valid(self) -> None:
        validate(ROOT, "R2", True)

    def test_r3_full_metadata_contract_is_valid(self) -> None:
        validate(ROOT, "R3", True)

    def test_r4_full_metadata_contract_is_valid(self) -> None:
        validate(ROOT, "R4", True)

    def test_r0_neutrality_expires_when_r2_starts(self) -> None:
        pending = [{"state": "approved"}, {"state": "approved"},
                   {"state": "pending"}, {"state": "pending"}]
        implementation = [{"state": "approved"}, {"state": "approved"},
                          {"state": "implementation"}, {"state": "pending"}]
        self.assertTrue(requires_r0_neutrality(pending))
        self.assertFalse(requires_r0_neutrality(implementation))


if __name__ == "__main__":
    unittest.main()
