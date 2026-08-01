#!/usr/bin/env python3
"""Validate versioned Red-parity gates and R0 retirement invariants."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import subprocess
import sys
from pathlib import Path

STATES = {"pending", "implementation", "automatic-passed", "manual-pending", "approved"}
ACTIVE = {"implementation", "manual-pending"}


def _sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()
R0_SCENARIOS = {
    "R0-LITTLEROOT", "R0-ROUTE101", "R0-ROUTE104", "R0-ROUTE115",
    "R0-MT-CHIMNEY", "R0-CLASSIC-SMOKE",
}
R0_COMMANDS = [
    "make -f Makefile_pc test-diorama-red-parity PKG_CONFIG_32_PATH=/usr/lib32/pkgconfig:/usr/lib/pkgconfig:/usr/share/pkgconfig",
    "make -f Makefile_pc NATIVE_LINUX=1 DIORAMA=1 PKG_CONFIG_32_PATH=/usr/lib32/pkgconfig:/usr/lib/pkgconfig:/usr/share/pkgconfig -j$(nproc)",
    "make -f Makefile_pc NATIVE_LINUX=1 PKG_CONFIG_32_PATH=/usr/lib32/pkgconfig:/usr/lib/pkgconfig:/usr/share/pkgconfig -j$(nproc)",
]
R1_SCENARIOS = {"R1-LITTLEROOT-SURVEY", "R1-ROUTE115-SURVEY"}
R1_COMMANDS = list(R0_COMMANDS)


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
            required = ("binarySha256", "classicBinarySha256", "tester", "testedAtUtc",
                        "approval")
            if any(not phase.get(field) for field in required):
                fail(f"{phase['id']} approval metadata is incomplete")
            for field in ("binarySha256", "classicBinarySha256"):
                if not re.fullmatch(r"[0-9a-f]{64}", phase[field]):
                    fail(f"{phase['id']} {field} is invalid")
    scenario_by_id = {scenario["id"]: scenario for scenario in scenarios}
    if len(scenario_by_id) != len(scenarios) or not R0_SCENARIOS <= set(scenario_by_id):
        fail("R0 scenarios are missing or duplicated")
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
        required = {"phase", "renderer", "map", "layout", "position", "facing", "pitch",
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
            if len(run_hashes) != 2 or len(set(run_hashes)) != 2 \
                    or any(not re.fullmatch(r"[0-9a-f]{64}", value)
                           for value in run_hashes):
                fail("R1 approval requires two distinct validated survey manifests")
            manifest_dir = root / "build/diorama_survey"
            if not manifest_dir.is_dir():
                fail("R1 approval requires stored survey manifests")
            digests = {_sha256_file(path) for path in manifest_dir.glob("manifest*.json")}
            for value in run_hashes:
                if value not in digests:
                    fail("R1 approval hash does not match a stored survey manifest")
    ids = [entry["id"] for entry in resolvers]
    if len(ids) != len(set(ids)):
        fail("resolver responsibilities must be unique")
    if any(entry["owner"] is None and not entry["disposition"].startswith("replace-in-")
           for entry in resolvers):
        fail("an unowned resolver has no replacement phase")
    if not (root / "docs/diorama_red_parity_traceability.md").is_file():
        fail("traceability matrix is missing")
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
