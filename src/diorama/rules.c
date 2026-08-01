#ifdef ENABLE_DIORAMA

#include <stddef.h>
#include <string.h>

#include "constants/metatile_behaviors.h"
#include "diorama/rules.generated.h"
#include "diorama/rules.h"

#define ACTION_HAS_PROFILE      (1u << 0)
#define ACTION_HAS_AXIS         (1u << 1)
#define ACTION_HAS_GROUND       (1u << 2)
#define ACTION_HAS_HEIGHT       (1u << 3)
#define ACTION_HAS_GROUND_MODE  (1u << 4)
#define ACTION_HAS_SHAPE        (1u << 6)

#define NEIGHBOR_HAS_METATILE   (1u << 0)
#define NEIGHBOR_HAS_BEHAVIOR   (1u << 1)
#define NEIGHBOR_HAS_LAYER      (1u << 2)
#define NEIGHBOR_HAS_ELEVATION  (1u << 3)

struct DioramaVolumeRun
{
    uint16_t component;
    int16_t mapX;
    int16_t northY;
    int16_t southY;
    uint8_t extent;
    uint8_t height;
    bool bounded;
    bool fromRepeat;
    bool terrainMeasured;
};

struct DioramaPlateauEdge
{
    uint16_t highRegion;
    uint16_t lowRegion;
    uint16_t transitionCell;
    int8_t delta;
};

struct DioramaPlateauLink
{
    uint16_t neighbor;
    int8_t delta;
    int16_t next;
};

static int FindCell(const struct DioramaSceneSnapshot *snapshot,
                    const struct DioramaCellSnapshot *origin, int dx, int dy);

static const struct DioramaGeneratedLayoutV2 *FindLayout(uint16_t layoutId)
{
    if (layoutId == 0 || layoutId > gDioramaLayoutV2Count)
        return NULL;
    if (gDioramaLayoutsV2[layoutId - 1].id == layoutId)
        return &gDioramaLayoutsV2[layoutId - 1];
    return NULL;
}

static const struct DioramaGeneratedTerrainV2 *FindStaticTerrain(
    const struct DioramaGeneratedLayoutV2 *layout, uint32_t cellOffset)
{
    uint32_t low = 0;
    uint32_t high;

    if (layout == NULL || layout->terrainRecordOffset > gDioramaTerrainV2Count
     || layout->terrainRecordCount > gDioramaTerrainV2Count - layout->terrainRecordOffset)
        return NULL;
    high = layout->terrainRecordCount;
    while (low < high)
    {
        uint32_t middle = low + (high - low) / 2;
        const struct DioramaGeneratedTerrainV2 *record =
            &gDioramaTerrainV2[layout->terrainRecordOffset + middle];

        if (record->cellOffset < cellOffset)
            low = middle + 1;
        else
            high = middle;
    }
    if (low < layout->terrainRecordCount
     && gDioramaTerrainV2[layout->terrainRecordOffset + low].cellOffset == cellOffset)
        return &gDioramaTerrainV2[layout->terrainRecordOffset + low];
    return NULL;
}

static void ApplyStaticTerrain(const struct DioramaCellSnapshot *cell,
                               struct DioramaResolvedCell *resolved)
{
    const struct DioramaGeneratedLayoutV2 *layout;
    const struct DioramaGeneratedTerrainV2 *record;
    uint32_t cellOffset;

    if (!(cell->flags & DIORAMA_CELL_SOURCE_VALID) || cell->sourceMapX < 0
     || cell->sourceMapY < 0)
        return;
    layout = FindLayout(cell->sourceLayoutId);
    if (layout == NULL || cell->sourceMapX >= layout->width || cell->sourceMapY >= layout->height)
        return;
    cellOffset = (uint32_t)cell->sourceMapY * layout->width + cell->sourceMapX;
    record = FindStaticTerrain(layout, cellOffset);
    if (record == NULL || record->expectedMetatile != cell->metatileId)
        return;
    if (resolved->source == DIORAMA_RULE_SOURCE_MAP
     || resolved->source == DIORAMA_RULE_SOURCE_BUILDING
     || resolved->source == DIORAMA_RULE_SOURCE_PATTERN
     || resolved->source == DIORAMA_RULE_SOURCE_CONTEXT
     || resolved->source == DIORAMA_RULE_SOURCE_EVENT)
        return;
    if (record->flags & DIORAMA_GENERATED_TERRAIN_AUTOMATIC)
    {
        if (resolved->source != DIORAMA_RULE_SOURCE_BEHAVIOR
         || resolved->shape != DIORAMA_SHAPE_FLAT
         || resolved->terrainClass != DIORAMA_TERRAIN_CLASS_ROCK
         || (cell->behavior != MB_CAVE && cell->behavior != MB_MOUNTAIN_TOP))
            return;
        resolved->source = DIORAMA_RULE_SOURCE_HEURISTIC;
    }
    resolved->shape = record->shape;
    resolved->archetype = record->archetype;
    resolved->terrainClass = record->terrainClass;
    resolved->planeAxis = record->axis;
    resolved->groundHeight = record->groundQ16 / 16.0f;
    resolved->featureHeight = record->heightQ16 / 16.0f;
    resolved->topHeight = resolved->groundHeight + resolved->featureHeight;
    resolved->volumeNorthY = record->volumeNorthY;
    resolved->volumeSouthY = record->volumeSouthY;
    resolved->volumeRunRows = record->volumeRunRows;
    resolved->volumeTopMetatile = record->volumeTopMetatile;
    memcpy(resolved->volumeBackMetatiles, record->volumeBackMetatiles,
           sizeof(resolved->volumeBackMetatiles));
    memcpy(resolved->volumeFrontMetatiles, record->volumeFrontMetatiles,
           sizeof(resolved->volumeFrontMetatiles));
    resolved->volumeTerrainMeasured =
        (record->flags & DIORAMA_GENERATED_TERRAIN_MEASURED) != 0;
    resolved->cliffEdgeMask = record->cliffEdgeMask;
    resolved->cliffBaseMask = record->cliffBaseMask;
    resolved->cliffTransitionMask = record->cliffTransitionMask;
    resolved->cliffCornerMask = record->cliffCornerMask;
}

static const struct DioramaGeneratedMapV2 *FindMap(uint8_t mapGroup, uint8_t mapNum,
                                                    uint16_t layoutId)
{
    size_t i;

    for (i = 0; i < gDioramaMapV2Count; i++)
        if (gDioramaMapsV2[i].group == mapGroup
         && gDioramaMapsV2[i].number == mapNum
         && gDioramaMapsV2[i].layoutId == layoutId)
            return &gDioramaMapsV2[i];
    return NULL;
}

static const struct DioramaGeneratedMapV2 *FindMapIdentity(uint8_t mapGroup, uint8_t mapNum)
{
    size_t i;

    for (i = 0; i < gDioramaMapV2Count; i++)
        if (gDioramaMapsV2[i].group == mapGroup && gDioramaMapsV2[i].number == mapNum)
            return &gDioramaMapsV2[i];
    return NULL;
}

bool DioramaRules_ProfileOccupies(uint8_t profileId, uint8_t x, uint8_t y)
{
    size_t profileIndex;

    if (profileId == 0)
        return x < DIORAMA_BUILDING_PIXELS_PER_CELL && y < DIORAMA_BUILDING_PIXELS_PER_CELL;
    for (profileIndex = 0; profileIndex < gDioramaProfileV2Count; profileIndex++)
    {
        const struct DioramaGeneratedProfileV2 *profile = &gDioramaProfilesV2[profileIndex];
        uint16_t maskIndex;

        if (profile->id != profileId)
            continue;
        for (maskIndex = 0; maskIndex < profile->maskCount; maskIndex++)
        {
            const struct DioramaGeneratedMaskV2 *mask =
                &gDioramaMasksV2[profile->maskOffset + maskIndex];

            if (mask->kind != 1)
                continue;
            if (x >= mask->width || y >= mask->height || y >= mask->rowCount)
                return false;
            return (gDioramaMaskRowsV2[mask->rowOffset + y] & (UINT64_C(1) << x)) != 0;
        }
        return x < DIORAMA_BUILDING_PIXELS_PER_CELL && y < DIORAMA_BUILDING_PIXELS_PER_CELL;
    }
    return false;
}

