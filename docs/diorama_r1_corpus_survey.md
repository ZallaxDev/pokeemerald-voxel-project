# R1 corpus and survey

R1 observes all Emerald source data without assigning geometry. The extractor covers 518
maps, 441 layouts, 75 logical tilesets, 324579 layout cells and 18318 logical metatiles.

## Corpus

`make -f Makefile_pc diorama-catalog` writes the generated corpus under
`build/diorama_catalog/`. `layout_cells.json` is row-major and preserves coordinates,
metatile, collision, elevation, tileset, behavior, layer type, composition references and
source provenance. Walkable and blocked values are candidates only; `authoritative` is
always false because runtime movement also depends on direction, elevation, objects and
map mutations.

Map records include type and connections. Event records include effective shared-event
provenance, warps and doors. Runtime mutation inventory scans map and global scripts and
labels unresolved dynamic systems rather than simulating scripts or saves.

`base`, `foreground` and `full` compositions share the canonical compositor. The golden
driver compares all 18318 logical metatiles pixel-by-pixel between Python and C, including
subtile, tile, palette, color, UV and flip provenance.

## Global survey

`make -f Makefile_pc diorama-survey` writes `build/diorama_survey/plan.json`. It contains
one deterministic spot and five capture names for every map, explicit offline entries for
all unreferenced layouts and logical tilesets, and exact hashes of static layout state and
camera configuration.

The survey never loads a save, teleports or writes gameplay state. The user reaches a
location normally. F5 freezes the last published frame, fixes the window to 960x640 and
cycles through `flat`, `v15`, `v35`, `v50` and `v75`; weather and interpolation are
disabled in Diorama views while the frozen snapshot preserves animation state. F6 writes
the current BMP and a JSON sidecar copied from that snapshot. Ctrl+P resumes gameplay and
restores the previous window, renderer, zoom and debug state.

Each survey entry selects the first unused run number, so a repeated survey never
overwrites its predecessor. A valid five-view set must share snapshot sequence and map,
palette, object-palette and animation generations. Captures are rejected during scripts,
transitions, fallback, unstable rendering or at a framebuffer size other than 960x640.

`survey.py record` validates captured BMP dimensions and runtime sidecars, then writes a
run manifest.
The manifest records commit, map, layout, coordinate, facing, pitch, optional save/config
hashes, exact semantic hashes, image hashes and same-driver/cross-driver tolerances.
Semantic equality is exact; image tolerance never changes semantic results.

Generated catalogs, plans, images and manifests remain under `build/`. The R1 gate rejects
tracked files in `build/diorama_catalog/` or `build/diorama_survey/`.
