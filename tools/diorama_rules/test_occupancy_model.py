#!/usr/bin/env python3

import unittest

from occupancy_model import Face, Span, build_shell, canonicalize, editor_geometry, merge_faces


MATERIALS = (1, 2, 3, 4, 5, 6)


class OccupancyModelTests(unittest.TestCase):
    def test_six_neighbor_closed_shell(self) -> None:
        faces = build_shell([Span(0, 0, -1, 1, 0, 0, MATERIALS)], (0, 0, 1, 1))
        self.assertEqual(len(faces), 6)
        self.assertEqual({face.axis for face in faces}, {0, 1, 2})

    def test_partial_overlap_preserves_exposed_intervals(self) -> None:
        spans = [Span(0, 0, 0, 4, 0, 0, MATERIALS),
                 Span(1, 0, 2, 6, 1, 0, MATERIALS)]
        faces = build_shell(spans, (0, 0, 2, 1))
        east = [face for face in faces if face.axis == 0 and face.sign == 1
                and face.source_x == 0]
        west = [face for face in faces if face.axis == 0 and face.sign == -1
                and face.source_x == 1]
        self.assertEqual([(face.v_min, face.v_max) for face in east], [(0, 2)])
        self.assertEqual([(face.v_min, face.v_max) for face in west], [(4, 6)])

    def test_chunk_owner_emits_only_owned_columns(self) -> None:
        spans = [Span(127, 0, 0, 1, 7, 0, MATERIALS),
                 Span(128, 0, 0, 1, 8, 0, MATERIALS)]
        west = build_shell(spans, (0, 0, 128, 128))
        east = build_shell(spans, (128, 0, 256, 128))
        self.assertFalse(any(face.axis == 0 and face.plane == 128 for face in west + east))
        self.assertTrue(all(face.source_x == 7 for face in west))
        self.assertTrue(all(face.source_x == 8 for face in east))

    def test_merging_requires_full_provenance(self) -> None:
        common = dict(axis=1, sign=1, plane=0, source_x=0, source_y=0,
                      flags=0, v_min=0, v_max=1)
        merged = merge_faces([Face(material=1, u_min=0, u_max=1, **common),
                              Face(material=1, u_min=1, u_max=2, **common)])
        split = merge_faces([Face(material=1, u_min=0, u_max=1, **common),
                             Face(material=2, u_min=1, u_max=2, **common)])
        self.assertEqual(len(merged), 1)
        self.assertEqual(len(split), 2)

    def test_canonicalization_is_deterministic(self) -> None:
        first = Span(0, 0, 0, 1, 0, 0, MATERIALS)
        second = Span(1, 0, -2, 2, 1, 0, MATERIALS)
        self.assertEqual(canonicalize([second, first]), canonicalize([first, second]))

    def test_bridge_keeps_lower_and_deck_surfaces(self) -> None:
        cell = {"x": 0, "y": 0, "behavior": 0x72}
        resolved = [{"rule": {"shape": "bridge", "archetype": "bridge",
                               "groundHeight": 1.75, "height": 0}}]
        geometry = editor_geometry([cell], resolved, [])
        column = [span for span in geometry["spans"] if span["x"] == 0 and span["z"] == 0]
        self.assertEqual([(span["y_min"], span["y_max"]) for span in column],
                          [(-3, -2), (27, 28)])

    def test_descending_stairs_open_below_ground(self) -> None:
        cell = {"x": 0, "y": 0, "behavior": 0}
        resolved = [{"rule": {"shape": "stairs", "archetype": "stairs-down-s",
                               "groundHeight": 0, "height": 1}}]
        geometry = editor_geometry([cell], resolved, [])
        self.assertEqual(min(span["y_min"] for span in geometry["spans"]), -17)
        self.assertEqual(max(span["y_max"] for span in geometry["spans"]), -4)

    def test_ledge_keeps_ground_flat_and_adds_only_directional_face(self) -> None:
        cell = {"x": 0, "y": 0, "behavior": 0x3B}
        resolved = [{"rule": {"shape": "ledge", "archetype": "ledge",
                               "groundHeight": 0, "height": 0.375}}]
        geometry = editor_geometry([cell], resolved, [])
        self.assertEqual({(span["y_min"], span["y_max"])
                          for span in geometry["spans"]}, {(-1, 0)})
        raised = [face for face in geometry["faces"] if face["v_max"] == 6]
        self.assertEqual(len(raised), 1)
        self.assertEqual((raised[0]["axis"], raised[0]["sign"], raised[0]["plane"]),
                         (2, 1, 16))


if __name__ == "__main__":
    unittest.main()