bool DioramaRules_ProfileIsFullCell(uint8_t profileId)
{
    uint8_t y;

    for (y = 0; y < DIORAMA_BUILDING_PIXELS_PER_CELL; y++)
    {
        uint8_t x;
        for (x = 0; x < DIORAMA_BUILDING_PIXELS_PER_CELL; x++)
            if (!DioramaRules_ProfileOccupies(profileId, x, y))
                return false;
    }
    return true;
}

static uint8_t ArchetypeShape(uint8_t archetype)
{
    if (archetype == DIORAMA_ARCHETYPE_VOID || archetype == DIORAMA_ARCHETYPE_CLAIM_ONLY)
        return DIORAMA_SHAPE_HIDDEN;
    if (archetype >= DIORAMA_ARCHETYPE_WATER && archetype <= DIORAMA_ARCHETYPE_HOT_SPRING)
        return DIORAMA_SHAPE_WATER;
    if (archetype == DIORAMA_ARCHETYPE_LEDGE)
        return DIORAMA_SHAPE_LEDGE;
    if (archetype == DIORAMA_ARCHETYPE_CLIFF || archetype == DIORAMA_ARCHETYPE_MOUND
     || archetype == DIORAMA_ARCHETYPE_WALL_VOLUME)
        return DIORAMA_SHAPE_CLIFF;
    if (archetype == DIORAMA_ARCHETYPE_BRIDGE || archetype == DIORAMA_ARCHETYPE_DECK
     || archetype == DIORAMA_ARCHETYPE_RAIL || archetype == DIORAMA_ARCHETYPE_SUPPORT)
        return DIORAMA_SHAPE_BRIDGE;
    if (archetype >= DIORAMA_ARCHETYPE_STAIRS_N && archetype <= DIORAMA_ARCHETYPE_STAIRS_DOWN_W)
        return DIORAMA_SHAPE_STAIRS;
    if (archetype == DIORAMA_ARCHETYPE_ROOF || archetype == DIORAMA_ARCHETYPE_TOP_SLAB
     || archetype == DIORAMA_ARCHETYPE_AWNING)
        return DIORAMA_SHAPE_ROOF;
    if (archetype == DIORAMA_ARCHETYPE_BUILDING)
        return DIORAMA_SHAPE_BUILDING_PART;
    if (archetype == DIORAMA_ARCHETYPE_BILLBOARD || archetype == DIORAMA_ARCHETYPE_SIGNPOST
     || archetype == DIORAMA_ARCHETYPE_POST)
        return DIORAMA_SHAPE_BILLBOARD;
    if (archetype >= DIORAMA_ARCHETYPE_CUTOUT)
        return DIORAMA_SHAPE_CUTOUT;
    return DIORAMA_SHAPE_FLAT;
}

static void ApplyAction(const struct DioramaGeneratedActionV2 *action,
                        enum DioramaRuleSource source, int16_t priority,
                        struct DioramaResolvedCell *resolved)
{
    size_t i;

    memset(resolved, 0, sizeof(*resolved));
    resolved->archetype = action->archetypeId;
    resolved->semanticPool = action->poolId;
    resolved->semanticProfile = action->profileId;
    resolved->terrainClass = action->terrainClass;
    resolved->shape = ArchetypeShape(action->archetypeId);
    if (action->flags & ACTION_HAS_SHAPE)
        resolved->shape = action->shape;
    resolved->volumeTerrainMeasured = resolved->shape == DIORAMA_SHAPE_CLIFF;
    resolved->profile = DIORAMA_ROOF_NONE;
    resolved->planeAxis = action->axis == 2 ? DIORAMA_PLANE_AXIS_Z
                        : action->axis == 3 ? DIORAMA_PLANE_AXIS_CROSS
                        : DIORAMA_PLANE_AXIS_X;
    resolved->source = source;
    resolved->rulePriority = priority;
    resolved->groundMode = action->groundMode;
    resolved->baseMetatileId = action->groundMode ? action->groundMetatile
                                                   : DIORAMA_MATERIAL_METATILE_SELF;
    resolved->groundHeight = action->groundOffset;
    resolved->featureHeight = action->height;
    resolved->topHeight = action->groundOffset + action->height;
    for (i = 0; i < DIORAMA_MATERIAL_FACE_COUNT; i++)
    {
        resolved->materials[i].metatileId = DIORAMA_MATERIAL_METATILE_SELF;
        resolved->materials[i].layer = i == DIORAMA_MATERIAL_FACE_PLANE
            ? DIORAMA_MATERIAL_FOREGROUND : DIORAMA_MATERIAL_FULL;
        resolved->materials[i].rotation = 0;
        resolved->materials[i].flags = 0;
    }
    if (action->flags & 32)
        for (i = 0; i < DIORAMA_MATERIAL_FACE_COUNT; i++)
        {
            resolved->materials[i].metatileId = action->faceMetatiles[i];
            resolved->materials[i].layer = action->faceLayers[i];
            resolved->materials[i].rotation = action->faceRotations[i];
            resolved->materials[i].flags = action->faceFlags[i];
        }
}

static uint8_t ResolveEffectiveElevation(const struct DioramaSceneSnapshot *snapshot,
                                         const struct DioramaCellSnapshot *cell)
{
    static const int8_t sCardinalOffsets[4][2] = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}};
    uint8_t counts[15] = {0};
    uint8_t best = 0;
    uint8_t bestCount = 0;
    int direction;

    if (cell->elevation > 0 && cell->elevation < 15)
        return cell->elevation;
    for (direction = 0; direction < 4; direction++)
    {
        int index = FindCell(snapshot, cell, sCardinalOffsets[direction][0],
                             sCardinalOffsets[direction][1]);
        uint8_t elevation;

        if (index < 0)
            continue;
        elevation = snapshot->cells[index].elevation;
        if (elevation == 0 || elevation == 15)
            continue;
        counts[elevation]++;
        if (counts[elevation] > bestCount)
        {
            best = elevation;
            bestCount = counts[elevation];
        }
    }
    if (bestCount != 0)
        for (direction = 1; direction < 15; direction++)
            if (direction != best && counts[direction] == bestCount)
                return 0;
    return best;
}

static void FinalizeSurfaces(const struct DioramaSceneSnapshot *snapshot,
                             const struct DioramaCellSnapshot *cell,
                             struct DioramaResolvedCell *resolved)
{
    float top;

    top = resolved->topHeight;

    if ((cell->elevation > 0 && cell->elevation < 15)
     || resolved->effectiveElevation == 0)
        resolved->effectiveElevation = ResolveEffectiveElevation(snapshot, cell);
    resolved->surfaceCount = resolved->shape == DIORAMA_SHAPE_HIDDEN ? 0 : 1;
    resolved->surfaces[0].topHeight = top;
    resolved->surfaces[0].bottomHeight = top - 1.0f / 16.0f;
    resolved->surfaces[0].gameplayElevation = resolved->effectiveElevation;
    if (resolved->shape == DIORAMA_SHAPE_BRIDGE)
    {
        resolved->surfaceCount = 2;
        resolved->surfaces[0].topHeight = resolved->groundHeight;
        resolved->surfaces[0].bottomHeight = resolved->groundHeight - 1.0f / 16.0f;
        resolved->surfaces[0].gameplayElevation = resolved->effectiveElevation;
        resolved->surfaces[1].topHeight = -0.125f;
        resolved->surfaces[1].bottomHeight = -0.1875f;
        resolved->surfaces[1].gameplayElevation = resolved->effectiveElevation == 1 ? 0 : 1;
    }
}

static uint8_t CellTileset(uint16_t layoutId, const struct DioramaCellSnapshot *cell,
                           uint16_t *localMetatile)
{
    const struct DioramaGeneratedLayoutV2 *layout = FindLayout(layoutId);

    if (layout == NULL)
        return 0;
    if (cell->metatileId < 0x200)
    {
        *localMetatile = cell->metatileId;
        return layout->primaryTilesetId;
    }
    *localMetatile = cell->metatileId - 0x200;
    return layout->secondaryTilesetId;
}

