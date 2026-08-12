# Diorama Red parity traceability

Reference commit: `b21fd46ea789a0b8cb99d2c7e0add5a007568a54`.

| Reference function/rule | Emerald adaptation | Deliberate divergence | Fixture | Independent expected result |
|---|---|---|---|---|
| `main.lua` fallback | Existing OpenGL compositor plus software fallback | Emerald retains complete classic rendering for unsupported scenes | `R0-CLASSIC-SMOKE` | Movement, menus, battle and save/load remain software-rendered and functional |
| `TileShape.lua` final wall fallback | R2 compiles one complete-layout classifier to guarded C records | Ambiguous Emerald art remains flat rather than becoming a wall | `R0-ROUTE101` | Blocked unclassified cells have zero invented height |
| `Structures.lua` generic run detector | R5 measures complete residual components in `measured_volumes.py`, then compiles guarded height/run/band records | Emerald compares tile/layer/palette/flips/subtile and tileset-pair provenance; metatile ID and pixel equality are not authority | `R5-ROUTE115-CLIFF-RUNS`, `tools/diorama_rules/test_measured_volumes.py` | Extent and period remain distinct, height is capped at six 8-pixel bands, and contradictory candidates remain flat |
| `voxel_heights.lua` manual profile | No R0 profile; R2/R7/R9 rebuild reviewed reusable knowledge | All inherited pins and coordinate experiments are removed | `R0-ROUTE104` | No map has an inherited terrain anchor or pin |
| Relative ledge/stair constraints | No R0 topology resolver; R6 will implement the audited equivalent | Gameplay elevation remains metadata, never metric height | `R0-MT-CHIMNEY` | No static topology record is generated before R6 |
| Map-complete cache stability | Static records guarded by layout, source coordinate and expected metatile | Runtime never repeats viewport-local inference | `R0-LITTLEROOT` | Leaving and returning does not change geometry |
| `TileShape.lua` precedence and conservative fallback | `TileShapeClassifier` resolves context > general pin > reliable behavior > flat fallback offline and C only consumes the final record | Collision is evidence only and ambiguous Emerald art stays flat | `R2-ROUTE101-CLASSIFIER`, `R2-ROUTE115-CLASSIFIER`, `R2-GRANITE-CAVE-B1F-CLASSIFIER` | The overlay reports stable class/source/evidence at five fixed views without changing gameplay state |
| `voxel_heights.lua` reviewed manual knowledge | `gTileset_General:4` is the authoritative flower pin compiled for all 615 placements | Coordinate targets are forbidden and every catalog occurrence is part of the review | `R2-ROUTE115-AUTHORITATIVE-PIN-BLAST-RADIUS` | Anchor and complete blast radius agree on class/height/artMode/pool/authored/source/evidence after a normal round trip |
| `Buildings.matches` exact matrix scan | R3 matches rectangular Emerald metatile matrices over complete layouts and stores source-exact candidates | Ragged sentinel rows are rejected; context must be explicit | `R3-LITTLEROOT-CLAIMS`, `R3-POKEMON-CENTER-CLAIMS` | All matrix cells must match and an accepted template owns its complete claim mask |
| `Buildings.build` ordered `S.skip` claims | R3 uses one owner IR with template > authored special > prop candidate > generic volume and whole-placement first-claim-wins | Emerald records owner and evidence instead of distributed Lua skip tables | `tools/diorama_rules/test_structures.py` | Reversing input iteration does not change winners and no partial overlapping placement survives |
| `Structures.forMap` structural flood | R3 flood-fills four-connected unclaimed cells over each complete layout using compatible class and semantic pool | Pixel component counts remain evidence and do not replace semantic map connectivity | `R3-ROUTE115-REGIONS`, `R3-FORTREE-CLAIMS` | Diagonal cells, different pools, layout boundaries and absent connections never merge |
| `voidTiles` black/transparent source test | R3 composes both Emerald layers with provenance and derives void only for resolved, unauthored, non-animated art | Atlas black/transparent detection is separate from the absent exterior ring | `tools/diorama_rules/test_structures.py` | Transparent and fully black fixtures become void; authored and animated fixtures retain their class |
| Door fold before generic volume | R3 records only conservative folds beneath compatible unauthored upright art after template claims | Warps alone never imply a visual door and gameplay data is never modified | `R3-LITTLEROOT-CLAIMS`, `R3-FORTREE-CLAIMS` | Walkable doors remain visible; a claimed template is not overwritten and no unsupported fold is emitted |
| Structures diagnostic ownership | R3 compiles candidates plus compact per-layout owners to C and draws immutable cyan boundaries from resolved snapshots | R3 intentionally emits no building/prop height or shell geometry | All R3 visual scenarios | `OWN/CLM/RGN/SRC` and boundaries remain stable across pitch, camera movement and normal round trips |
| `Structures.extractObjects` background flood | Opaque Emerald art classifies boundary-connected shade classes while black remains foreground; compatible neighboring ground is still compiled for replacement | RGB tolerance seven applies to automatic ground matching; shade thresholds are frozen independently from exact palette indices | `R4-LITTLEROOT-SIGN`, `tools/diorama_rules/test_pixel_objects.py` | Background is absent, the black frame and enclosed foreground remain, and `G:1` is deterministic |
| Forced billboard outline mode | `billboard`, `console`, `signpost` and `post` use reference-style shade classes and outline-connected segmentation without authored masks | White/gray enclosed pixels survive; event-backed Littleroot/Oldale signs cover reused art outside the isolated 3x3 pattern | `R4-BRENDAN-HOUSE-TV-SUPPORT` | The TV screen and every sign keep black outline plus enclosed light texels |
| Layered cutout components | Props with visible Emerald foreground layers use that exact layer as their mask; R4 then uses eight-connected components and a separate drawn foot for each | Base-layer walls/floors remain in place instead of being inferred as foreground or replaced by one neighboring metatile | `R4-OLDALE-HOUSE1-PLANTS`, `R4-MAUVILLE-HOUSE1-STOOLS` | Adjacent props do not merge and each component starts at its own foot |
| Authored object support | A compiled console references the separately claimed furniture owner north of it and is emitted only while both owners are active | Support height is the reviewed table height, not gameplay elevation | `R4-BRENDAN-HOUSE-TV-SUPPORT` | TV starts at 0.75 cells, furniture remains and replacement ground never covers the support cell |
| Automatic/manual `propGround` | Manual profile ground wins; otherwise a unique cardinal vote is compiled into the object IR | Ties reject the cutout rather than selecting by iteration order | `R4-LITTLEROOT-SIGN`, `tools/diorama_rules/test_pixel_objects.py` | Runtime never re-votes and renders the compiled ground under accepted props |
| Horizontal relief | The exact foreground-layer mask maps source Y to world Z and emits a visible shallow twelve-Q32 top relief | Solid edge-to-edge furniture is reclassified instead of receiving a second authored mask system | `R4-DEWFORD-HALL-RELIEF` | The table is shallow and horizontal, never a vertical box |
| Pixel prism source texel | Every accepted pixel stores cell, metatile, tile entry, tile, layer, palette, index, flips and UV; runtime samples the immutable snapshot palette/tile copy | Colors remain live under fades while topology stays compiled | `tests/diorama/test_terrain_mesh.c`, Python/C parity driver | Every prism has valid provenance and changing its texel changes the deterministic geometry hash |
| Dynamic object preservation | Cut trees, boulders, player and NPCs remain in the existing snapshot billboard renderer | Dynamic object events are never compiled as static terrain cutouts | `R4-ROUTE104-CUT-TREE-BILLBOARD`, `R4-FIERY-PATH-BOULDER-BILLBOARD` | Cut removes the tree and Strength moves the boulder without residual terrain geometry |

