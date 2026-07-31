# Diorama visual editor

Local browser and editor for the versioned visual metadata in `data/diorama/maps/`.
It lists all 518 maps and previews the shared G5 occupancy model. Maps without an
explicit rule file use the generated global baseline in read-only mode. It never
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
Saving validates a candidate copy first, checks for external changes, atomically
replaces only the selected `data/diorama/maps/*.json`, and runs the validator and
deterministic rule compiler.

G3 uses strict schema v2 sources. The backend reads the generated tileset catalog and
normalizes the same actions used by the compiler. The existing point-editing UI is a
compatibility preview; authoring profiles, patterns, claims and contextual rules is
part of G13, and v1-shaped saves are rejected.
