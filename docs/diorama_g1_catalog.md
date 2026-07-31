# Diorama global catalog (G1)

`tools/diorama_rules/catalog.py` builds the inspection catalog from the game's
authoritative map, layout, tileset, event, behavior, animation and decoration
sources. It does not maintain a manual list of maps or tilesets.

Run:

```sh
make -f Makefile_pc diorama-catalog
```

Open `build/diorama_catalog/index.html` through a local HTTP server. The page
links each logical tileset pair to its contact sheets and exposes local/global
metatile IDs, behavior, layer type, real use count, subtile tile ownership,
palette, flips, animation slots and 16-row alpha masks.

The generated directory also contains:

- `maps.json`, `layouts.json` and `tilesets.json`: complete global inventories.
- `metatile_usage.json`: coordinates, frequency, neighbors, collision and
  elevation contexts for every used layout cell.
- `metatiles.json`: inspectable metatile and eight-subtile provenance records.
- `events.json`: objects, signs, doors, warps, field obstacles and decoration
  definitions. Save-backed decoration placement remains explicitly runtime-only.
- `behaviors.json`: all used behaviors and their water, grass, ledge, bridge,
  stairs, warp and special-surface families.
- `animations.json`: tile and palette animation slots extracted from
  `src/tileset_anims.c` and associated with logical tilesets.
- `structures.json`: bounded repeated 2x2 through 3x3 exact-matrix candidates,
  deduplicated by logical tileset pair and matrix.
- `ambiguities.json`: metatiles reused under distinct collision, elevation or
  map-type contexts.
- `runtime_layouts.json`: default, script-selected, dynamic and unreferenced
  layouts, plus `setmetatile` mutations and generated-map systems.
- `coverage.json`: complete resource totals, rule match counts and dead rules.
- `contact_sheets/diagnostics.json`: static tile references absent from source
  PNGs, including whether each metatile is actually used or animation-backed.

The catalog is diagnostic data, not gameplay authority. Dynamic facilities,
decorations, object positions and script mutations must still come from the
immutable live scene snapshot when rendering a running game.
