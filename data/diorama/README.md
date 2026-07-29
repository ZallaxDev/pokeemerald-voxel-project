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
behavior, collision/elevation, visual heuristic, and flat fallback. `AUTO` mode
renders any safe overworld snapshot; maps marked `supported: true` have curated
coverage, while all other maps use the generic resolver and may look incomplete.

Generated files are `include/diorama/rules.generated.h` and
`src/data/diorama/diorama_rules.generated.c`. Do not edit them manually.

## Connected maps

The scene snapshot can extend a cardinal connection beyond the gameplay map
buffer when both maps use the exact same primary and secondary tilesets. Each
copied cell keeps its source map, layout, and local
coordinates so map overrides and building templates resolve before the player
crosses the boundary. Connected object events are intentionally not synthesized;
the original game remains authoritative for active objects and gameplay state.

## Animated visuals

The snapshot publishes complete faded palettes, BG tile graphics, active weather,
reflection state, and the visible Surf attachment. The renderer compares complete
tile images so skipped snapshots cannot lose animation changes, then recomposes and
uploads only affected atlas slots. Palette changes intentionally refresh the full
atlas but do not rebuild terrain chunks.

Basic rain, horizontal fog, and volcanic ash render in 3D. Clear and sunny weather
need no overlay; all other weather types retain the software 2D fallback.
Reflections use the game's faded reflection palettes and bridge offsets, and a
per-pixel stencil excludes non-reflective terrain and foreground tile layers. The
Surf blob remains a visual attachment to the player and never changes movement,
collision, or elevation state.
