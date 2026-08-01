# Diorama Red parity traceability

Reference commit: `b21fd46ea789a0b8cb99d2c7e0add5a007568a54`.

| Reference function/rule | Emerald adaptation | Deliberate divergence | Fixture | Independent expected result |
|---|---|---|---|---|
| `main.lua` fallback | Existing OpenGL compositor plus software fallback | Emerald retains complete classic rendering for unsupported scenes | `R0-CLASSIC-SMOKE` | Movement, menus, battle and save/load remain software-rendered and functional |
| `TileShape.lua` final wall fallback | No R0 classifier; R2 will implement the audited equivalent | Ambiguous Emerald art remains flat rather than becoming a wall | `R0-ROUTE101` | Blocked unclassified cells have zero invented height |
| `Structures.lua` generic run detector | No R0 owner; scheduled for R5 | The discarded metatile-ID north-south detector is removed, not used as fallback | `R0-ROUTE115` | Generated IR has no automatic/measured run records |
| `voxel_heights.lua` manual profile | No R0 profile; R2/R7/R9 rebuild reviewed reusable knowledge | All inherited pins and coordinate experiments are removed | `R0-ROUTE104` | No map has an inherited terrain anchor or pin |
| Relative ledge/stair constraints | No R0 topology resolver; R6 will implement the audited equivalent | Gameplay elevation remains metadata, never metric height | `R0-MT-CHIMNEY` | No static topology record is generated before R6 |
| Map-complete cache stability | Static records guarded by layout, source coordinate and expected metatile | Runtime never repeats viewport-local inference | `R0-LITTLEROOT` | Leaving and returning does not change geometry |

The machine-readable owner and precedence inventory is
`data/diorama/red_parity_resolvers.json`. A responsibility has one owner or no owner when
the conservative R0 result is deliberately unsupported and assigned to a later phase.
