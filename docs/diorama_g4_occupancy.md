# G4 occupancy and shell contract

The common geometry model uses signed integer coordinates. One art pixel is one
voxel and one Emerald map cell is exactly 16 voxels on each horizontal axis.
Floating-point coordinates are introduced only while producing render vertices.

## Occupancy

Each occupied `(x,z)` column contains sorted, non-overlapping half-open vertical
spans `[yMin,yMax)`. A span carries material provenance for all six directions,
its source cell and render flags. Empty and unresolved overlapping spans are
invalid. Adjacent spans merge only when their complete provenance is identical.

The runtime implementation is `src/diorama/terrain/occupancy.c`. The deterministic
reference and headless JSON frontend are in
`tools/diorama_rules/occupancy_model.py`. The editor receives this same versioned
IR from `/api/map`; JavaScript only triangulates the supplied shell rectangles.

## Shell and ownership

Top and bottom faces query occupancy immediately above or below a span. Side
faces subtract the complete set of neighboring intervals, preserving exposed
pieces when two components overlap only partially. Component and structure IDs
never cause face removal.

Chunks own half-open horizontal voxel rectangles. Halo spans participate in
neighbor queries but only spans whose columns lie in the owned rectangle emit
faces. This gives every face one stable owner, including chunk boundaries.

Faces merge only when they are coplanar and their orientation, source cell,
material, flags and UV run are identical and contiguous. Keeping source cells in
the compatibility key conservatively prevents merges across metatile, palette,
flip, animation and atlas seams. UVs are cropped to source texels and inset by
0.02 texel to prevent atlas bleeding.

## Capacity and diagnostics

The terrain backend reserves for six unmerged faces per owned pixel column; it
does not rely on greedy merging to fit. `DioramaTerrainMesh` reports occupancy
spans, raw shell faces, emitted faces and directional face counts. Bounds are
accumulated from every emitted vertex, including bottoms and below-ground faces.

Full-cell profiles with uniform height use an equivalent compressed run path:
the backend emits the same pixel occupancy intervals as cell-sized rectangles
without sorting tens of thousands of unit faces. Masked profiles and profiled
roofs retain the general pixel shell path. `usedCompressedOccupancy` exposes
which path built a mesh and is covered by the terrain regression tests.

Run the headless reference with:

```sh
python3 tools/diorama_rules/occupancy_model.py --input spans.json --owner 0,0,128,128
```