static bool ContainsU8(const uint8_t *values, uint32_t offset, uint16_t count, uint8_t value)
{
    uint16_t i;

    if (count == 0)
        return true;
    for (i = 0; i < count; i++)
        if (values[offset + i] == value)
            return true;
    return false;
}

static bool ContainsU16(const uint16_t *values, uint32_t offset, uint16_t count, uint16_t value)
{
    uint16_t i;

    if (count == 0)
        return true;
    for (i = 0; i < count; i++)
        if (values[offset + i] == value)
            return true;
    return false;
}

static int FindCell(const struct DioramaSceneSnapshot *snapshot,
                    const struct DioramaCellSnapshot *origin, int dx, int dy)
{
    int mapX = origin->mapX + dx;
    int mapY = origin->mapY + dy;
    int gridX = mapX - snapshot->gridOriginX;
    int gridY = mapY - snapshot->gridOriginY;
    int index;

    if (gridX < 0 || gridY < 0 || gridX >= DIORAMA_GRID_WIDTH || gridY >= DIORAMA_GRID_HEIGHT)
        return -1;
    index = gridY * DIORAMA_GRID_WIDTH + gridX;
    if (index >= snapshot->visibleCellCount || index >= DIORAMA_MAX_VISIBLE_CELLS
     || snapshot->cells[index].mapX != mapX || snapshot->cells[index].mapY != mapY)
        return -1;
    return index;
}

static bool MatchNeighbor(const struct DioramaGeneratedNeighborV2 *rule,
                          const struct DioramaCellSnapshot *cell)
{
    return (!(rule->flags & NEIGHBOR_HAS_METATILE) || rule->metatile == cell->metatileId)
        && (!(rule->flags & NEIGHBOR_HAS_BEHAVIOR) || rule->behavior == cell->behavior)
        && (!(rule->flags & NEIGHBOR_HAS_LAYER) || rule->layer == cell->layerType + 1)
        && (!(rule->flags & NEIGHBOR_HAS_ELEVATION) || rule->elevation == cell->elevation);
}

static bool MatchSelector(const struct DioramaSceneSnapshot *snapshot,
                          const struct DioramaCellSnapshot *cell, uint16_t layoutId,
                          const struct DioramaGeneratedSelectorV2 *selector)
{
    static const int8_t sOffsets[][2] = {
        { 0, 0 }, { 0, -1 }, { 1, -1 }, { 1, 0 }, { 1, 1 },
        { 0, 1 }, { -1, 1 }, { -1, 0 }, { -1, -1 }
    };
    uint16_t localMetatile;
    uint8_t tileset = CellTileset(layoutId, cell, &localMetatile);
    uint16_t i;

    (void)localMetatile;
    if (!ContainsU8(gDioramaSelectorTilesetsV2, selector->tilesetOffset,
                    selector->tilesetCount, tileset)
     || !ContainsU16(gDioramaSelectorMetatilesV2, selector->metatileOffset,
                     selector->metatileCount, cell->metatileId)
     || !ContainsU8(gDioramaSelectorBehaviorsV2, selector->behaviorOffset,
                    selector->behaviorCount, cell->behavior)
     || !ContainsU8(gDioramaSelectorLayersV2, selector->layerOffset,
                    selector->layerCount, cell->layerType + 1)
     || !ContainsU8(gDioramaSelectorElevationsV2, selector->elevationOffset,
                    selector->elevationCount, cell->elevation)
     || !ContainsU8(gDioramaSelectorMapTypesV2, selector->mapTypeOffset,
                    selector->mapTypeCount, snapshot->mapType))
        return false;
    /* Event predicates need a static event placement; they never guess from mutable globals. */
    if (selector->eventKind != 0)
        return false;
    for (i = 0; i < selector->neighborCount; i++)
    {
        const struct DioramaGeneratedNeighborV2 *neighbor =
            &gDioramaNeighborsV2[selector->neighborOffset + i];
        int index = FindCell(snapshot, cell, sOffsets[neighbor->direction][0],
                             sOffsets[neighbor->direction][1]);

        if (index < 0 || !MatchNeighbor(neighbor, &snapshot->cells[index]))
            return false;
    }
    return true;
}

static void ResolveBase(const struct DioramaSceneSnapshot *snapshot,
                        const struct DioramaCellSnapshot *cell,
                        struct DioramaResolvedCell *resolved)
{
    uint16_t layoutId = (cell->flags & DIORAMA_CELL_SOURCE_VALID)
                      ? cell->sourceLayoutId : snapshot->mapLayoutId;
    uint16_t localMetatile;
    uint8_t tileset;
    size_t i;

    ApplyAction(&gDioramaDefaultActionV2, DIORAMA_RULE_SOURCE_FALLBACK, -32768, resolved);
    tileset = CellTileset(layoutId, cell, &localMetatile);
    if (tileset != 0 && tileset <= gDioramaTilesetV2Count
     && gDioramaTilesetsV2[tileset - 1].id == tileset)
        resolved->terrainClass = gDioramaTilesetsV2[tileset - 1].terrainClass;
    for (i = 0; i < gDioramaTilesetPinV2Count; i++)
        if (gDioramaTilesetPinsV2[i].tilesetId == tileset
         && gDioramaTilesetPinsV2[i].metatile == localMetatile)
        {
            ApplyAction(&gDioramaTilesetPinsV2[i].action,
                        DIORAMA_RULE_SOURCE_TILESET, 0, resolved);
            return;
        }
    for (i = 0; i < gDioramaBehaviorRuleV2Count; i++)
        if (gDioramaBehaviorRulesV2[i].behavior == cell->behavior)
        {
            uint8_t baseTerrainClass = resolved->terrainClass;

            ApplyAction(&gDioramaBehaviorRulesV2[i].action,
                        DIORAMA_RULE_SOURCE_BEHAVIOR, -1, resolved);
            if (resolved->shape == DIORAMA_SHAPE_LEDGE
             || resolved->shape == DIORAMA_SHAPE_STAIRS)
                resolved->terrainClass = baseTerrainClass;
            return;
        }
}

static void ResolveContext(const struct DioramaSceneSnapshot *snapshot,
                           const struct DioramaCellSnapshot *cell,
                           struct DioramaResolvedCell *resolved)
{
    const struct DioramaGeneratedMapV2 *map;
    uint8_t mapGroup = (cell->flags & DIORAMA_CELL_SOURCE_VALID)
                     ? cell->sourceMapGroup : snapshot->mapGroup;
    uint8_t mapNum = (cell->flags & DIORAMA_CELL_SOURCE_VALID)
                   ? cell->sourceMapNum : snapshot->mapNum;
    uint16_t layoutId = (cell->flags & DIORAMA_CELL_SOURCE_VALID)
                      ? cell->sourceLayoutId : snapshot->mapLayoutId;
    size_t i;

    if (gDioramaContextualRuleV2Count == 0)
        return;
    map = FindMap(mapGroup, mapNum, layoutId);
    for (i = 0; i < gDioramaContextualRuleV2Count; i++)
    {
        const struct DioramaGeneratedContextualRuleV2 *rule = &gDioramaContextualRulesV2[i];

        if (rule->mapScopeId != 0 && (map == NULL || rule->mapScopeId != map->mapScopeId))
            continue;
        if (rule->priority <= resolved->rulePriority)
            continue;
        if (rule->selectorId == 0 || rule->selectorId > gDioramaSelectorV2Count)
            continue;
        if (MatchSelector(snapshot, cell, layoutId, &gDioramaSelectorsV2[rule->selectorId - 1]))
            ApplyAction(&rule->action, DIORAMA_RULE_SOURCE_CONTEXT, rule->priority, resolved);
    }
}

