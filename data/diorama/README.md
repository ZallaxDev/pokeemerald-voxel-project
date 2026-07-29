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

## Visual editor contract

Map rules may define an editor-friendly camera profile:

```json
"camera": { "profile": "interior", "pitch": 60, "focalLength": 145 }
```

`pitch` is expressed in degrees and both numeric fields are optional. The
compiled profile is immutable and selected from the snapshot's map and layout;
camera adjustments remain visual and never modify game state.
With terrain debug enabled (`F3`), `CAM:I` identifies an active interior profile,
`CAM:E` an exterior profile, and `CAM:-` a map without a curated profile. If the
debug panel does not appear, the scene is using the safe software fallback.

Export one map for the planned visual editor with:

```bash
python3 tools/diorama_rules/export_editor_map.py \
  MAP_LITTLEROOT_TOWN_BRENDANS_HOUSE_1F \
  --output /tmp/diorama-map.json
```

The export contains stable `x,y` cell IDs, original metatile, collision,
elevation, behavior and layer values, object/warp/background events, current
diorama rules, and paths to both tilesets' source graphics and binary tables.
It is a derived interchange file and must not be committed. The editor should
write only the corresponding file under `data/diorama/maps/`; the validator
rejects unknown fields, invalid coordinates and stale generated C tables.

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

## Overworld interface

Dialogue windows, the start menu, auxiliary field windows, and the map-name popup
are described by immutable UI profiles in the scene snapshot. The profile copies
only BG0 tile graphics, tilemap state, scroll, faded palettes, window rectangles,
and field/interface OAM classifications. The graphics thread renders BG0 index zero
as transparent alpha and composites the original 240x160 UI over the diorama, so
text printers, timing, palette colors, and accessibility hooks remain authoritative.

Battles, palette fades, unsupported weather, non-overworld callbacks, incomplete
snapshots, and renderer failures continue to present the complete software frame.
