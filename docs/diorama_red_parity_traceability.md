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