static bool MatchPattern(const struct DioramaSceneSnapshot *snapshot,
                         const struct DioramaCellSnapshot *anchor,
                         const struct DioramaGeneratedExactPatternV2 *pattern,
                         int *indices)
{
    const struct DioramaGeneratedLayoutV2 *layout = FindLayout(anchor->sourceLayoutId);
    uint16_t x;
    uint16_t y;

    if (layout == NULL || layout->primaryTilesetId != pattern->primaryTilesetId
     || layout->secondaryTilesetId != pattern->secondaryTilesetId)
        return false;
    for (y = 0; y < pattern->height; y++)
        for (x = 0; x < pattern->width; x++)
        {
            uint16_t offset = y * pattern->width + x;
            int index = FindCell(snapshot, anchor, x, y);

            if (index < 0
             || snapshot->cells[index].sourceLayoutId != anchor->sourceLayoutId
             || snapshot->cells[index].sourceMapGroup != anchor->sourceMapGroup
             || snapshot->cells[index].sourceMapNum != anchor->sourceMapNum
             || snapshot->cells[index].sourceMapX != anchor->sourceMapX + x
             || snapshot->cells[index].sourceMapY != anchor->sourceMapY + y
             || snapshot->cells[index].metatileId !=
                 gDioramaPatternCellsV2[pattern->cellOffset + offset])
                return false;
            indices[offset] = index;
        }
    return true;
}

/* Static topology is compiled from complete layouts. Keep the retired implementation
 * nearby for comparison until G5 settles, but never execute viewport-local inference. */
#if 0
static bool IsExplicitVolumeRule(const struct DioramaResolvedCell *resolved)
{
    return resolved->source == DIORAMA_RULE_SOURCE_MAP
        || resolved->source == DIORAMA_RULE_SOURCE_TILESET
        || resolved->source == DIORAMA_RULE_SOURCE_BUILDING
        || resolved->source == DIORAMA_RULE_SOURCE_PATTERN
        || resolved->source == DIORAMA_RULE_SOURCE_CONTEXT
        || resolved->source == DIORAMA_RULE_SOURCE_EVENT;
}

static bool IsVolumeCandidate(const struct DioramaCellSnapshot *cell,
                               const struct DioramaResolvedCell *resolved)
{
    return (cell->flags & DIORAMA_CELL_SOURCE_VALID)
        && cell->collision != 0
        && resolved->shape == DIORAMA_SHAPE_FLAT
        && !IsExplicitVolumeRule(resolved)
        && resolved->source == DIORAMA_RULE_SOURCE_BEHAVIOR
        && resolved->terrainClass == DIORAMA_TERRAIN_CLASS_ROCK
        && (cell->behavior == MB_CAVE || cell->behavior == MB_MOUNTAIN_TOP);
}

static bool SameSourceStep(const struct DioramaCellSnapshot *from,
                           const struct DioramaCellSnapshot *to, int dx, int dy)
{
    return (to->flags & DIORAMA_CELL_SOURCE_VALID)
        && from->sourceMapGroup == to->sourceMapGroup
        && from->sourceMapNum == to->sourceMapNum
        && from->sourceLayoutId == to->sourceLayoutId
        && from->sourceMapX + dx == to->sourceMapX
        && from->sourceMapY + dy == to->sourceMapY;
}

static int FindVolumeNeighbor(const struct DioramaSceneSnapshot *snapshot,
                              const struct DioramaResolvedCell *resolvedCells,
                              uint16_t index, int dx, int dy)
{
    int neighbor = FindCell(snapshot, &snapshot->cells[index], dx, dy);

    if (neighbor < 0
     || !SameSourceStep(&snapshot->cells[index], &snapshot->cells[neighbor], dx, dy)
     || !IsVolumeCandidate(&snapshot->cells[neighbor], &resolvedCells[neighbor]))
        return -1;
    return neighbor;
}

static void ResolveAutomaticVolumes(const struct DioramaSceneSnapshot *snapshot,
                                    struct DioramaResolvedCell *resolvedCells,
                                    uint16_t count)
{
    static const int8_t offsets[4][2] = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}};
    uint16_t components[DIORAMA_MAX_VISIBLE_CELLS] = {0};
    bool componentEnclosed[DIORAMA_MAX_VISIBLE_CELLS + 1] = {false};
    uint16_t queue[DIORAMA_MAX_VISIBLE_CELLS];
    struct DioramaVolumeRun runs[DIORAMA_MAX_VISIBLE_CELLS];
    uint16_t runCount = 0;
    uint16_t componentCount = 0;
    uint16_t i;

    for (i = 0; i < count; i++)
    {
        uint16_t readIndex = 0;
        uint16_t queued = 0;

        if (components[i] != 0
         || !IsVolumeCandidate(&snapshot->cells[i], &resolvedCells[i]))
            continue;
        componentCount++;
        componentEnclosed[componentCount] = true;
        components[i] = componentCount;
        queue[queued++] = i;
        while (readIndex < queued)
        {
            uint16_t current = queue[readIndex++];
            uint8_t direction;

            for (direction = 0; direction < 4; direction++)
            {
                int rawNeighbor = FindCell(snapshot, &snapshot->cells[current],
                                           offsets[direction][0], offsets[direction][1]);
                int neighbor;

                if (rawNeighbor < 0)
                    componentEnclosed[componentCount] = false;
                neighbor = FindVolumeNeighbor(snapshot, resolvedCells, current,
                                              offsets[direction][0], offsets[direction][1]);
                if (neighbor >= 0 && components[neighbor] == 0)
                {
                    components[neighbor] = componentCount;
                    queue[queued++] = neighbor;
                }
            }
        }
    }

    for (i = 0; i < count; i++)
    {
        struct DioramaVolumeRun *run;
        int north;
        int current;
        int southNeighbor;
        uint8_t extent = 0;
        uint8_t unit;
        uint8_t distance;

        if (components[i] == 0
         || FindVolumeNeighbor(snapshot, resolvedCells, i, 0, -1) >= 0)
            continue;
        run = &runs[runCount++];
        memset(run, 0, sizeof(*run));
        run->component = components[i];
        run->mapX = snapshot->cells[i].mapX;
        run->northY = snapshot->cells[i].mapY;
        north = i;
        current = i;
        do
        {
            extent++;
            southNeighbor = FindVolumeNeighbor(snapshot, resolvedCells, current, 0, 1);
            if (southNeighbor >= 0 && components[southNeighbor] == run->component)
                current = southNeighbor;
            else
                break;
        } while (extent < UINT8_MAX);
        run->southY = snapshot->cells[current].mapY;
        run->extent = extent;
        run->terrainMeasured = resolvedCells[i].source == DIORAMA_RULE_SOURCE_BEHAVIOR
                            && resolvedCells[i].terrainClass == DIORAMA_TERRAIN_CLASS_ROCK
                            && (snapshot->cells[i].behavior == MB_CAVE
                             || snapshot->cells[i].behavior == MB_MOUNTAIN_TOP);
        run->bounded = FindCell(snapshot, &snapshot->cells[north], 0, -1) >= 0
                    && FindCell(snapshot, &snapshot->cells[current], 0, 1) >= 0;
        if (!run->terrainMeasured)
        {
            run->height = 1;
            continue;
        }
        unit = extent < DIORAMA_VOLUME_MAX_ROWS ? extent : DIORAMA_VOLUME_MAX_ROWS;
        for (distance = 1; distance < extent; distance++)
        {
            int repeated = FindCell(snapshot, &snapshot->cells[current], 0, -distance);

            if (repeated >= 0
             && snapshot->cells[repeated].metatileId == snapshot->cells[current].metatileId)
            {
                unit = distance;
                if (unit > DIORAMA_VOLUME_MAX_ROWS)
                    unit = DIORAMA_VOLUME_MAX_ROWS;
                run->fromRepeat = true;
                break;
            }
        }
        if (!run->fromRepeat && extent >= 3)
        {
            int first = FindCell(snapshot, &snapshot->cells[current], 0, -1);
            int second = FindCell(snapshot, &snapshot->cells[current], 0, -2);

            if (first >= 0 && second >= 0
             && snapshot->cells[first].metatileId == snapshot->cells[second].metatileId)
            {
                unit = 1;
                run->fromRepeat = true;
            }
        }
        run->height = unit;
    }

    for (i = 1; i <= componentCount; i++)
    {
        uint16_t votes[DIORAMA_VOLUME_MAX_ROWS + 1] = {0};
        uint8_t mode = 0;
        uint16_t runIndex;

        if (!componentEnclosed[i])
            continue;
        for (runIndex = 0; runIndex < runCount; runIndex++)
            if (runs[runIndex].component == i && runs[runIndex].bounded)
                votes[runs[runIndex].height]++;
        for (uint8_t height = 1; height <= DIORAMA_VOLUME_MAX_ROWS; height++)
            if (votes[height] >= votes[mode])
                mode = height;
        for (runIndex = 0; runIndex < runCount; runIndex++)
            if (runs[runIndex].component == i && runs[runIndex].terrainMeasured
             && runs[runIndex].fromRepeat
             && mode > runs[runIndex].height)
                runs[runIndex].height = mode;
    }

    for (i = 0; i < runCount; i++)
    {
        const struct DioramaVolumeRun *run = &runs[i];
        int mapY;

        if (!run->bounded)
            continue;
        for (mapY = run->northY; mapY <= run->southY; mapY++)
        {
            int index = FindCell(snapshot, &snapshot->cells[0],
                                 run->mapX - snapshot->cells[0].mapX,
                                 mapY - snapshot->cells[0].mapY);
            struct DioramaResolvedCell *resolved;

            if (index < 0 || components[index] != run->component)
                continue;
            resolved = &resolvedCells[index];
            resolved->source = DIORAMA_RULE_SOURCE_HEURISTIC;
            resolved->shape = DIORAMA_SHAPE_CLIFF;
            resolved->archetype = DIORAMA_ARCHETYPE_WALL_VOLUME;
            if (resolved->terrainClass == DIORAMA_TERRAIN_CLASS_NONE)
                resolved->terrainClass = DIORAMA_TERRAIN_CLASS_ROCK;
            resolved->groundHeight = 0.0f;
            resolved->featureHeight = run->height;
            resolved->topHeight = run->height;
            resolved->volumeRunRows = run->height;
            resolved->volumeTerrainMeasured = run->terrainMeasured;
            resolved->volumeNorthY = run->northY;
            resolved->volumeSouthY = run->southY;
        }
    }
}

