# Diorama Red parity traceability

Reference commit: `b21fd46ea789a0b8cb99d2c7e0add5a007568a54`.

| Reference function/rule | Emerald adaptation | Deliberate divergence | Fixture | Independent expected result |
|---|---|---|---|---|
| `main.lua` fallback | Existing OpenGL compositor plus software fallback | Emerald retains complete classic rendering for unsupported scenes | `R0-CLASSIC-SMOKE` | Movement, menus, battle and save/load remain software-rendered and functional |
| `TileShape.lua` final wall fallback | R2 compiles one complete-layout classifier to guarded C records | Ambiguous Emerald art remains flat rather than becoming a wall | `R0-ROUTE101` | Blocked unclassified cells have zero invented height |
| `Structures.lua` generic run detector | No R0 owner; scheduled for R5 | The discarded metatile-ID north-south detector is removed, not used as fallback | `R0-ROUTE115` | Generated IR has no automatic/measured run records |
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