The machine-readable owner and precedence inventory is
`data/diorama/red_parity_resolvers.json`. A responsibility has one owner or no owner when
the conservative R0 result is deliberately unsupported and assigned to a later phase.

R1 adds the Emerald extractor equivalent before any classifier. `layout_cells.json`
retains every layout cell and links it to canonical metatile composition metadata;
`plan.json` deterministically covers every map plus layouts and tilesets without a map
placement. These generated files remain under `build/` and are not rule authority.

R2 is `approved`. Python and i386 C agree over all 324,579 cells in the 441
layouts; 95 ambiguity groups retain 250,585 conservative fallback placements. The five R2 fixtures
fix map, layout, coordinates, facing, five pitches, 960x640 setup, expected result and
normal-gameplay round trip. The optional local AI spike is `not-run`, non-authoritative
and absent from build/runtime. The user approved R2 after the Route 101 survey exposed
and verified the correction for all four reliable diagonal ledge behaviors.

R3 compiles 2,923 candidates over the same 324,579 cells: 8 exact template claims,
634 authored prop/vegetation candidates and 2,281 residual regions. Python and i386 C
publish the same owner/region records. The 3,863 derived void cells retain pixel evidence;
zero door folds are emitted because no current source satisfies the conservative visual
predicate outside a prior template claim.

