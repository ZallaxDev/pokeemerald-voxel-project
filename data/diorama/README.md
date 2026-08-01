# Diorama rules

These JSON files are strict G3 `schemaVersion: 2` sources. The game does not parse
them at runtime. The complete schema, normalized IR and proposed runtime C contract
are documented in `docs/diorama_g3_schema.md`.

Run validation, tests, and a Python/data-only compilation from the repository root:

```bash
python3 tools/diorama_rules/validate_rules.py
python3 tools/diorama_rules/test_compile_rules.py
python3 tools/diorama_rules/compile_rules.py \
  --output-c /tmp/diorama_rules.g3.c \
  --output-h /tmp/diorama_rules.g3.h
```

The compiler calls `catalog.load_tilesets()` and `catalog.load_world()` directly. Its
normalized output always includes deterministic IDs for all 75 logical tilesets,
mappings for all 441 layouts, and support rows for all 518 maps. Every cataloged map
uses the conservative G5 baseline; explicit map files may still add camera or visual
rule refinements. No retired G0 prototype is active.

`defaults.json` owns semantic pools, profiles/masks/samples, the fallback action,
global behavior/context/event/pattern rules and the global map baseline. `maps/` owns
explicit camera, ground policy and terrain inference mode refinements plus optional
local rules. `tilesets/` contains reviewed local metatile pins and may set
`terrainMode` to `manual`. Manual cells still resolve pins, behaviors and defaults,
but cannot be changed by blocked-art volume or plateau inference. This is the required
mode for partially authored rock families: unknown cells stay flat rather than becoming
speculative columns. The global map default is manual until the complete Red-equivalent
assembled-art detector replaces the residual run heuristic. No gameplay elevation is
converted into metric height.

Rules with no real placements are errors. The only escape hatches are structured
`allowedUnused: {"reason": "..."}` for rules/pins/presets and
`allowedNoPlacements: {"reason": "..."}` for exact patterns. IDs outside catalog
ranges, stale map/layout references, unknown keys and invalid claim masks are errors.

Schema v1 is never loaded implicitly. To migrate a supported v1 document explicitly:

```bash
python3 tools/diorama_rules/migrate_v1_to_v2.py old.json new.json
```

The migrator refuses retired overrides, building templates/placements and old
metatile shapes. Those require deliberate G3 authoring.

The canonical SHA-256 covers catalogs, pools, profiles, masks, samples, all rule
families, patterns, pins, presets and map rows. Generated runtime files remain
`include/diorama/rules.generated.h` and
`src/data/diorama/diorama_rules.generated.c`, but this Python/data task does not
overwrite them unless an alternate output is requested; the runtime consumes the v2
generated ABI.

The global visual catalog and contact sheets remain available with:

```bash
make -f Makefile_pc diorama-catalog
python3 tools/diorama_rules/g5_audit.py --check \
  --output build/diorama_catalog/g5_coverage.json
```
