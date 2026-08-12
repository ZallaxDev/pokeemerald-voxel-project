#!/usr/bin/env python3
"""Validate versioned Red-parity gates and R0 retirement invariants."""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from pathlib import Path

STATES = {"pending", "implementation", "automatic-passed", "manual-pending", "approved"}
ACTIVE = {"implementation", "manual-pending"}
R0_SCENARIOS = {
    "R0-LITTLEROOT", "R0-ROUTE101", "R0-ROUTE104", "R0-ROUTE115",
    "R0-MT-CHIMNEY", "R0-CLASSIC-SMOKE",
}
R0_COMMANDS = [
    "make -f Makefile_pc test-diorama-red-parity PKG_CONFIG_32_PATH=/usr/lib32/pkgconfig:/usr/lib/pkgconfig:/usr/share/pkgconfig",
    "make -f Makefile_pc NATIVE_LINUX=1 DIORAMA=1 PKG_CONFIG_32_PATH=/usr/lib32/pkgconfig:/usr/lib/pkgconfig:/usr/share/pkgconfig -j$(nproc)",
    "make -f Makefile_pc NATIVE_LINUX=1 PKG_CONFIG_32_PATH=/usr/lib32/pkgconfig:/usr/lib/pkgconfig:/usr/share/pkgconfig -j$(nproc)",
]
R1_SCENARIOS = {"R1-LITTLEROOT-SURVEY"}
R1_COMMANDS = list(R0_COMMANDS)
R2_SCENARIOS = {
    "R2-ROUTE101-CLASSIFIER",
    "R2-ROUTE115-CLASSIFIER",
    "R2-GRANITE-CAVE-B1F-CLASSIFIER",
    "R2-ROUTE115-AUTHORITATIVE-PIN-BLAST-RADIUS",
    "R2-CLASSIC-DIORAMA-REGRESSION-SMOKE",
}
R2_COMMANDS = list(R0_COMMANDS)
R2_PITCHES = [0, 15, 35, 50, 75]
R2_LOCATIONS = {
    "R2-ROUTE101-CLASSIFIER": (
        "MAP_ROUTE101", "LAYOUT_ROUTE101", {"x": 10, "y": 10}, "norte"),
    "R2-ROUTE115-CLASSIFIER": (
        "MAP_ROUTE115", "LAYOUT_ROUTE115", {"x": 18, "y": 41}, "norte"),
    "R2-GRANITE-CAVE-B1F-CLASSIFIER": (
        "MAP_GRANITE_CAVE_B1F", "LAYOUT_GRANITE_CAVE_B1F",
        {"x": 25, "y": 14}, "norte"),
    "R2-ROUTE115-AUTHORITATIVE-PIN-BLAST-RADIUS": (
        "MAP_ROUTE115", "LAYOUT_ROUTE115", {"x": 21, "y": 63}, "norte"),
    "R2-CLASSIC-DIORAMA-REGRESSION-SMOKE": (
        "MAP_LITTLEROOT_TOWN", "LAYOUT_LITTLEROOT_TOWN", {"x": 10, "y": 10}, "sur"),
}
R2_AI_DECISION = {
    "status": "not-run",
    "authoritative": False,
    "buildDependency": False,
    "runtimeDependency": False,
}
R2_CLASSIFIER_EVIDENCE = {
    "rulesSha256": "55bfb6c93c2092d9059bdfe629212db231ce8da833f4fa019ae5073f37eed2b8",
    "compiledRecords": 324579,
    "ambiguityGroups": 95,
    "fallbackPlacements": 250585,
    "pythonCParityRecords": 324579,
    "authoritativePin": "gTileset_General:4",
    "authoritativePinPlacements": 615,
    "flowerAnimationSources": 4,
}
R3_SCENARIOS = {
    "R3-LITTLEROOT-CLAIMS", "R3-ROUTE115-REGIONS", "R3-FORTREE-CLAIMS",
    "R3-POKEMON-CENTER-CLAIMS", "R3-CLASSIC-DIORAMA-REGRESSION-SMOKE",
}
R3_COMMANDS = list(R0_COMMANDS)
R3_PITCHES = [0, 15, 35, 50, 75]
R3_LOCATIONS = {
    "R3-LITTLEROOT-CLAIMS": (
        "MAP_LITTLEROOT_TOWN", "LAYOUT_LITTLEROOT_TOWN", {"x": 5, "y": 9}, "norte"),
    "R3-ROUTE115-REGIONS": (
        "MAP_ROUTE115", "LAYOUT_ROUTE115", {"x": 18, "y": 41}, "norte"),
    "R3-FORTREE-CLAIMS": (
        "MAP_FORTREE_CITY", "LAYOUT_FORTREE_CITY", {"x": 5, "y": 7}, "norte"),
    "R3-POKEMON-CENTER-CLAIMS": (
        "MAP_OLDALE_TOWN_POKEMON_CENTER_1F", "LAYOUT_POKEMON_CENTER_1F",
        {"x": 7, "y": 7}, "norte"),
    "R3-CLASSIC-DIORAMA-REGRESSION-SMOKE": (
        "MAP_LITTLEROOT_TOWN", "LAYOUT_LITTLEROOT_TOWN", {"x": 10, "y": 10}, "sur"),
}
R3_STRUCTURE_EVIDENCE = {
    "rulesSha256": "41833ce1b4d7de654ba744e77ec19ab737c94dced03afeb5a738e554580d4c31",
    "compiledRecords": 324579,
    "pythonCParityRecords": 324579,
    "structureCandidates": 2923,
    "templateClaims": 8,
    "propCandidates": 634,
    "regions": 2281,
    "derivedVoidCells": 3863,
    "doorFolds": 0,
}
R4_SCENARIOS = {
    "R4-LITTLEROOT-SIGN", "R4-OLDALE-HOUSE1-PLANTS",
    "R4-BRENDAN-HOUSE-TV-SUPPORT", "R4-MAUVILLE-HOUSE1-STOOLS",
    "R4-DEWFORD-HALL-RELIEF", "R4-OLDALE-SIGN-SHARED-TILESET",
    "R4-ROUTE104-CUT-TREE-BILLBOARD", "R4-FIERY-PATH-BOULDER-BILLBOARD",
    "R4-CLASSIC-DIORAMA-REGRESSION-SMOKE",
}
R4_COMMANDS = list(R0_COMMANDS)
R4_PITCHES = [0, 15, 35, 50, 75]
R4_LOCATIONS = {
    "R4-LITTLEROOT-SIGN": ("MAP_LITTLEROOT_TOWN", "LAYOUT_LITTLEROOT_TOWN",
                             {"x": 15, "y": 14}, "norte"),
    "R4-OLDALE-HOUSE1-PLANTS": ("MAP_OLDALE_TOWN_HOUSE1", "LAYOUT_HOUSE1",
                                  {"x": 8, "y": 3}, "norte"),
    "R4-BRENDAN-HOUSE-TV-SUPPORT": ("MAP_LITTLEROOT_TOWN_BRENDANS_HOUSE_1F",
                                      "LAYOUT_LITTLEROOT_TOWN_BRENDANS_HOUSE_1F",
                                      {"x": 5, "y": 4}, "oeste"),
    "R4-MAUVILLE-HOUSE1-STOOLS": ("MAP_MAUVILLE_CITY_HOUSE1", "LAYOUT_HOUSE2",
                                    {"x": 7, "y": 6}, "norte"),
    "R4-DEWFORD-HALL-RELIEF": ("MAP_DEWFORD_TOWN_HALL", "LAYOUT_DEWFORD_TOWN_HALL",
                                 {"x": 3, "y": 6}, "norte"),
    "R4-OLDALE-SIGN-SHARED-TILESET": ("MAP_OLDALE_TOWN", "LAYOUT_OLDALE_TOWN",
                                        {"x": 11, "y": 10}, "norte"),
    "R4-ROUTE104-CUT-TREE-BILLBOARD": ("MAP_ROUTE104", "LAYOUT_ROUTE104",
                                         {"x": 34, "y": 22}, "este"),
    "R4-FIERY-PATH-BOULDER-BILLBOARD": ("MAP_FIERY_PATH", "LAYOUT_FIERY_PATH",
                                          {"x": 7, "y": 11}, "este"),
    "R4-CLASSIC-DIORAMA-REGRESSION-SMOKE": ("MAP_LITTLEROOT_TOWN",
                                               "LAYOUT_LITTLEROOT_TOWN",
                                               {"x": 10, "y": 10}, "sur"),
}
R4_PIXEL_OBJECT_EVIDENCE = {
    "rulesSha256": "c78c4030e5913997e48126f529aeccc7aa73070a9337e6b2531fd4d1ed2b4a6e",
    "compiledRecords": 325276,
    "pythonCParityRecords": 325276,
    "structureCandidates": 2959,
    "pixelObjects": 31,
    "cutouts": 30,
    "reliefs": 1,
    "sourcePixels": 6364,
    "maskRows": 592,
    "authoredSupports": 2,
    "replacementGroundObjects": 7,
    "sourceBaseObjects": 24,
    "goldenMasks": 7,
    "colorDistance": 7,
    "blackMaxChannel": 90,
    "darkMaxChannel": 150,
    "lightMaxChannel": 220,
    "maxUprightHeight": 64,
    "maxSolidRatio": 0.82,
    "repetitiveRowRatio": 0.75,
    "reliefDepthQ32": 10,
}
R5_SCENARIOS = {
    "R5-ROUTE115-CLIFF-RUNS", "R5-MT-CHIMNEY-REPEATED-RIDGES",
    "R5-JAGGED-PASS-BIDIRECTIONAL-RUNS", "R5-FORTREE-FOREST-REPETITION",
    "R5-GRANITE-CAVE-B1F-WALL-EXTENT", "R5-CLAIMED-CONTENT-EXCLUSION",
}
R5_COMMANDS = list(R0_COMMANDS)
R5_PITCHES = [0, 15, 35, 50, 75]
R5_LOCATIONS = {
    "R5-ROUTE115-CLIFF-RUNS": (
        "MAP_ROUTE115", "LAYOUT_ROUTE115", {"x": 18, "y": 41}, "norte"),
    "R5-MT-CHIMNEY-REPEATED-RIDGES": (
        "MAP_MT_CHIMNEY", "LAYOUT_MT_CHIMNEY", {"x": 20, "y": 23}, "sur"),
    "R5-JAGGED-PASS-BIDIRECTIONAL-RUNS": (
        "MAP_JAGGED_PASS", "LAYOUT_JAGGED_PASS", {"x": 16, "y": 25}, "norte"),
    "R5-FORTREE-FOREST-REPETITION": (
        "MAP_FORTREE_CITY", "LAYOUT_FORTREE_CITY", {"x": 5, "y": 7}, "norte"),
    "R5-GRANITE-CAVE-B1F-WALL-EXTENT": (
        "MAP_GRANITE_CAVE_B1F", "LAYOUT_GRANITE_CAVE_B1F",
        {"x": 25, "y": 14}, "norte"),
    "R5-CLAIMED-CONTENT-EXCLUSION": (
        "MAP_OLDALE_TOWN_POKEMON_CENTER_1F", "LAYOUT_POKEMON_CENTER_1F",
        {"x": 7, "y": 7}, "norte"),
}


