#!/usr/bin/env python3
"""Explicitly migrate supported schema-v1 diorama JSON to schemaVersion 2.

Retired G0 prototypes are rejected rather than silently translated.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path


class MigrationError(ValueError):
    pass


def _action(rule: object, path: str) -> dict:
    if not isinstance(rule, dict):
        raise MigrationError(f"{path}: rule must be an object")
    unknown = set(rule) - {"shape", "groundHeight"}
    if unknown:
        raise MigrationError(f"{path}: cannot migrate retired or unsupported fields: {', '.join(sorted(unknown))}")
    shape = rule.get("shape", "flat")
    mapping = {"flat": ("ground", "terrain"), "ledge": ("ledge", "terrain"),
               "water": ("water", "water"), "hidden": ("void", "terrain")}
    if shape not in mapping:
        raise MigrationError(f"{path}: shape {shape!r} was retired and requires manual G3 authoring")
    archetype, pool = mapping[shape]
    output = {"archetype": archetype, "pool": pool}
    if "groundHeight" in rule:
        output["groundOffset"] = rule["groundHeight"]
    return output


def migrate_document(source: dict, path: str = "input") -> dict:
    if source.get("version") != 1:
        raise MigrationError(f"{path}: expected version 1")
    if "behaviors" in source or "default" in source:
        unknown = set(source) - {"version", "default", "behaviors"}
        if unknown:
            raise MigrationError(f"{path}: unknown fields: {', '.join(sorted(unknown))}")
        return {
            "schemaVersion": 2, "kind": "global",
            "semanticPools": [
                {"id": "terrain", "description": "Migrated terrain pool."},
                {"id": "water", "description": "Migrated water pool."},
            ],
            "profiles": [],
            "default": _action(source.get("default", {"shape": "flat"}), f"{path}.default"),
            "behaviorRules": [
                {"behavior": behavior, "action": _action(rule, f"{path}.behaviors.{behavior}")}
                for behavior, rule in sorted(source.get("behaviors", {}).items())
            ],
            "contextualRules": [], "exactPatterns": [],
             "mapDefault": {"supported": False, "reason": "No curated G3 map rule is available.",
                            "groundPolicy": {"mode": "automatic"},
                           "camera": {"profile": "exterior"}},
        }
    if "map" in source:
        unknown = set(source) - {"version", "map", "layout", "supported", "camera", "overrides", "buildings"}
        if unknown:
            raise MigrationError(f"{path}: unknown fields: {', '.join(sorted(unknown))}")
        if source.get("overrides") or source.get("buildings"):
            raise MigrationError(f"{path}: v1 overrides/buildings were retired in G0 and need manual G3 rules")
        supported = source.get("supported") is True
        status = {"supported": supported}
        if not supported:
            status["reason"] = "Migrated v1 map was not marked supported."
        return {"schemaVersion": 2, "kind": "map", "map": source.get("map"),
                "layout": source.get("layout"), "status": status,
                "camera": source.get("camera", {"profile": "exterior"}),
                "groundPolicy": {"mode": "automatic"},
                "contextualRules": [], "exactPatterns": []}
    if "tileset" in source:
        unknown = set(source) - {"version", "tileset", "role", "metatiles"}
        if unknown:
            raise MigrationError(f"{path}: unknown fields: {', '.join(sorted(unknown))}")
        if source.get("metatiles"):
            raise MigrationError(f"{path}: v1 metatile shapes need manual G3 pin migration")
        return {"schemaVersion": 2, "kind": "tileset", "tileset": source.get("tileset"),
                "pins": []}
    if "templates" in source:
        if source.get("templates"):
            raise MigrationError(f"{path}: v1 building templates were retired in G0")
        raise MigrationError(f"{path}: empty v1 building files have no v2 equivalent and should be removed")
    raise MigrationError(f"{path}: unrecognized v1 document kind")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    try:
        source = json.loads(args.input.read_text(encoding="utf-8"))
        migrated = migrate_document(source, str(args.input))
        args.output.write_text(json.dumps(migrated, indent=2, ensure_ascii=True) + "\n", encoding="utf-8")
    except (OSError, json.JSONDecodeError, MigrationError) as error:
        print(f"diorama v1 migration: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
