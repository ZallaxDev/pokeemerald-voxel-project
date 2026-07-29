# Diorama rules

These versioned JSON files are the editable source for the diorama rule tables.
The game does not parse JSON at runtime.

Run validation and regeneration from the repository root:

```bash
python3 tools/diorama_rules/validate_rules.py
python3 tools/diorama_rules/compile_rules.py
python3 tools/diorama_rules/compile_rules.py --check
```

`defaults.json` defines behavior rules. `tilesets/` uses tileset-local metatile
IDs, so secondary IDs start at zero rather than `0x200`. `maps/` contains
source-map coordinates without `MAP_OFFSET`. Building placements refer to
templates in `buildings/`.

Rules and building templates may define `faces` for `top`, `north`, `east`,
`south`, `west`, and `plane`. A face selects a source `metatile` (`self` or a
global ID) and the `full`, `base`, `foreground`, or `none` layer. Cutouts also
select an `axis`: `x`, `z`, or `cross`. Buildings are resolved as one
multi-cell structure, so internal faces are removed and roof patches share one
continuous profile.

Tileset rules may set `inferBase: true` when their lower tile entries exactly
match one unique ground metatile with an empty upper layer. Ambiguous matches
are rejected. Map `eventRules.sign` entries are expanded from the authoritative
`bg_events` coordinates, rather than duplicating sign positions by hand.

The resolver priority is map override/event rule, tileset metatile, building placement,
behavior, collision/elevation, visual heuristic, and flat fallback. A map must
have a rule file with `supported: true` to render in 3D in `AUTO` mode.

Generated files are `include/diorama/rules.generated.h` and
`src/data/diorama/diorama_rules.generated.c`. Do not edit them manually.
