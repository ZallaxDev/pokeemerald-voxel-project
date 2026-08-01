# Diorama visual editor

Local browser and editor for the versioned visual metadata in `data/diorama/maps/`.
It lists all 518 maps and previews the shared G5 occupancy model. Saving a map without
an explicit rule file creates its map-scoped visual document. It never
writes gameplay maps, layouts, tilesets, graphics, events, collision, or assets.

Run it from the repository root:

```bash
python3 map_editor/server.py
```

Then open `http://127.0.0.1:8765/`. Use `--port` to select another port and
`--no-browser` to avoid opening a browser automatically.

The editor uses only Python's standard library and browser APIs. Metatile atlases
are composed by the shared `tools/diorama_rules/emerald_compositor.py` module
from the original tiles, metatile tables, and palettes; the server does not keep
an independent decoder.
The Terrain selector edits the map's `terrainMode` independently from its ground
material policy. Manual mode previews the same inference exclusion used by the compiler.
Geometry and face-material edits are staged as reusable tileset/metatile pins and affect
every map using that tileset. They remain in browser memory until `Save rules` is pressed;
closing or changing maps asks before discarding them. Use cliff `height` for a terrace
course. Do not use absolute `groundOffset` to number terrace floors. To author those
floors or local visual components, select cells from one connected region, set
`Visual base level`, and use `Stage selected region geometry`. Negative levels are
valid for sand and water. Walls and other extruded regions can also store their local
`Relative height`. The compiler expands that guarded map anchor without affecting
identical metatiles in other regions.
The region button also persists `Shape`, `Reusable archetype`, `Terrain class`, and
`Axis` for the selected cells. Use the global tileset pin only when that classification
is valid for every occurrence of the metatile.
Saving validates a candidate copy first, checks for external changes, atomically
replaces only the selected `data/diorama/maps/*.json`, and runs the validator and
deterministic rule compiler.

G3 uses strict schema v2 sources. The backend reads the generated tileset catalog and
normalizes the same actions used by the compiler. The existing point-editing UI is a
compatibility preview; authoring profiles, patterns, claims and contextual rules is
part of G13, and v1-shaped saves are rejected.
