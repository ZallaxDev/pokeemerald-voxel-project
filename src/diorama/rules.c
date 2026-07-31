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
            ApplyAction(&gDioramaBehaviorRulesV2[i].action,
                        DIORAMA_RULE_SOURCE_BEHAVIOR, -1, resolved);
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
        && !IsExplicitVolumeRule(resolved);
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
        run->bounded = FindCell(snapshot, &snapshot->cells[north], 0, -1) >= 0
                    && FindCell(snapshot, &snapshot->cells[current], 0, 1) >= 0;
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
            if (runs[runIndex].component == i && runs[runIndex].fromRepeat
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
            resolved->volumeNorthY = run->northY;
            resolved->volumeSouthY = run->southY;
        }
    }
}

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
    ResolveAutomaticVolumes(snapshot, resolvedCells, count);
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