static bool SameVisualPlane(const struct DioramaResolvedCell *left,
                            const struct DioramaResolvedCell *right)
{
    return left->effectiveElevation == right->effectiveElevation;
}

static bool IsPlateauCell(const struct DioramaCellSnapshot *cell,
                           const struct DioramaResolvedCell *resolved)
{
    return (cell->collision == 0
         || (resolved->source == DIORAMA_RULE_SOURCE_TILESET
          && resolved->archetype == DIORAMA_ARCHETYPE_GROUND))
        && resolved->shape == DIORAMA_SHAPE_FLAT
        && resolved->archetype != DIORAMA_ARCHETYPE_VOID
        && resolved->terrainClass != DIORAMA_TERRAIN_CLASS_WATER;
}

static bool TransitionDirections(const struct DioramaCellSnapshot *cell,
                                 const struct DioramaResolvedCell *resolved,
                                 int8_t *highX, int8_t *highY,
                                 int8_t *lowX, int8_t *lowY)
{
    if (resolved->shape == DIORAMA_SHAPE_LEDGE)
    {
        switch (cell->behavior)
        {
        case MB_JUMP_NORTH: *lowX = 0; *lowY = -1; break;
        case MB_JUMP_EAST:  *lowX = 1; *lowY = 0; break;
        case MB_JUMP_SOUTH: *lowX = 0; *lowY = 1; break;
        case MB_JUMP_WEST:  *lowX = -1; *lowY = 0; break;
        default: return false;
        }
        *highX = -*lowX;
        *highY = -*lowY;
        return true;
    }
    if (resolved->shape != DIORAMA_SHAPE_STAIRS)
        return false;
    switch (resolved->archetype)
    {
    case DIORAMA_ARCHETYPE_STAIRS_N:
    case DIORAMA_ARCHETYPE_STAIRS_DOWN_S:
        *highX = 0; *highY = -1; break;
    case DIORAMA_ARCHETYPE_STAIRS_S:
    case DIORAMA_ARCHETYPE_STAIRS_DOWN_N:
        *highX = 0; *highY = 1; break;
    case DIORAMA_ARCHETYPE_STAIRS_E:
    case DIORAMA_ARCHETYPE_STAIRS_DOWN_W:
        *highX = 1; *highY = 0; break;
    case DIORAMA_ARCHETYPE_STAIRS_W:
    case DIORAMA_ARCHETYPE_STAIRS_DOWN_E:
        *highX = -1; *highY = 0; break;
    default:
        return false;
    }
    *lowX = -*highX;
    *lowY = -*highY;
    return true;
}

static uint16_t FindPlateauRegion(const struct DioramaSceneSnapshot *snapshot,
                                  const struct DioramaResolvedCell *resolvedCells,
                                  const uint16_t *regions, uint16_t origin,
                                  int dx, int dy)
{
    const struct DioramaCellSnapshot *originCell = &snapshot->cells[origin];
    int step;

    for (step = 1; step <= 8; step++)
    {
        int index = FindCell(snapshot, originCell, dx * step, dy * step);

        if (index < 0 || !SameSourceStep(originCell, &snapshot->cells[index],
                                         dx * step, dy * step))
            return 0;
        if (regions[index] != 0)
            return regions[index];
        if (resolvedCells[index].shape != DIORAMA_SHAPE_LEDGE
         && resolvedCells[index].shape != DIORAMA_SHAPE_STAIRS)
            return 0;
    }
    return 0;
}

static uint16_t FindPlateauAcrossCliffs(const struct DioramaSceneSnapshot *snapshot,
                                        const struct DioramaResolvedCell *resolvedCells,
                                        const uint16_t *regions, uint16_t origin,
                                        int dy)
{
    const struct DioramaCellSnapshot *originCell = &snapshot->cells[origin];
    int step;

    for (step = 1; step <= 8; step++)
    {
        int index = FindCell(snapshot, originCell, 0, dy * step);

        if (index < 0 || !SameSourceStep(originCell, &snapshot->cells[index],
                                         0, dy * step))
            return 0;
        if (regions[index] != 0)
            return regions[index];
        if (resolvedCells[index].shape != DIORAMA_SHAPE_CLIFF)
            return 0;
    }
    return 0;
}

