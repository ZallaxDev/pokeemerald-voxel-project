#!/usr/bin/env python3

import unittest
from pathlib import Path

from g5_audit import build_audit


ROOT = Path(__file__).resolve().parents[2]


class G5AuditTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.audit = build_audit(ROOT)

    def test_global_classification_is_exhaustive(self) -> None:
        classification = self.audit["classification"]
        self.assertEqual(classification["declaredCells"], 324579)
        self.assertEqual(classification["staticallyDecodedCells"], 323071)
        self.assertEqual(classification["dynamicTilesetCells"], 1508)
        self.assertEqual(classification["tilesetsDeclared"], 75)
        self.assertEqual(classification["unclassifiedCells"], 0)
        self.assertEqual(classification["coordinateOverrides"], 0)
        self.assertEqual(set(classification["declaredClasses"]),
                         set(classification["classCounts"]))

    def test_cliffs_and_reference_profiles_have_automated_evidence(self) -> None:
        cliffs = self.audit["cliffs"]
        self.assertGreaterEqual(cliffs["explicitReusablePins"], 6)
        self.assertGreater(cliffs["explicitPinPlacements"], 0)
        self.assertEqual(cliffs["maximumBands"], 3)
        self.assertEqual(len(self.audit["referenceProfiles"]), 6)
        self.assertTrue(all(row["automatedPass"]
                            for row in self.audit["referenceProfiles"]))

    def test_manual_approval_is_deliberately_pending(self) -> None:
        self.assertEqual(self.audit["manualValidation"]["status"],
                         "pending-user-confirmation")
        self.assertFalse(self.audit["manualValidation"]["approved"])


if __name__ == "__main__":
    unittest.main()