R4 compiles 31 accepted pixel objects from 2,959 candidates: 30 upright cutouts and one
horizontal relief. Their 6,364 visible source pixels retain complete provenance in 592
mask rows and seven frozen Emerald golden masks; two consoles reference distinct authored
supports, which now also have cutout geometry. Seven opaque objects use compiled replacement
ground and 24 layered objects retain each source metatile's base layer. Python and i386 C
agree over all 325,276 terrain records and every object, mask and pixel record. That
technical evidence is now invalid as a candidate: the second visual review still found
flat TV screen/support, plants, stools and relief. R4 is back in `implementation`, and
corpus-wide closure will not run again until a reduced visual candidate is accepted.

R5 introduces one detector over complete layouts after authored/template/prop claims. It
scans north-south and east-west runs in deterministic source order, anchors repetition at
the south/east front, supports the trim-plus-repeat case, applies regional consensus and
stores confidence/conflicts. Runtime consumes immutable measured records, invalidates the
whole owner after a dirty source cell and renders each half-metatile face band from its own
compiled source. Roof evidence remains a candidate for R7 rather than creating a generic
gable.

The failed forest candidates exposed a missing prerequisite from Red: generic folding is
restricted to residual `upright` art. Collision is supporting evidence, never a positive
classification. Emerald `fallback:flat` cells therefore cannot enter R5. General tree art
is claimed before the generic fold, while unresolved buildings stay flat until R7 rather
than becoming mixed generic owners.

Emerald's authoritative `MB_MOUNTAIN_TOP` behavior now supplies a separate positive R5
terrain class. Every accepted mountain cell contributes exactly one cell of visual height;
gameplay elevation remains plane identity only. Runtime derives a fade-invariant occupancy
mask from the unfaded foreground layer when partial, or removes the corner-connected
background from an opaque full composition. Raised pixels use the mountain shell while all
removed pixels retain base underlay, so no variant can become a square slab or expose black.
The offline compiler applies the equivalent extraction to the complete corpus and rejects
unresolved, empty or rectangular mountain masks.

General's Route 104 rock-terrace family is not a horizontal topographic silhouette: its art
depicts a directed boundary between visual planes. The complete-layout solver recognizes only
connected 121/136/137/144 courses, closes Route 104's 207/175 transitions, and compiles integer
potentials without treating gameplay elevation as metric. At source `(25,66)`, north ground is
one cell above south ground even though both carry gameplay elevation 3. Components anchor their
highest plane at zero, preserving map-connection ground while lower beaches become negative.
Terrace cells with partial foreground alpha use that exact unfaded 16x16 mask as their high-plane
foot. A six-voxel run rises from the lower plane to the upper plane at that curved boundary,
forming a voxel wedge rather than a binary cut. The wedge uses full-composite opaque materials
on its exposed surfaces; foreground alpha never leaves the clear color visible below the course.
Course turns share boundary functions rather than meshing independent local ramps: horizontal
uses N, vertical uses E, inner corners use `min(E,N)`, and outer corners use `max(E,N)`. Curvature
is restricted to interior samples while canonical endpoint sections make every shared edge
bit-identical. Low and high endpoints land exactly on -1 and 0, respectively.
The attempted luminance relief skin is not part of the current renderer. Replacing the wedge with
a binary wall and brown pixel prisms visually regressed to square blocks and black artifacts, so
the experiment was removed. The restored candidate is the curved gradual wedge with opaque full
materials and shared N/E/min/max seam functions. Any future color relief must preserve that base
volume rather than substitute it.