def requires_r0_neutrality(gates: list[dict]) -> bool:
    """R0's empty-rule baseline expires when classifier work starts in R2."""
    return all(phase["state"] == "pending" for phase in gates[2:])


def load(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def fail(message: str) -> None:
    raise ValueError(message)


def validate(root: Path, phase_id: str, require_previous: bool) -> None:
    gates = load(root / "data/diorama/red_parity_gates.json")["phases"]
    scenario_document = load(root / "data/diorama/red_parity_scenarios.json")
    scenarios = scenario_document["scenarios"]
    resolvers = load(root / "data/diorama/red_parity_resolvers.json")["responsibilities"]
    denylist = load(root / "data/diorama/red_parity_denylist.json")
    expected_phases = [f"R{index}" for index in range(11)]
    if [phase["id"] for phase in gates] != expected_phases:
        fail("gates must contain R0 through R10 in order")
    if any(phase.get("state") not in STATES for phase in gates):
        fail("gate contains an invalid state")
    if sum(phase["state"] in ACTIVE for phase in gates) > 1:
        fail("only one phase may be implementation or manual-pending")
    by_phase = {phase["id"]: phase for phase in gates}
    if phase_id not in by_phase:
        fail(f"unknown phase {phase_id}")
    index = expected_phases.index(phase_id)
    if require_previous:
        for previous in expected_phases[:index]:
            if by_phase[previous]["state"] != "approved":
                fail(f"{previous} is not approved")
    for phase_index, phase in enumerate(gates):
        if phase["state"] != "pending" \
                and any(previous["state"] != "approved" for previous in gates[:phase_index]):
            fail(f"{phase['id']} advanced before all previous phases were approved")
        if phase["state"] == "approved":
            waived = phase.get("automaticClosureWaivedByUser") is True
            required = (("tester", "testedAtUtc", "approval", "automaticClosureWaiver")
                        if waived else
                        ("binarySha256", "classicBinarySha256", "tester", "testedAtUtc",
                         "approval"))
            if any(not phase.get(field) for field in required):
                fail(f"{phase['id']} approval metadata is incomplete")
            if not waived:
                for field in ("binarySha256", "classicBinarySha256"):
                    if not re.fullmatch(r"[0-9a-f]{64}", phase[field]):
                        fail(f"{phase['id']} {field} is invalid")
    scenario_by_id = {scenario["id"]: scenario for scenario in scenarios}
    required_scenarios = (R0_SCENARIOS | R1_SCENARIOS | R2_SCENARIOS | R3_SCENARIOS
                          | R4_SCENARIOS | R5_SCENARIOS)
    if len(scenario_by_id) != len(scenarios) or not required_scenarios <= set(scenario_by_id):
        fail("required Red-parity scenarios are missing or duplicated")
    sys.path.insert(0, str(root / "tools/diorama_rules"))
    from catalog import load_tilesets, load_world
    catalog_maps, catalog_layouts = load_world(root)
    catalog_tilesets = load_tilesets(root)
    if (len(catalog_maps), len(catalog_layouts), len(catalog_tilesets)) != (518, 441, 75):
        fail("repository catalog cardinality is not 518 maps, 441 layouts and 75 tilesets")
    if scenario_document.get("scope", {}).get("maps") != len(catalog_maps) \
            or scenario_document.get("scope", {}).get("layouts") != 441 \
            or scenario_document.get("scope", {}).get("tilesets") != 75:
        fail("scenario authority must cover the complete Emerald catalog")
    maps_by_symbol = {row["symbol"]: row for row in catalog_maps}
    layouts_by_symbol = {row["id"]: row for row in catalog_layouts}
    for scenario in scenarios:
        required = {"id", "phase", "renderer", "map", "layout", "position", "facing", "pitch",
                     "resolution", "setup", "expected", "roundTrip"}
        if not required <= set(scenario):
            fail(f"{scenario['id']} is not reproducible")
        if not all(isinstance(scenario["position"].get(axis), int) for axis in ("x", "y")):
            fail(f"{scenario['id']} has invalid coordinates")
        map_row = maps_by_symbol.get(scenario["map"])
        layout = layouts_by_symbol.get(scenario["layout"])
        if map_row is None or layout is None or map_row["layout"] != scenario["layout"]:
            fail(f"{scenario['id']} has an invalid map/layout pair")
        if not (0 <= scenario["position"]["x"] < layout["width"]
                and 0 <= scenario["position"]["y"] < layout["height"]):
            fail(f"{scenario['id']} position is outside its layout")
        if not scenario["id"].startswith(f"{scenario['phase']}-"):
            fail(f"{scenario['id']} does not match its phase")
        resolution = scenario["resolution"]
        if set(resolution) != {"width", "height"} \
                or not all(isinstance(resolution[axis], int) and resolution[axis] > 0
                           for axis in ("width", "height")):
            fail(f"{scenario['id']} has an invalid resolution")
    r0 = by_phase["R0"]
    if set(r0["manualScenarios"]) != R0_SCENARIOS \
            or r0["automaticCommands"] != R0_COMMANDS:
        fail("R0 gate commands or scenarios do not match the phase contract")
    if r0.get("manualGuide") != "docs/diorama_red_parity_manual_testing.md" \
            or not (root / r0["manualGuide"]).is_file():
        fail("R0 Spanish manual guide is missing")
    r1 = by_phase["R1"]
    if r1["state"] != "pending":
        if set(r1.get("manualScenarios", [])) != R1_SCENARIOS \
                or r1.get("automaticCommands") != R1_COMMANDS:
            fail("R1 gate commands or scenarios do not match the phase contract")
        sys.path.insert(0, str(root / "tools/diorama_survey"))
        from survey import build_plan
        plan = build_plan(root)
        expected_summary = {"maps": 518, "layouts": 441, "tilesets": 75,
                            "mapSpots": 518, "layoutOnly": 35, "tilesetOnly": 2,
                            "captureNames": 2590}
        if plan["summary"] != expected_summary:
            fail("R1 global survey plan is incomplete")
        if any(len(entry["captures"]) != 5 for entry in plan["entries"]
               if entry["kind"] == "map"):
            fail("R1 map spots must define exactly five views")
        plan_by_map = {entry["map"]: entry for entry in plan["entries"]
                       if entry["kind"] == "map"}
        facing_aliases = {"sur": "south", "norte": "north",
                          "oeste": "west", "este": "east"}
        for scenario_id in R1_SCENARIOS:
            scenario = scenario_by_id[scenario_id]
            entry = plan_by_map[scenario["map"]]
            if entry["position"] != scenario["position"] \
                    or entry["facing"] != facing_aliases.get(scenario["facing"],
                                                             scenario["facing"]):
                fail(f"{scenario_id} does not match the global survey plan")
        tracked = subprocess.run(["git", "ls-files"], cwd=root, check=True,
                                 text=True, stdout=subprocess.PIPE).stdout.splitlines()
        forbidden = ("build/diorama_catalog/", "build/diorama_survey/")
        if any(path.startswith(forbidden) for path in tracked):
            fail("generated Diorama catalog or survey output is tracked")
        if r1["state"] == "approved":
            run_hashes = r1.get("manualRunSha256", [])
            if len(run_hashes) != 1 or not re.fullmatch(r"[0-9a-f]{64}", run_hashes[0]):
                fail("R1 approval requires one validated survey manifest")
            evidence = r1.get("manualEvidence", {})
            entry = plan_by_map["MAP_LITTLEROOT_TOWN"]
            expected_evidence = {
                "manifestSha256": run_hashes[0], "map": entry["map"],
                "mapGroup": entry["mapGroup"], "mapNum": entry["mapNum"],
                "layoutId": entry["layoutId"], "position": entry["position"],
                "facing": entry["facing"], "captureRun": 1,
            }
            if any(evidence.get(key) != value for key, value in expected_evidence.items()):
                fail("R1 manual evidence does not match the approved scenario")
            view_hashes = evidence.get("views", {})
            if set(view_hashes) != {"flat", "v15", "v35", "v50", "v75"} \
                    or any(not re.fullmatch(r"[0-9a-f]{64}", value)
                           for value in view_hashes.values()):
                fail("R1 manual evidence does not contain five valid image hashes")
            identity = evidence.get("snapshotIdentity", {})
            identity_fields = ("snapshotSequence", "mapGeneration", "mapEditGeneration",
                               "paletteGeneration", "objPaletteGeneration",
                               "animationGeneration")
            if set(identity) != set(identity_fields) \
                    or not all(isinstance(identity[field], int) for field in identity_fields):
                fail("R1 manual evidence has an invalid snapshot identity")
    r2 = by_phase["R2"]
    if r2["state"] != "pending":
        if set(r2.get("manualScenarios", [])) != R2_SCENARIOS \
                or len(r2["manualScenarios"]) != len(R2_SCENARIOS) \
                or r2.get("automaticCommands") != R2_COMMANDS:
            fail("R2 gate commands or scenarios do not match the phase contract")
        if r2.get("manualGuide") != "docs/diorama_red_parity_manual_testing.md" \
                or not (root / r2["manualGuide"]).is_file():
            fail("R2 Spanish manual guide is missing")
        if r2.get("gameplayAuthority") != "original-game-only-no-teleport":
            fail("R2 must preserve original gameplay authority and forbid teleport setup")
        if r2.get("aiDecision") != R2_AI_DECISION:
            fail("R2 optional AI decision must be recorded as non-authoritative not-run")
        if r2["state"] in {"automatic-passed", "manual-pending", "approved"}:
            for field in ("binarySha256", "classicBinarySha256"):
                if not re.fullmatch(r"[0-9a-f]{64}", r2.get(field, "")):
                    fail(f"R2 {field} is missing after automatic validation")
            if not r2.get("automaticTestedAtUtc"):
                fail("R2 automatic validation timestamp is missing")
            if r2.get("classifierEvidence") != R2_CLASSIFIER_EVIDENCE:
                fail("R2 compiled classifier evidence does not match the validated corpus")
        for scenario_id in R2_SCENARIOS:
            scenario = scenario_by_id[scenario_id]
            expected_map, expected_layout, expected_position, expected_facing = \
                R2_LOCATIONS[scenario_id]
            if scenario["phase"] != "R2" or scenario["renderer"] != "classic-and-diorama" \
                    or scenario["map"] != expected_map \
                    or scenario["layout"] != expected_layout \
                    or scenario["position"] != expected_position \
                    or scenario["facing"] != expected_facing \
                    or scenario["pitch"] != R2_PITCHES \
                    or scenario["resolution"] != {"width": 960, "height": 640}:
                fail(f"{scenario_id} does not match the R2 capture contract")
            if "sin teletransporte" not in scenario["setup"] \
                    or "teletransporte" not in scenario["roundTrip"]:
                fail(f"{scenario_id} must use normal gameplay traversal without teleport")
        pin_review = scenario_by_id["R2-ROUTE115-AUTHORITATIVE-PIN-BLAST-RADIUS"]
        expected_review = {
            "kind": "authoritative-pin-blast-radius",
            "authority": "human-reviewed-tileset-metatile",
            "anchorOnly": False,
            "scope": "all-catalog-occurrences",
            "requiredOutputs": ["class", "height", "artMode", "pool", "authored",
                                "source", "evidence"],
        }
        if pin_review.get("review") != expected_review:
            fail("R2 authoritative pin review does not cover its complete blast radius")
    r3 = by_phase["R3"]
    if r3["state"] != "pending":
        if set(r3.get("manualScenarios", [])) != R3_SCENARIOS \
                or len(r3["manualScenarios"]) != len(R3_SCENARIOS) \
                or r3.get("automaticCommands") != R3_COMMANDS:
            fail("R3 gate commands or scenarios do not match the phase contract")
        if r3.get("manualGuide") != "docs/diorama_red_parity_manual_testing.md" \
                or not (root / r3["manualGuide"]).is_file():
            fail("R3 Spanish manual guide is missing")
        if r3.get("gameplayAuthority") != "original-game-only-no-teleport":
            fail("R3 must preserve original gameplay authority")
        if r3["state"] in {"automatic-passed", "manual-pending", "approved"}:
            for field in ("binarySha256", "classicBinarySha256"):
                if not re.fullmatch(r"[0-9a-f]{64}", r3.get(field, "")):
                    fail(f"R3 {field} is missing after automatic validation")
            if not r3.get("automaticTestedAtUtc"):
                fail("R3 automatic validation timestamp is missing")
            if r3.get("structureEvidence") != R3_STRUCTURE_EVIDENCE:
                fail("R3 compiled structure evidence does not match the validated corpus")
        for scenario_id in R3_SCENARIOS:
            scenario = scenario_by_id[scenario_id]
            expected_map, expected_layout, expected_position, expected_facing = \
                R3_LOCATIONS[scenario_id]
            if scenario["phase"] != "R3" or scenario["renderer"] != "classic-and-diorama" \
                    or scenario["map"] != expected_map \
                    or scenario["layout"] != expected_layout \
                    or scenario["position"] != expected_position \
                    or scenario["facing"] != expected_facing \
                    or scenario["pitch"] != R3_PITCHES \
                    or scenario["resolution"] != {"width": 960, "height": 640}:
                fail(f"{scenario_id} does not match the R3 capture contract")
    r4 = by_phase["R4"]
    if r4["state"] != "pending":
        if set(r4.get("manualScenarios", [])) != R4_SCENARIOS \
                or len(r4["manualScenarios"]) != len(R4_SCENARIOS) \
                or r4.get("automaticCommands") != R4_COMMANDS:
            fail("R4 gate commands or scenarios do not match the phase contract")
        if r4.get("manualGuide") != "docs/diorama_red_parity_manual_testing.md" \
                or not (root / r4["manualGuide"]).is_file():
            fail("R4 Spanish manual guide is missing")
        if r4.get("gameplayAuthority") != "original-game-only-no-teleport":
            fail("R4 must preserve original gameplay authority")
        if r4["state"] in {"automatic-passed", "manual-pending", "approved"} \
                and not r4.get("automaticClosureWaivedByUser"):
            for field in ("binarySha256", "classicBinarySha256"):
                if not re.fullmatch(r"[0-9a-f]{64}", r4.get(field, "")):
                    fail(f"R4 {field} is missing after automatic validation")
            if not r4.get("automaticTestedAtUtc"):
                fail("R4 automatic evidence is incomplete")
            if r4.get("pixelObjectEvidence") != R4_PIXEL_OBJECT_EVIDENCE:
                fail("R4 pixel-object evidence does not match the validated corpus")
        for scenario_id in R4_SCENARIOS:
            scenario = scenario_by_id[scenario_id]
            expected_map, expected_layout, expected_position, expected_facing = \
                R4_LOCATIONS[scenario_id]
            if scenario["phase"] != "R4" or scenario["renderer"] != "classic-and-diorama" \
                    or scenario["map"] != expected_map \
                    or scenario["layout"] != expected_layout \
                    or scenario["position"] != expected_position \
                    or scenario["facing"] != expected_facing \
                    or scenario["pitch"] != R4_PITCHES \
                    or scenario["resolution"] != {"width": 960, "height": 640}:
                fail(f"{scenario_id} does not match the R4 capture contract")
    r5 = by_phase["R5"]
    if r5["state"] != "pending":
        if set(r5.get("manualScenarios", [])) != R5_SCENARIOS \
                or len(r5["manualScenarios"]) != len(R5_SCENARIOS) \
                or r5.get("automaticCommands") != R5_COMMANDS:
            fail("R5 gate commands or scenarios do not match the phase contract")
        if r5.get("manualGuide") != "docs/diorama_red_parity_manual_testing.md" \
                or not (root / r5["manualGuide"]).is_file():
            fail("R5 Spanish manual guide is missing")
        if r5.get("gameplayAuthority") != "original-game-only-no-teleport":
            fail("R5 must preserve original gameplay authority")
        for scenario_id in R5_SCENARIOS:
            scenario = scenario_by_id[scenario_id]
            expected_map, expected_layout, expected_position, expected_facing = \
                R5_LOCATIONS[scenario_id]
            if scenario["phase"] != "R5" or scenario["renderer"] != "classic-and-diorama" \
                    or scenario["map"] != expected_map \
                    or scenario["layout"] != expected_layout \
                    or scenario["position"] != expected_position \
                    or scenario["facing"] != expected_facing \
                    or scenario["pitch"] != R5_PITCHES \
                    or scenario["resolution"] != {"width": 960, "height": 640}:
                fail(f"{scenario_id} does not match the R5 capture contract")
    ids = [entry["id"] for entry in resolvers]
    if len(ids) != len(set(ids)):
        fail("resolver responsibilities must be unique")
    if any(entry["owner"] is None and not entry["disposition"].startswith("replace-in-")
           for entry in resolvers):
        fail("an unowned resolver has no replacement phase")
    if not (root / "docs/diorama_red_parity_traceability.md").is_file():
        fail("traceability matrix is missing")
    if requires_r0_neutrality(gates):
        defaults = load(root / "data/diorama/defaults.json")
        neutral_sections = ("profiles", "behaviorRules", "contextualRules", "exactPatterns")
        if any(defaults.get(section) for section in neutral_sections):
            fail("R0 defaults contain inherited classification or geometry")
        if any((root / "data/diorama/maps").glob("*.json")) \
                or any((root / "data/diorama/tilesets").glob("*.json")):
            fail("R0 contains inherited map or tileset rules")
        generated = (root / "src/data/diorama/diorama_rules.generated.c").read_text(
            encoding="utf-8")
        empty_counts = ("Terrain", "BehaviorRule", "Profile", "Mask", "MaskRow", "Sample",
                        "TilesetPin", "ContextualRule", "ExactPattern")
        for name in empty_counts:
            if f"const size_t gDiorama{name}V2Count = 0;" not in generated:
                fail(f"R0 generated {name} table is not empty")
    excluded = tuple(denylist["excludedRoots"])
    scan_roots = (root / "src/diorama", root / "src/data/diorama", root / "include/diorama",
                  root / "tools/diorama_rules", root / "data/diorama")
    tokens = denylist["symbols"] + denylist["fields"]
    for scan_root in scan_roots:
        for path in scan_root.rglob("*"):
            if not path.is_file() or path.suffix not in {".c", ".h", ".py", ".json"}:
                continue
            relative = path.relative_to(root)
            if relative.parts and relative.parts[0] in excluded:
                continue
            text = path.read_text(encoding="utf-8")
            for token in tokens:
                if token in text and path.name != "red_parity_denylist.json":
                    fail(f"retired token {token!r} remains in {relative}")
    for retired in denylist["files"]:
        if (root / retired).exists():
            fail(f"retired file still exists: {retired}")
    for path in (root / "src/diorama").rglob("*.c"):
        if re.search(r"#\s*if\s+0\b", path.read_text(encoding="utf-8")):
            fail(f"retired #if 0 remains in {path.relative_to(root)}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--phase", required=True)
    parser.add_argument("--require-approved-previous", action="store_true")
    args = parser.parse_args()
    try:
        validate(args.root.resolve(), args.phase, args.require_approved_previous)
    except (OSError, json.JSONDecodeError, ValueError, subprocess.CalledProcessError) as error:
        print(f"red parity gate: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