static void ResolveVisualPlateaus(const struct DioramaSceneSnapshot *snapshot,
                                  struct DioramaResolvedCell *resolvedCells,
                                  uint16_t count)
{
    static const int8_t offsets[4][2] = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}};
    uint16_t regions[DIORAMA_MAX_VISIBLE_CELLS] = {0};
    uint16_t queue[DIORAMA_MAX_VISIBLE_CELLS];
    struct DioramaPlateauEdge edges[DIORAMA_MAX_VISIBLE_CELLS * 2];
    struct DioramaPlateauLink links[DIORAMA_MAX_VISIBLE_CELLS * 4];
    int16_t heads[DIORAMA_MAX_VISIBLE_CELLS + 1];
    int16_t levels[DIORAMA_MAX_VISIBLE_CELLS + 1];
    float levelOffsets[DIORAMA_MAX_VISIBLE_CELLS + 1] = {0};
    bool offsetSet[DIORAMA_MAX_VISIBLE_CELLS + 1] = {false};
    float anchors[DIORAMA_MAX_VISIBLE_CELLS + 1] = {0};
    bool anchored[DIORAMA_MAX_VISIBLE_CELLS + 1] = {false};
    bool anchorConflict[DIORAMA_MAX_VISIBLE_CELLS + 1] = {false};
    bool visited[DIORAMA_MAX_VISIBLE_CELLS + 1] = {false};
    bool cliffVisited[DIORAMA_MAX_VISIBLE_CELLS] = {false};
    uint16_t regionCount = 0;
    uint16_t edgeCount = 0;
    uint16_t linkCount = 0;
    uint16_t i;

    for (i = 0; i <= DIORAMA_MAX_VISIBLE_CELLS; i++)
    {
        heads[i] = -1;
        levels[i] = INT16_MAX;
    }
    for (i = 0; i < count; i++)
    {
        uint16_t read = 0;
        uint16_t queued = 0;

        if (regions[i] != 0 || !IsPlateauCell(&snapshot->cells[i], &resolvedCells[i]))
            continue;
        regionCount++;
        regions[i] = regionCount;
        queue[queued++] = i;
        while (read < queued)
        {
            uint16_t current = queue[read++];
            uint8_t direction;

            for (direction = 0; direction < 4; direction++)
            {
                int neighbor = FindCell(snapshot, &snapshot->cells[current],
                                        offsets[direction][0], offsets[direction][1]);

                if (neighbor < 0 || regions[neighbor] != 0
                 || !SameSourceStep(&snapshot->cells[current], &snapshot->cells[neighbor],
                                    offsets[direction][0], offsets[direction][1])
                 || !SameVisualPlane(&resolvedCells[current], &resolvedCells[neighbor])
                 || !IsPlateauCell(&snapshot->cells[neighbor], &resolvedCells[neighbor]))
                    continue;
                regions[neighbor] = regionCount;
                queue[queued++] = neighbor;
            }
        }
    }
    for (i = 0; i < count && edgeCount < DIORAMA_MAX_VISIBLE_CELLS * 2; i++)
    {
        int8_t highX;
        int8_t highY;
        int8_t lowX;
        int8_t lowY;
        uint16_t high;
        uint16_t low;

        if (!TransitionDirections(&snapshot->cells[i], &resolvedCells[i],
                                  &highX, &highY, &lowX, &lowY))
            continue;
        high = FindPlateauRegion(snapshot, resolvedCells, regions, i, highX, highY);
        low = FindPlateauRegion(snapshot, resolvedCells, regions, i, lowX, lowY);
        if (high == 0 || low == 0 || high == low)
            continue;
        edges[edgeCount].highRegion = high;
        edges[edgeCount].lowRegion = low;
        edges[edgeCount].transitionCell = i;
        edges[edgeCount].delta = 1;
        links[linkCount] = (struct DioramaPlateauLink){low, -edges[edgeCount].delta, heads[high]};
        heads[high] = linkCount++;
        links[linkCount] = (struct DioramaPlateauLink){high, edges[edgeCount].delta, heads[low]};
        heads[low] = linkCount++;
        edgeCount++;
    }
    for (i = 0; i < count && edgeCount < DIORAMA_MAX_VISIBLE_CELLS * 2; i++)
    {
        const struct DioramaResolvedCell *cliff = &resolvedCells[i];
        int southIndex;
        uint16_t high;
        uint16_t low;
        int8_t delta;

        if (cliff->source != DIORAMA_RULE_SOURCE_HEURISTIC
         || cliff->shape != DIORAMA_SHAPE_CLIFF
         || !cliff->volumeTerrainMeasured
         || snapshot->cells[i].mapY != cliff->volumeNorthY
         || cliff->volumeRunRows == 0)
            continue;
        southIndex = FindCell(snapshot, &snapshot->cells[i], 0,
                              cliff->volumeSouthY - snapshot->cells[i].mapY);
        if (southIndex < 0)
            continue;
        high = FindPlateauRegion(snapshot, resolvedCells, regions, i, 0, -1);
        low = FindPlateauRegion(snapshot, resolvedCells, regions, southIndex, 0, 1);
        if (high == 0 || low == 0 || high == low)
            continue;
        delta = cliff->volumeRunRows;
        edges[edgeCount] = (struct DioramaPlateauEdge){high, low, UINT16_MAX, delta};
        links[linkCount] = (struct DioramaPlateauLink){low, -delta, heads[high]};
        heads[high] = linkCount++;
        links[linkCount] = (struct DioramaPlateauLink){high, delta, heads[low]};
        heads[low] = linkCount++;
        edgeCount++;
    }
    for (i = 0; i < count && edgeCount < DIORAMA_MAX_VISIBLE_CELLS * 2; i++)
    {
        uint16_t high;
        uint16_t low;
        int8_t delta;

        if (resolvedCells[i].source != DIORAMA_RULE_SOURCE_TILESET
         || resolvedCells[i].shape != DIORAMA_SHAPE_CLIFF
         || resolvedCells[i].planeAxis != DIORAMA_PLANE_AXIS_X)
            continue;
        high = FindPlateauAcrossCliffs(snapshot, resolvedCells, regions, i, -1);
        low = FindPlateauAcrossCliffs(snapshot, resolvedCells, regions, i, 1);
        if (high == 0 || low == 0 || high == low)
            continue;
        delta = resolvedCells[i].featureHeight < 1.0f ? 1
              : resolvedCells[i].featureHeight > DIORAMA_VOLUME_MAX_ROWS
              ? DIORAMA_VOLUME_MAX_ROWS : resolvedCells[i].featureHeight;
        edges[edgeCount] = (struct DioramaPlateauEdge){high, low, UINT16_MAX, delta};
        links[linkCount] = (struct DioramaPlateauLink){low, -delta, heads[high]};
        heads[high] = linkCount++;
        links[linkCount] = (struct DioramaPlateauLink){high, delta, heads[low]};
        heads[low] = linkCount++;
        edgeCount++;
    }
    for (i = 0; i < count; i++)
    {
        uint8_t direction;

        if (resolvedCells[i].shape != DIORAMA_SHAPE_BRIDGE
         || resolvedCells[i].effectiveElevation == 0)
            continue;
        for (direction = 0; direction < 4; direction++)
        {
            int neighbor = FindCell(snapshot, &snapshot->cells[i],
                                    offsets[direction][0], offsets[direction][1]);
            uint16_t region;

            if (neighbor < 0 || !SameSourceStep(&snapshot->cells[i],
                                                &snapshot->cells[neighbor],
                                                offsets[direction][0], offsets[direction][1])
             || resolvedCells[neighbor].effectiveElevation
                != resolvedCells[i].effectiveElevation)
                continue;
            region = regions[neighbor];
            if (region == 0)
                continue;
            if (anchored[region] && anchors[region] != resolvedCells[i].groundHeight)
                anchorConflict[region] = true;
            else
            {
                anchored[region] = true;
                anchors[region] = resolvedCells[i].groundHeight;
            }
        }
    }
    for (i = 1; i <= regionCount; i++)
    {
        uint16_t read = 0;
        uint16_t queued = 0;
        int16_t minimum = 0;
        int16_t maximum = 0;
        bool conflict = false;

        if (visited[i] || heads[i] < 0)
            continue;
        visited[i] = true;
        levels[i] = 0;
        queue[queued++] = i;
        while (read < queued)
        {
            uint16_t current = queue[read++];
            int16_t link;

            if (levels[current] < minimum) minimum = levels[current];
            if (levels[current] > maximum) maximum = levels[current];
            for (link = heads[current]; link >= 0; link = links[link].next)
            {
                uint16_t neighbor = links[link].neighbor;
                int16_t expected = levels[current] + links[link].delta;

                if (levels[neighbor] == INT16_MAX)
                {
                    levels[neighbor] = expected;
                    visited[neighbor] = true;
                    queue[queued++] = neighbor;
                }
                else if (levels[neighbor] != expected)
                    conflict = true;
            }
        }
        if (conflict || maximum - minimum > DIORAMA_VOLUME_MAX_ROWS)
        {
            while (queued != 0)
                levels[queue[--queued]] = 0;
        }
        else
            while (queued != 0)
                levels[queue[--queued]] -= minimum;
    }
    for (i = 1; i <= regionCount; i++)
        if (anchored[i] && !anchorConflict[i])
        {
            uint16_t read = 0;
            uint16_t queued = 0;
            float offset;

            if (levels[i] == INT16_MAX)
                levels[i] = 0;
            offset = anchors[i] - levels[i];
            queue[queued++] = i;
            while (read < queued)
            {
                uint16_t current = queue[read++];
                int16_t link;

                if (offsetSet[current])
                    continue;
                offsetSet[current] = true;
                levelOffsets[current] = offset;
                for (link = heads[current]; link >= 0; link = links[link].next)
                    if (!offsetSet[links[link].neighbor])
                        queue[queued++] = links[link].neighbor;
            }
        }
    for (i = 0; i < count; i++)
        if (regions[i] != 0 && levels[regions[i]] != INT16_MAX)
        {
            float height = levels[regions[i]] + levelOffsets[regions[i]];

            resolvedCells[i].groundHeight += height;
            resolvedCells[i].topHeight += height;
        }
    for (i = 0; i < edgeCount; i++)
    {
        struct DioramaResolvedCell *transition;
        int16_t high = levels[edges[i].highRegion] == INT16_MAX ? 0
                     : levels[edges[i].highRegion];
        int16_t low = levels[edges[i].lowRegion] == INT16_MAX ? 0
                    : levels[edges[i].lowRegion];

        if (edges[i].transitionCell == UINT16_MAX || high <= low)
            continue;
        transition = &resolvedCells[edges[i].transitionCell];
        if (transition->shape == DIORAMA_SHAPE_LEDGE)
        {
            transition->groundHeight = high;
            transition->topHeight = high;
        }
        else
        {
            transition->groundHeight = low;
            transition->featureHeight = high - low;
            transition->topHeight = high;
        }
    }
    for (i = 0; i < count; i++)
    {
        uint16_t read = 0;
        uint16_t queued = 0;
        int16_t minimum = INT16_MAX;
        int16_t maximum = INT16_MIN;

        if (cliffVisited[i] || resolvedCells[i].shape != DIORAMA_SHAPE_CLIFF
         || !resolvedCells[i].volumeTerrainMeasured)
            continue;
        cliffVisited[i] = true;
        queue[queued++] = i;
        while (read < queued)
        {
            uint16_t current = queue[read++];
            uint8_t direction;

            for (direction = 0; direction < 4; direction++)
            {
                int neighbor = FindCell(snapshot, &snapshot->cells[current],
                                        offsets[direction][0], offsets[direction][1]);

                if (neighbor < 0)
                    continue;
                if (regions[neighbor] != 0 && levels[regions[neighbor]] != INT16_MAX)
                {
                    int16_t level = levels[regions[neighbor]];

                    if (level < minimum) minimum = level;
                    if (level > maximum) maximum = level;
                }
                else if (!cliffVisited[neighbor]
                      && resolvedCells[neighbor].shape == DIORAMA_SHAPE_CLIFF
                      && resolvedCells[neighbor].volumeTerrainMeasured
                      && SameSourceStep(&snapshot->cells[current], &snapshot->cells[neighbor],
                                        offsets[direction][0], offsets[direction][1]))
                {
                    cliffVisited[neighbor] = true;
                    queue[queued++] = neighbor;
                }
            }
        }
        if (minimum != INT16_MAX && maximum > minimum)
            while (queued != 0)
            {
                struct DioramaResolvedCell *cliff = &resolvedCells[queue[--queued]];

                cliff->groundHeight = minimum;
                cliff->featureHeight = maximum - minimum;
                cliff->topHeight = maximum;
                cliff->volumeRunRows = maximum - minimum;
            }
    }
}

