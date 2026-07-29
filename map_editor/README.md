# Diorama visual editor

Local editor for the versioned visual metadata in `data/diorama/maps/`. It never
writes gameplay maps, layouts, tilesets, graphics, events, collision, or assets.

Run it from the repository root:

```bash
python3 map_editor/server.py
```

Then open `http://127.0.0.1:8765/`. Use `--port` to select another port and
`--no-browser` to avoid opening a browser automatically.

The editor uses only Python's standard library and browser APIs. Metatile atlases
are decoded in memory from the original tiles, metatile tables, and palettes.
Saving validates a candidate copy first, checks for external changes, atomically
replaces only the selected `data/diorama/maps/*.json`, and runs the validator and
deterministic rule compiler.

The current game schema supports per-cell overrides, reusable building templates,
and the reusable `eventRules.sign` rule. Proposed free-form structures and prop
presets are deliberately not written until the game compiler supports them.
