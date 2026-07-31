# G3 diorama rule schema

G3 replaces schema v1 with strict `schemaVersion: 2` documents. The compiler never
interprets v1 implicitly. Use `tools/diorama_rules/migrate_v1_to_v2.py INPUT OUTPUT`;
the migrator rejects retired G0 overrides, templates, placements and metatile shapes
instead of reviving them.

## Source documents

`data/diorama/defaults.json` is the single `kind: global` document. It contains:

- `semanticPools`: named non-merging semantic domains.
- `profiles`: an archetype, one or more pixel masks, and source-art samples.
- `default`: the fallback action.
- `behaviorRules`: global rules keyed by an Emerald `MB_*` symbol.
- `contextualRules`: selectors plus actions and signed 16-bit priority.
- `exactPatterns`: exact multicell matrices, dimensions, priority and claim mask.
- `eventPresets`: reusable object, warp, coordinate or background-event actions.
- `mapDefault`: unsupported reason, camera and ground policy for uncurated maps.

`maps/*.json` are `kind: map` documents. They identify the authoritative map and
layout, explicitly state supported status (with a reason when unsupported), select a
camera and automatic/manual ground policy, and may add contextual rules or exact
patterns. `tilesets/*.json` are optional `kind: tileset` documents containing local
metatile pins. Empty v1 building and tileset files have been removed.

Every object has a closed key set. Duplicate JSON keys, unknown keys, invalid types,
non-finite/out-of-range numbers, duplicate logical IDs across global and map-local
sections, unknown symbols, mismatched map layouts, bad profile references and
out-of-range local pins fail validation. Pool, profile, contextual-rule, exact-pattern,
and preset IDs share one compiler-wide logical namespace so diagnostics and generated
numeric references cannot be ambiguous.

## Actions and archetypes

An action requires `archetype` and `pool`. Optional fields are `profile`, `axis`,
`groundOffset`, `height`, and `groundPolicy`. The closed archetype set is:

```text
ground void water shallow-water waterfall current hot-spring
ledge cliff mound wall-volume bridge deck rail support
stairs-n stairs-s stairs-e stairs-w stairs-down-n stairs-down-s stairs-down-e stairs-down-w
roof top-slab awning building claim-only billboard cutout console signpost post
counter table desk bed bookcase relief round-hull grouped-hull stump
tree forest-wall shrub hedge rock boulder grass flower animated-cutout
```

Semantic pools prevent unrelated touching objects from becoming one component. Pools
are references, not free-form labels. Profiles likewise use validated IDs. Profile
masks are 1..64 pixels in each dimension and encode each row as lower-case hexadecimal;
bits outside the declared width are invalid. Samples reference a real tileset-local
metatile and one of `base`, `foreground`, or `full`.

## Selectors and patterns

Contextual selectors can combine:

- tileset symbols and global metatile IDs;
- behavior symbols, layer types (`normal`, `covered`, `split`) and elevations 0..15;
- map types;
- any of eight neighbors, each constrained by metatile, behavior, layer or elevation;
- an event kind and optional normalized event class.

Exact patterns require a primary/secondary tileset pair, `width` and `height` from
1..32, a matrix of global metatile IDs, a signed 16-bit priority, and a binary claim
mask of identical dimensions with at least one claimed cell. Matching scans all 441
authoritative layouts. A pattern with zero placements fails unless it has
`allowedNoPlacements: {"reason": "..."}`.

Every matrix cell is checked against the declared pair: IDs below `0x200` must be less
than the primary metatile count, and IDs from `0x200` use a secondary-local index after
subtracting `0x200`. The compiler also expands actual pattern matches and claim masks
on each concrete map. Two distinct matching placements may overlap claims when their
priorities differ; equal-priority overlaps are ambiguous and fail validation. Global
and map-local patterns participate in the same check on that map.

Behavior rules, pins, contextual rules and event presets are checked against actual
layout/map/event placement data. Zero-match entries fail unless they carry the
structured `allowedUnused: {"reason": "..."}` exception. These checks are compiler
logic, not documentation-only promises.

