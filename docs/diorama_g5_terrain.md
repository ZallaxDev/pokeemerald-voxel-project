# G5 terrain and visual planes

The G5 runtime baseline is active for every cataloged map. Passable metatiles stay flat
and retain their own atlas art. Specialized behavior rules add verified terrain
geometry, while unauthored blocked art may form local upright volumes.
`tools/diorama_rules/g5_audit.py --check` gates 518 maps, 441 layouts, 75 tilesets and
the required reference families.

G5 keeps Emerald's four-bit elevation unchanged as gameplay metadata. It is not a
linear world height: zero is a wildcard transition and fifteen retains the incoming
plane. `DioramaTerrain_GetElevationSemantics()` exposes those meanings, while visual
height comes only from compiled terrain actions, profiles, and local blocked-art runs.
Wildcard and retain
cells acquire a non-metric effective plane identity from an unambiguous neighboring
concrete plane. Conflicting neighborhoods stay unresolved instead of guessing.

## Surface model

Terrain actions resolve a closed material class (`ground`, `path`, `sand`, `ash`,
`rock`, `pavement`, `wood`, `carpet`, `void`, or `water`) independently from the
source metatile. The live per-tileset atlas remains the material authority, so paths,
edges, corners, unions and transitions retain their own 16x16 artwork instead of
being synthesized or stretched.

Each resolved cell publishes an explicit stack of up to three visual surfaces. The
G4 span IR receives these G5 producers:

- ordinary terrain and cliffs produce occupied columns; the common six-neighbor
  shell emits sides only across real occupancy differences;
- tall cliff faces are divided into one-cell texture bands, preserving source art;
- cardinal and diagonal ledges keep the `base` ground flat and place only the
  painted `foreground` band on the directed boundary instead of compressing the
  complete metatile into a raised strip;
- cardinal stairs produce four treads, while muddy and bumpy slopes use sixteen
  one-pixel rises;
- descending stair archetypes remove the ordinary floor and emit thin steps below
  the surrounding datum, leaving a real stairwell opening;
- bridges emit a depressed lower base-layer surface and a separate one-pixel deck at
  the canonical Emerald bridge offset, with empty space and independent gameplay-plane
  identities between them;
- hole behaviors remove the ordinary floor, while descending stairs use a shared deep
  foundation so their treads and cavity walls do not become independent closed boxes.

Every surface carries independent six-face material provenance. Shell subtraction,
partial vertical intervals, chunk ownership and conservative merging remain shared
with G4. Uniform full-cell terrain stays on the compressed path. Directional ledges
use an equivalent compact path. Diagonal behaviors emit both corner boundaries, and
feature faces are suppressed when neighboring occupancy covers their vertical interval.

## Object support

`DioramaRules_ObjectGroundHeight()` selects a visual support by gameplay-plane identity
without changing the object's gameplay elevation. Stair and one-pixel ramp support use
the same behavior and profile as terrain. Bridge objects on the lower plane remain on
the underlay, while objects on the deck use the authored bridge offset. Ledge jump arcs
remain controlled by the original sprite movement offsets.

## Reference targets

The global audit verifies specialized placements in Fortree, Sootopolis, Granite Cave,
Mt. Chimney, Jagged Pass and Mt. Pyre. Reusable behavior rules cover Emerald jump
directions, bridge levels and logs, Abandoned Ship stairs, escalators, muddy and bumpy
slopes, water planes, ice and explicit holes. Alternate runtime layouts remain eligible
for Diorama when their layout exists in the generated 441-layout catalog.

Automatic volumes follow the reference renderer's local method rather than assigning
levels to walkable regions. After explicit pins, contextual rules, and patterns resolve,
passable cells remain flat. Remaining blocked flat cells form four-connected components
and north-south art runs. A run's front metatile is scanned north for the nearest repeat;
nonrepeating runs retain their drawn extent, capped at three 16-pixel cells (the
reference's six 8-pixel rows), and only repeat-derived
runs may adopt a taller component mode. Runs clipped by the 33x33 snapshot boundary are
left flat until both ends are visible. Components and runs never cross source-map seams.

Detected volume sides are emitted only above lower neighboring occupancy. Each vertical
band takes art from the owning run: south/east/west faces walk north from the visual
front, north faces walk south from the back, and top art comes from the first two run
rows. This reconstructs the local drawing without stretching one metatile over a tall
wall. The map editor applies the same algorithm over its complete immutable layout.

Rule resolution uses direct snapshot-grid neighbor lookup and one linear component
traversal. The compositor resolves each snapshot once and shares that immutable result
with atlas and terrain synchronization; the former cubic fixed-point pass is prohibited
by the full 33x33 regression fixture.

The conservative baseline covers all statically decodable cells and runtime mutations.
It deliberately does not infer semantic path/carpet labels or raise walkable terrain.
Reviewed pins and exact patterns override automatic blocked-volume inference.

## Verification

The C tests cover contextual plane propagation, bounded repeat-aware blocked volumes,
directional and diagonal ledges,
neighbor occlusion, all four stair directions, descending stairwells, object/ramp
support parity, cliff face bands, bridge surface stacks, chunk seams and capacity
failure. The Python reference and editor use model version 3. The generated
`build/diorama_catalog/g5_coverage.json` records global and reference-family coverage.