Terrace occupancy is relative to the resolved course ground, not absolute world zero. Every raised
mountain-art sample spans from `ground - 1 voxel` through `ground + mountainHeight`; this keeps the
gradual `-1..0` Route 104 wedge solid instead of emitting disconnected one-voxel slabs. The focused
terrain fixture uses a negative-ground vertical terrace and rejects any downward horizontal face
above the single lower base plane.

The follow-up Route 104 capture confirms this invariant removes both previously visible failure
modes: there are no black concave cavities and no disconnected side bands. This validates solid
course occupancy while leaving final terrace shape approval as a separate visual decision.

The current smoothing candidate changes only unconstrained interior height samples. Shared N/E
ramps use integer smoothstep while preserving exact 0/16 endpoints. Inner corners use
`ceil(E*N/16)` and outer corners use its exact dual
`16-ceil((16-E)*(16-N)/16)`, so all high, low and neighboring profile boundaries remain identical.
At E=N=8 the interiors become 4/12 instead of the orthogonal min/max value 8. Solid occupancy,
native 16x16 resolution, full-composite materials and gameplay state are unchanged.
This candidate failed visual review and is not an accepted technique. R5 is paused by explicit user
decision. Further terrace work must replace runtime height-formula iteration with an offline,
deterministic model compiler: immutable Emerald art and topology constrain a known wedge template;
the compiler emits a solid inspectable voxel model, validates interfaces and provenance, and greedy-
meshes it for immutable runtime instancing. `tree_model/emerald_tree.vox` and
`tools/diorama_tree/compile_vox.py` define the existing output-side precedent.
Contradictory components fall back together, isolated ID reuse is ignored, and tree VOX rendering
remains independent.

General tree ownership is now ordered by complete drawing: 2x3 trees, repeated 2x2 bodies,
then incomplete 2x1 edge rows. Dense forest metatiles 198/199 are identical one-cell units
rather than left/right pairs. The renderer assembles each complete 16x16, 32x16 or 32x32
drawing. General's opaque art cannot use Red's luminance flood directly: stable palette-2
tokens `{1,2,3,4,6,8}` identify crown/trunk while `{12,13,14,15}` identify cyan ground and
painted shadow. The largest upper connected component becomes quantized circular depth
chords. Front/back faces preserve full-composite texels, sides search inward past the dark
outline, dome interiors sample deeper artwork rows, and the dominant visible flat ground
metatile replaces the source tree below the hull. Compatible faces are emitted as runs rather
than one quad per voxel. Dense forest 198/199 remains full-cell foliage, not a round crown.

The user-authored `tree_model/emerald_tree.vox` supersedes that inferred hull for General's
large 2x2 body. Its 6782 voxels compile offline into 2424 exterior, color-aware greedy quads;
runtime never parses an asset or emits hidden cube faces. A 2x3 painted pattern anchors the
same model at its southern 2x2 blocking body. Every claimed source cell becomes flat visible
ground, with a darker ground shade only below the blocking body. The 2x1 small-tree fallback
and dense forest cells remain separate classes.

The authored model is uploaded once as a static local-space VBO. Terrain chunks retain only
bounded value-only translations, own each tree by its visible anchor chunk, and conservatively
include the complete transformed model in their culling bounds. All visible trees use one
OpenGL 3.3 instanced draw. A render-only border provenance flag distinguishes repeated
`border.bin` presentation from source-valid map cells; only the exact General matrix
`468/469/476/477` becomes a 2x2 tree owner. This neither fabricates map coordinates nor changes
the field map, connections, collision, or any gameplay state.
