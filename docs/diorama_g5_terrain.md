# G5 terrain and visual planes

> The current complete-layout ID-run inference is a conservative baseline, not parity
> with the assembled-art detector used by the Pokemon Red reference. Its replacement is
> specified in `docs/diorama_red_parity.md` and gated phase by phase in
> `TODOLIST_DIORAMA_RED_PARITY.md`. G6 and editor work remain blocked until that TODO is
> manually approved.

The G5 runtime baseline is active for every cataloged map. Passable metatiles stay flat
and retain their own atlas art. Specialized behavior rules add verified terrain
geometry, while ambiguous blocked art remains flat until a dedicated profile classifies it.
`tools/diorama_rules/g5_audit.py --check` gates 518 maps, 441 layouts, 75 tilesets and
the required reference families.

G5 keeps Emerald's four-bit elevation unchanged as gameplay metadata. It is not a
linear world height: zero is a wildcard transition and fifteen retains the incoming
plane. `DioramaTerrain_GetElevationSemantics()` exposes those meanings, while visual
height comes only from compiled terrain actions, profiles, and complete-layout terrain records.
Wildcard and retain
cells acquire a non-metric effective plane identity from an unambiguous neighboring
concrete plane. Conflicting neighborhoods stay unresolved instead of guessing.

## Surface model

Terrain actions resolve a closed material class (`ground`, `path`, `sand`, `ash`,
`rock`, `pavement`, `wood`, `carpet`, `void`, or `water`) independently from the
source metatile. The live per-tileset atlas remains the material authority, so paths,
edges, corners, unions and transitions retain their own 16x16 artwork instead of
being synthesized or stretched.

Classification is exhaustive and deterministic. Every one of the 75 logical tilesets
declares a baseline class in `tilesetTerrainDefaults`. Reusable metatile pins override
that baseline, and semantically reliable behavior rules override both. Geometry-only
ledge and stair behaviors preserve the tileset's underlying material class. The G5
audit accounts for all 323,071 statically decodable cells plus 1,508 dynamic-tileset
cells and rejects missing tilesets, unknown classes, unclassified cells, and coordinate
overrides. A baseline class is classification metadata, not permission to synthesize or
replace the metatile's original art.

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

Automatic volumes follow the reference renderer's run method, but resolution is offline
over each complete immutable layout. `compile_rules.py` reads every `map.bin`, including
collision and elevation bits, resolves all 441 layouts through the same
`terrain_volumes.py` implementation used by the editor, and emits sparse static records.
After explicit pins,
contextual rules, and patterns resolve, passable cells remain flat unless reliable
ledge or stair transitions constrain two connected walkable regions. Those constraints
assign relative visual levels (`high = low + 1`) without treating raw elevation as a
metric height. Conflicting components, components spanning more than three levels, and
regions without transition evidence stay at datum zero. Raw concrete planes only prevent
incompatible regions from merging. Blocked flat cells form four-connected components and
north-south art runs, but only reliable rock-terrain behaviors (`MB_CAVE` or
`MB_MOUNTAIN_TOP`) may derive a repeat-aware height. Other blocked art remains flat so
trees, buildings, furniture, and props cannot become terrain cubes before their dedicated
phases. A measured run's front metatile is scanned north
for the nearest repeat;
nonrepeating runs retain their drawn extent, capped at three 16-pixel cells (the
reference's six 8-pixel rows), and only repeat-derived
runs may adopt a taller component mode. Layout boundaries, rather than the camera
viewport, bound components and runs.

Inference is opt-in at both map and tileset scope through `terrainMode`. The global map
default is currently `manual`, so the incomplete residual-art detector cannot create
geometry anywhere in the game. If either
scope is `manual`, the affected cells are excluded from blocked-volume detection.
Explicit cliffs, ledges, stairs and bridge anchors still participate in the relative
plane graph; this is how one reusable one-cell cliff course can separate cumulative
terrace levels rather than assigning the same absolute level everywhere.
Pins, behavior actions and default flat surfaces still resolve normally, and edge masks
may be derived from explicitly authored cliffs. General, Fallarbor, Lavaridge and Cave
currently use this authoritative manual mode; incomplete families therefore remain flat
instead of producing camera-stable but visually false towers.

Detected volume sides are emitted only above lower neighboring occupancy. Each vertical
band takes art from the owning run: south/east/west faces walk north from the visual
front, north faces walk south from the back, and top art comes from the first two run
rows. This reconstructs the local drawing without stretching one metatile over a tall
wall. The generated record stores source-cell offset, expected metatile, shape, terrain
class and axis, exact sixteenth-cell ground/feature heights, measured run metadata, and
cliff masks. Runtime applies it only when `sourceLayoutId/sourceMapX/sourceMapY` and the
expected static art match, preserving connected-map seams and dynamic metatile changes.

Every cliff also publishes cardinal edge, base, height-transition, and corner masks.
The common shell uses the actual occupied intervals, so adjacent equal-height cliffs
remove their shared face, lower neighbors expose only the height transition, and two
adjacent exposed edges form a closed convex corner. The lowest source band is the cliff
base/toe, higher bands retain their own metatile provenance, and the top remains an
independent atlas-backed cap. Reusable pins cover the named General, Fallarbor, Lavaridge,
and Cave rock-wall families. Fallarbor and Cave also pin visually continuous ground that
uses blocked collision variants, preventing gameplay collision from fragmenting a visual
plateau. Bounded blocked-art runs remain a fallback only for tilesets and maps that
explicitly retain automatic terrain mode.

Runtime performs a binary lookup in the owning layout's record range after ordinary
rule, context, and exact-pattern resolution. Explicit rules remain authoritative and
automatic cliff shape replacement is limited to eligible behavior-flat rock cells.
There is no viewport-local volume, plateau-height, or cliff-topology pass. Gameplay
elevation propagation remains snapshot-local only as non-metric support identity.

The conservative baseline covers all statically decodable cells and runtime mutations.
It does not infer arbitrary semantic labels or heights: walkable plateaus rise only from
the constrained transition graph. The shared resolved grid drives terrain and object
support so sprites remain on raised surfaces. Reviewed pins and exact patterns override
automatic blocked-volume inference.

## Verification

The C tests cover contextual plane propagation, generated range guards, expected-art
guards, viewport-invariant source-coordinate lookup, directional and diagonal ledges,
neighbor occlusion, all four stair directions, descending stairwells, object/ramp
support parity, cliff face bands, bridge surface stacks, chunk seams and capacity
failure. The Python reference and editor use model version 3. The generated
`build/diorama_catalog/g5_coverage.json` records global and reference-family coverage.

`data/diorama/g5_profiles.json` defines reproducible checks for Fortree, Sootopolis,
Granite Cave B1F, Mt. Chimney, Jagged Pass, and Mt. Pyre 2F. Automated verification uses
`g5_audit.py --check`; final closure additionally requires
`g5_audit.py --require-manual` and therefore remains blocked while the manifest says
`pending-user-confirmation`. The G5 checklist is intentionally not closed until the user
has inspected those maps in the running Diorama renderer and explicitly approved them.
