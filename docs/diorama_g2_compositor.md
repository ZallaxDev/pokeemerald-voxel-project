# Emerald compositor parity (G2)

`tools/diorama_rules/emerald_compositor.py` is the shared offline compositor for
the rule compiler, catalog, and map editor. It decodes indexed PNG tiles,
metatile entries, palettes, flips, layers, and BGR555 colors while retaining the
source metatile, layer, subtile, tile, palette, color index, UV, and flip flags
for every pixel.

Transparent color index zero is represented as transparent black, matching
`DioramaMetatile_ComposeLayer()` exactly while retaining its provenance.

## Static and animated tiles

The compositor distinguishes four tile states:

- `static`: present in the generated `tiles.4bpp` payload.
- `static-padding`: represented by transparent PNG padding but absent from the
  packed runtime payload.
- `animated-slot`: absent statically and covered by a callback-owned animation
  destination extracted from `src/tileset_anims.c`.
- `unresolved`: neither static nor backed by a declared animation.

Animation ownership follows the C callback call graph, not symbol-name prefixes.
This keeps Cave and Lavaridge lava separate and prevents Mauville flower data
from being assigned to Mauville Gym. Frame paths preserve pointer-array order and
multi-file frames such as Sootopolis stormy water.

Static tools normally retain the source PNG frame and record missing references.
Callers that need absent animation destinations can pass `animation_frame=0` (or
another declared frame index); only destinations absent from the static payload
are filled. `missing_policy="error"` rejects padding and unresolved references.

The generated `animations.json` records 75 callback-owned tile destinations.
Twenty destination tiles are absent from packed static graphics: eight
Sootopolis tiles represented only by PNG padding and twelve Sootopolis Gym tiles
outside its PNG. All twenty have declared extracted frames. Current metatile
tables do not reference those twenty destinations.

## Golden verification

Run:

```sh
make -f Makefile_pc test-diorama-metatile-golden NATIVE_LINUX=1 \
  PKG_CONFIG_32_PATH=/usr/lib32/pkgconfig:/usr/lib/pkgconfig:/usr/share/pkgconfig
```

The test compiles a 32-bit C oracle that calls
`DioramaMetatile_ComposeLayer()` and `DioramaMetatile_DecodePixel()`. Python is
then compared against C for both layers and every provenance field across all
18,318 logical metatiles and all 75 logical tilesets. Coverage includes 67
compressed and eight uncompressed tilesets, shared metatile tables with distinct
graphics, every real 4-bit indexed PNG, and a synthetic 8-bit indexed PNG.

The C side consumes independently generated `tiles.4bpp` data padded to the two
512-tile VRAM regions. The test also verifies every PNG-derived 4bpp prefix and
requires omitted PNG tails to be transparent, so the two sides do not share the
same tile decoder as their oracle.