Ground policy is exactly one of:

```json
{ "mode": "automatic" }
{ "mode": "manual", "metatile": 17 }
```

Automatic policy leaves compatible-ground selection to the later occupancy runtime.
Manual policy fixes a global 0..1023 metatile source.

## Normalized output

`compile_data(root)` returns a deterministic, value-only dictionary with these
authoritative keys:

```text
schemaVersion, tilesets, layouts, pools, profiles, default,
behaviorRules, tilesetPins, contextualRules, exactPatterns,
eventPresets, maps, sha256, generation
```

It always contains 75 tileset rows with stable IDs 1..75, 441 layout rows with the
authoritative numeric IDs 1..441, and all 518 maps. Dynamic tileset references use ID
zero. Each map row contains group/number/layout, map type, camera, ground policy,
supported status and unsupported reason. The seven migrated curated maps remain the
only supported maps.

The SHA-256 is over canonical ASCII JSON (`sort_keys=True`, compact separators) of
the complete catalogs, pools, profiles/masks/samples, default, every rule/pin/pattern/
preset, and all maps. Placement counts and structured exceptions are included. The
32-bit generation is the first eight hash hex digits, with zero remapped to one.

Transitional aliases (`layout_rules`, `map_rules`, `behavior_rules`, `tileset_rules`,
and empty retired arrays) remain for Python callers. They are not the v2 authority.

## Runtime C contract

`render_header()` declares the packed immutable integration ABI. Existing base names
remain stable: `gDioramaTilesetsV2`, `gDioramaLayoutsV2`, `gDioramaMapsV2`,
`gDioramaDefaultActionV2`, and `gDioramaBehaviorRulesV2`.

The additional generated struct/table families are:

- `DioramaGeneratedPoolV2` and `gDioramaPoolsV2` for semantic pool IDs and descriptions.
- `DioramaGeneratedProfileV2`, `DioramaGeneratedMaskV2`, `gDioramaMaskRowsV2`, and
  `DioramaGeneratedSampleV2` for profile mask/sample offset ranges.
- `DioramaGeneratedTilesetPinV2` for tileset-local pins.
- `DioramaGeneratedSelectorV2`, typed selector value arrays, and
  `DioramaGeneratedNeighborV2` for all tileset, metatile, behavior, layer, elevation,
  map-type, event, and eight-direction neighbor predicates.
- `DioramaGeneratedContextualRuleV2` for selector/action/priority rows.
- `DioramaGeneratedExactPatternV2`, `gDioramaPatternCellsV2`, and
  `gDioramaPatternClaimsV2` for exact matrices and claim masks.
- `DioramaGeneratedEventPresetV2` for event kind/class actions.

Each variable-length relation uses an offset/count pair. Empty source sections still
emit an immutable table declaration and a zero count; repository data therefore does
not need placeholder sample patterns. Numeric IDs are deterministic: catalog IDs keep
their authoritative ordering, pool/profile IDs use sorted logical IDs, rule/pattern IDs
use sorted logical IDs, and map scope IDs use catalog map order. Scope zero means global;
nonzero scope IDs identify a map. Map rows also expose offsets/counts for their local
rules and patterns. Actions retain presence flags for optional fields, and placement
counts plus structured allowance reasons are emitted rather than discarded.

`render_c(data)` emits every authoritative normalized section, including map type,
ground policy, symbols, map-local rules/patterns, and all sparse child arrays.
`src/diorama/rules.c` consumes the v2 actions, pins, selectors and patterns only from
immutable snapshots. It carries archetype, pool, profile and claim ownership while
mapping them to the coarse compatibility shapes until G4 supplies occupancy spans.
Event predicates that require static event identity fail closed rather than reading
mutable gameplay state on the graphics thread.

## Commands

```bash
python3 tools/diorama_rules/validate_rules.py
python3 tools/diorama_rules/test_compile_rules.py
python3 tools/diorama_rules/compile_rules.py
python3 tools/diorama_rules/compile_rules.py --check
```