static void ResolveCliffTopology(const struct DioramaSceneSnapshot *snapshot,
                                 struct DioramaResolvedCell *resolvedCells,
                                 uint16_t count)
{
    static const int8_t offsets[4][2] = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}};
    static const uint8_t edges[4] = {DIORAMA_CLIFF_EDGE_NORTH, DIORAMA_CLIFF_EDGE_EAST,
                                     DIORAMA_CLIFF_EDGE_SOUTH, DIORAMA_CLIFF_EDGE_WEST};
    uint16_t i;

    for (i = 0; i < count; i++)
    {
        struct DioramaResolvedCell *resolved = &resolvedCells[i];
        uint8_t direction;

        if (resolved->shape != DIORAMA_SHAPE_CLIFF)
            continue;
        for (direction = 0; direction < 4; direction++)
        {
            int neighborIndex = FindCell(snapshot, &snapshot->cells[i],
                                         offsets[direction][0], offsets[direction][1]);
            const struct DioramaResolvedCell *neighbor = neighborIndex >= 0
                ? &resolvedCells[neighborIndex] : NULL;
            float neighborTop = neighbor != NULL && neighbor->shape != DIORAMA_SHAPE_HIDDEN
                ? neighbor->topHeight : resolved->groundHeight;

            if (neighbor == NULL || neighbor->shape != DIORAMA_SHAPE_CLIFF
             || neighborTop < resolved->topHeight)
                resolved->cliffEdgeMask |= edges[direction];
            if (neighbor == NULL || neighborTop <= resolved->groundHeight)
                resolved->cliffBaseMask |= edges[direction];
            if (neighbor != NULL && neighbor->shape == DIORAMA_SHAPE_CLIFF
             && neighborTop != resolved->topHeight)
                resolved->cliffTransitionMask |= edges[direction];
        }
        if ((resolved->cliffEdgeMask & (DIORAMA_CLIFF_EDGE_NORTH | DIORAMA_CLIFF_EDGE_WEST))
         == (DIORAMA_CLIFF_EDGE_NORTH | DIORAMA_CLIFF_EDGE_WEST))
            resolved->cliffCornerMask |= DIORAMA_CLIFF_CORNER_NORTH_WEST;
        if ((resolved->cliffEdgeMask & (DIORAMA_CLIFF_EDGE_NORTH | DIORAMA_CLIFF_EDGE_EAST))
         == (DIORAMA_CLIFF_EDGE_NORTH | DIORAMA_CLIFF_EDGE_EAST))
            resolved->cliffCornerMask |= DIORAMA_CLIFF_CORNER_NORTH_EAST;
        if ((resolved->cliffEdgeMask & (DIORAMA_CLIFF_EDGE_SOUTH | DIORAMA_CLIFF_EDGE_EAST))
         == (DIORAMA_CLIFF_EDGE_SOUTH | DIORAMA_CLIFF_EDGE_EAST))
            resolved->cliffCornerMask |= DIORAMA_CLIFF_CORNER_SOUTH_EAST;
        if ((resolved->cliffEdgeMask & (DIORAMA_CLIFF_EDGE_SOUTH | DIORAMA_CLIFF_EDGE_WEST))
         == (DIORAMA_CLIFF_EDGE_SOUTH | DIORAMA_CLIFF_EDGE_WEST))
            resolved->cliffCornerMask |= DIORAMA_CLIFF_CORNER_SOUTH_WEST;
    }
}
#endif

uint32_t DioramaRules_GetGeneration(void)
{
    return gDioramaRulesGeneration;
}

const char *DioramaRules_GetSha256(void)
{
    return gDioramaRulesSha256;
}

const struct DioramaGeneratedBuildingTemplate *DioramaRules_GetBuildingTemplate(uint16_t id)
{
    (void)id;
    return NULL;
}

bool DioramaRules_IsMapSupported(uint8_t mapGroup, uint8_t mapNum, uint16_t layoutId)
{
    const struct DioramaGeneratedMapV2 *map = FindMap(mapGroup, mapNum, layoutId);

    if (map == NULL && FindLayout(layoutId) != NULL)
        map = FindMapIdentity(mapGroup, mapNum);
    return map != NULL && map->supported;
}

const char *DioramaRules_GetUnsupportedReason(uint8_t mapGroup, uint8_t mapNum,
                                              uint16_t layoutId)
{
    const struct DioramaGeneratedMapV2 *map = FindMap(mapGroup, mapNum, layoutId);

    if (map == NULL && FindLayout(layoutId) != NULL)
        map = FindMapIdentity(mapGroup, mapNum);
    return map == NULL ? "Map is absent from the generated G3 catalog."
                       : map->unsupportedReason;
}

bool DioramaRules_GetMapProfile(uint8_t mapGroup, uint8_t mapNum, uint16_t layoutId,
                                struct DioramaMapProfile *profile)
{
    const struct DioramaGeneratedMapV2 *map = FindMap(mapGroup, mapNum, layoutId);

    if (map == NULL && FindLayout(layoutId) != NULL)
        map = FindMapIdentity(mapGroup, mapNum);
    if (map == NULL || profile == NULL || !map->supported)
        return false;
    profile->cameraProfile = map->cameraProfile;
    profile->cameraPitch = map->pitchRadians;
    profile->cameraFocalLength = map->focalLength;
    return true;
}

bool DioramaRules_ResolveCell(const struct DioramaSceneSnapshot *snapshot,
                              const struct DioramaCellSnapshot *cell,
                              struct DioramaResolvedCell *resolved)
{
    if (snapshot == NULL || cell == NULL || resolved == NULL)
        return false;
    ResolveBase(snapshot, cell, resolved);
    ResolveContext(snapshot, cell, resolved);
    ApplyStaticTerrain(cell, resolved);
    if (resolved->source == DIORAMA_RULE_SOURCE_FALLBACK)
    {
        if (cell->collision != 0
         || (cell->elevation != 0 && cell->elevation != 3 && cell->elevation != 15))
            resolved->source = DIORAMA_RULE_SOURCE_COLLISION_ELEVATION;
        else if (cell->layerType != 0)
            resolved->source = DIORAMA_RULE_SOURCE_HEURISTIC;
    }
    FinalizeSurfaces(snapshot, cell, resolved);
    return true;
}

void DioramaRules_ResolveGrid(const struct DioramaSceneSnapshot *snapshot,
                              struct DioramaResolvedCell *resolvedCells)
{
    uint16_t count;
    uint16_t i;
    size_t patternIndex;

    if (snapshot == NULL || resolvedCells == NULL)
        return;
    count = snapshot->visibleCellCount;
    if (count > DIORAMA_MAX_VISIBLE_CELLS)
        count = DIORAMA_MAX_VISIBLE_CELLS;
    for (i = 0; i < count; i++)
    {
        ResolveBase(snapshot, &snapshot->cells[i], &resolvedCells[i]);
        ResolveContext(snapshot, &snapshot->cells[i], &resolvedCells[i]);
    }
    for (patternIndex = 0; patternIndex < gDioramaExactPatternV2Count; patternIndex++)
    {
        const struct DioramaGeneratedExactPatternV2 *pattern = &gDioramaExactPatternsV2[patternIndex];
        const struct DioramaGeneratedMapV2 *map = FindMap(snapshot->mapGroup, snapshot->mapNum,
                                                          snapshot->mapLayoutId);
        int indices[32 * 32];

        if (pattern->mapScopeId != 0 && (map == NULL || pattern->mapScopeId != map->mapScopeId))
            continue;
        for (i = 0; i < count; i++)
        {
            uint16_t offset;

            if (!MatchPattern(snapshot, &snapshot->cells[i], pattern, indices))
                continue;
            for (offset = 0; offset < pattern->width * pattern->height; offset++)
                if (gDioramaPatternClaimsV2[pattern->claimOffset + offset]
                 && pattern->priority > resolvedCells[indices[offset]].rulePriority)
                {
                    ApplyAction(&pattern->action, DIORAMA_RULE_SOURCE_PATTERN,
                                pattern->priority, &resolvedCells[indices[offset]]);
                    resolvedCells[indices[offset]].claimOwner = pattern->id;
                }
        }
    }
    for (i = 0; i < count; i++)
        ApplyStaticTerrain(&snapshot->cells[i], &resolvedCells[i]);
    for (i = 0; i < count; i++)
        resolvedCells[i].effectiveElevation = snapshot->cells[i].elevation > 0
                                          && snapshot->cells[i].elevation < 15
                                          ? snapshot->cells[i].elevation : 0;
    {
        bool visited[DIORAMA_MAX_VISIBLE_CELLS] = {false};
        uint16_t queue[DIORAMA_MAX_VISIBLE_CELLS];
        static const int8_t offsets[4][2] = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}};

        for (i = 0; i < count; i++)
        {
            uint16_t componentCount = 0;
            uint16_t readIndex = 0;
            uint8_t candidate = 0;
            bool conflict = false;

            if (visited[i] || resolvedCells[i].effectiveElevation != 0)
                continue;
            visited[i] = true;
            queue[componentCount++] = i;
            while (readIndex < componentCount)
            {
                uint16_t current = queue[readIndex++];
                uint8_t direction;

                for (direction = 0; direction < 4; direction++)
                {
                    int neighbor = FindCell(snapshot, &snapshot->cells[current],
                                            offsets[direction][0], offsets[direction][1]);
                    uint8_t elevation;

                    if (neighbor < 0)
                        continue;
                    elevation = resolvedCells[neighbor].effectiveElevation;
                    if (elevation != 0)
                    {
                        if (candidate != 0 && candidate != elevation)
                            conflict = true;
                        else
                            candidate = elevation;
                    }
                    else if (!visited[neighbor])
                    {
                        visited[neighbor] = true;
                        queue[componentCount++] = neighbor;
                    }
                }
            }
            if (!conflict && candidate != 0)
                for (readIndex = 0; readIndex < componentCount; readIndex++)
                    resolvedCells[queue[readIndex]].effectiveElevation = candidate;
        }
    }
    for (i = 0; i < count; i++)
        FinalizeSurfaces(snapshot, &snapshot->cells[i], &resolvedCells[i]);
}

float DioramaRules_DefaultGroundHeight(uint8_t behavior)
{
    size_t i;

    for (i = 0; i < gDioramaBehaviorRuleV2Count; i++)
        if (gDioramaBehaviorRulesV2[i].behavior == behavior)
            return gDioramaBehaviorRulesV2[i].action.groundOffset;
    return gDioramaDefaultActionV2.groundOffset;
}

float DioramaRules_ProfileHeight(uint8_t archetype, uint8_t behavior,
                                 float groundHeight, float featureHeight,
                                 uint8_t pixelX, uint8_t pixelY)
{
    uint8_t direction;
    uint8_t coordinate;
    uint8_t step;
    bool descending;

    if (archetype < DIORAMA_ARCHETYPE_STAIRS_N
     || archetype > DIORAMA_ARCHETYPE_STAIRS_DOWN_W)
        return groundHeight;
    descending = archetype >= DIORAMA_ARCHETYPE_STAIRS_DOWN_N;
    direction = descending ? archetype - DIORAMA_ARCHETYPE_STAIRS_DOWN_N
                           : archetype - DIORAMA_ARCHETYPE_STAIRS_N;
    coordinate = direction <= 1 ? pixelY : pixelX;
    if (direction == 0 || direction == 2)
        coordinate = DIORAMA_BUILDING_PIXELS_PER_CELL - 1 - coordinate;
    if (behavior == MB_MUDDY_SLOPE || behavior == MB_BUMPY_SLOPE)
        step = coordinate + 1;
    else
        step = (coordinate / 4 + 1) * 4;
    if (featureHeight == 0.0f)
        featureHeight = 1.0f;
    return groundHeight + (descending ? -1.0f : 1.0f)
         * featureHeight * step / DIORAMA_BUILDING_PIXELS_PER_CELL;
}

float DioramaRules_ObjectGroundHeight(const struct DioramaResolvedCell *resolved,
                                       uint8_t cellElevation, uint8_t objectElevation,
                                       uint8_t behavior,
                                       uint8_t pixelX, uint8_t pixelY)
{
    uint8_t i;

    if (resolved == NULL)
        return 0.0f;
    if (resolved->surfaceCount > 1 && objectElevation > 0 && objectElevation < 15)
        for (i = 0; i < resolved->surfaceCount; i++)
            if (resolved->surfaces[i].gameplayElevation == objectElevation)
                return resolved->surfaces[i].topHeight;
    (void)cellElevation;
    return DioramaRules_ProfileHeight(resolved->archetype, behavior,
                                       resolved->groundHeight, resolved->featureHeight,
                                       pixelX, pixelY);
}

#endif
