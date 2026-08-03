#ifdef ENABLE_DIORAMA

#include <stddef.h>
#include <string.h>

#include "constants/metatile_behaviors.h"
#include "diorama/rules.generated.h"
#include "diorama/rules.h"

static int FindCell(const struct DioramaSceneSnapshot *snapshot,
                    const struct DioramaCellSnapshot *origin, int dx, int dy);
static const struct DioramaGeneratedLayoutV2 *FindLayout(uint16_t layoutId);

static const struct DioramaCellSnapshot *FindSourceCell(
    const struct DioramaSceneSnapshot *snapshot, const struct DioramaCellSnapshot *origin,
    uint16_t layoutId, int16_t sourceX, int16_t sourceY)
{
    uint16_t i;

    for (i = 0; i < snapshot->visibleCellCount && i < DIORAMA_MAX_VISIBLE_CELLS; i++)
    {
        const struct DioramaCellSnapshot *cell = &snapshot->cells[i];

        if ((cell->flags & DIORAMA_CELL_SOURCE_VALID)
         && cell->sourceMapGroup == origin->sourceMapGroup
         && cell->sourceMapNum == origin->sourceMapNum
         && cell->sourceLayoutId == layoutId
         && cell->sourceMapX == sourceX && cell->sourceMapY == sourceY)
            return cell;
    }
    return NULL;
}

static bool PixelObjectMatchesSnapshot(const struct DioramaSceneSnapshot *snapshot,
                                       const struct DioramaCellSnapshot *origin,
                                       const struct DioramaGeneratedPixelObjectV2 *object)
{
    const struct DioramaGeneratedLayoutV2 *layout = FindLayout(object->layoutId);
    const struct DioramaGeneratedPixelV2 *pixels;
    size_t count;
    size_t i;
    uint32_t previousOffset = UINT32_MAX;

    if (layout == NULL)
        return false;
    pixels = DioramaRules_GetPixelObjectPixels(object, &count);
    if (pixels == NULL)
        return false;
    for (i = 0; i < count; i++)
    {
        if (pixels[i].sourceCellOffset == previousOffset)
            continue;
        previousOffset = pixels[i].sourceCellOffset;
        const struct DioramaCellSnapshot *cell = FindSourceCell(
            snapshot, origin, object->layoutId,
            pixels[i].sourceCellOffset % layout->width,
            pixels[i].sourceCellOffset / layout->width);

        if (cell != NULL && cell->metatileId != pixels[i].expectedMetatile)
            return false;
    }
    return true;
}

static bool PixelObjectSupportMatchesSnapshot(
    const struct DioramaSceneSnapshot *snapshot, const struct DioramaResolvedCell *resolvedCells,
    uint16_t count, const struct DioramaCellSnapshot *origin,
    const struct DioramaGeneratedPixelObjectV2 *object)
{
    const struct DioramaGeneratedStructureV2 *support;
    const struct DioramaGeneratedLayoutV2 *layout;
    uint16_t i;
    uint32_t cellIndex;

    if (object->supportStructureId == 0)
        return true;
    for (i = 0; i < count; i++)
        if (resolvedCells[i].structureId == object->supportStructureId
         && snapshot->cells[i].sourceMapGroup == origin->sourceMapGroup
         && snapshot->cells[i].sourceMapNum == origin->sourceMapNum)
            return true;
    support = DioramaRules_GetStructure(object->supportStructureId);
    layout = support != NULL ? FindLayout(support->layoutId) : NULL;
    if (support == NULL || layout == NULL
     || support->cellOffset > gDioramaStructureCellV2Count
     || support->cellCount > gDioramaStructureCellV2Count - support->cellOffset)
        return false;
    for (cellIndex = 0; cellIndex < support->cellCount; cellIndex++)
    {
        uint32_t offset = gDioramaStructureCellsV2[support->cellOffset + cellIndex];

        if (FindSourceCell(snapshot, origin, support->layoutId,
                           offset % layout->width, offset / layout->width) != NULL)
            return false;
    }
    return true;
}

static const struct DioramaGeneratedLayoutV2 *FindLayout(uint16_t layoutId)
{
    if (layoutId == 0 || layoutId > gDioramaLayoutV2Count)
        return NULL;
    if (gDioramaLayoutsV2[layoutId - 1].id == layoutId)
        return &gDioramaLayoutsV2[layoutId - 1];
    return NULL;
}

static const struct DioramaGeneratedTerrainV2 *FindStaticTerrain(
    const struct DioramaGeneratedLayoutV2 *layout, uint32_t cellOffset,
    uint8_t mapGroup, uint8_t mapNum)
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
    while (low < layout->terrainRecordCount)
    {
        const struct DioramaGeneratedTerrainV2 *record =
            &gDioramaTerrainV2[layout->terrainRecordOffset + low++];

        if (record->cellOffset != cellOffset)
            break;
        if (record->mapGroup == mapGroup && record->mapNumber == mapNum)
            return record;
        if (record->mapGroup == UINT8_MAX && record->mapNumber == UINT8_MAX)
            return record;
    }
    return NULL;
}

static const struct DioramaGeneratedStructureCellV2 *FindStaticStructure(
    const struct DioramaGeneratedLayoutV2 *layout, uint32_t cellOffset,
    uint16_t expectedMetatile, uint8_t mapGroup, uint8_t mapNum)
{
    size_t i;

    for (i = 0; i < gDioramaStructureOverrideV2Count; i++)
    {
        const struct DioramaGeneratedStructureOverrideV2 *override =
            &gDioramaStructureOverridesV2[i];

        if (override->layoutId == layout->id && override->cellOffset == cellOffset
         && override->expectedMetatile == expectedMetatile
         && override->mapGroup == mapGroup && override->mapNumber == mapNum)
            return &override->cell;
    }
    if (layout->structureCellOffset > gDioramaStructureOwnerV2Count
     || cellOffset >= gDioramaStructureOwnerV2Count - layout->structureCellOffset)
        return NULL;
    return &gDioramaStructureOwnersV2[layout->structureCellOffset + cellOffset];
}

static bool StructureIsDirty(const struct DioramaSceneSnapshot *snapshot,
                             const struct DioramaCellSnapshot *cell,
                             const struct DioramaGeneratedStructureV2 *structure,
                             const struct DioramaGeneratedLayoutV2 *layout)
{
    uint8_t dirtyIndex;

    if (structure == NULL || cell->sourceMapGroup != snapshot->mapGroup
     || cell->sourceMapNum != snapshot->mapNum)
        return false;
    if (snapshot->dirtyOverflow)
        return true;
    if (structure->cellOffset > gDioramaStructureCellV2Count
     || structure->cellCount > gDioramaStructureCellV2Count - structure->cellOffset)
        return true;
    for (dirtyIndex = 0; dirtyIndex < snapshot->dirtyCellCount; dirtyIndex++)
    {
        int x = snapshot->dirtyCells[dirtyIndex].mapX - snapshot->mapCoordinateOffset;
        int y = snapshot->dirtyCells[dirtyIndex].mapY - snapshot->mapCoordinateOffset;
        uint32_t target;
        uint32_t low = 0;
        uint32_t high = structure->cellCount;

        if (x < 0 || y < 0 || x >= layout->width || y >= layout->height)
            continue;
        target = (uint32_t)y * layout->width + x;
        while (low < high)
        {
            uint32_t middle = low + (high - low) / 2;
            uint32_t value = gDioramaStructureCellsV2[structure->cellOffset + middle];

            if (value < target)
                low = middle + 1;
            else
                high = middle;
        }
        if (low < structure->cellCount
         && gDioramaStructureCellsV2[structure->cellOffset + low] == target)
            return true;
    }
    return false;
}

static void InitializeResolved(struct DioramaResolvedCell *resolved)
{
    size_t i;

    memset(resolved, 0, sizeof(*resolved));
    resolved->shape = DIORAMA_SHAPE_FLAT;
    resolved->archetype = DIORAMA_ARCHETYPE_GROUND;
    resolved->terrainClass = DIORAMA_TERRAIN_CLASS_GROUND;
    resolved->classifierClass = 1;
    resolved->artMode = DIORAMA_ART_FLAT;
    resolved->source = DIORAMA_RULE_SOURCE_FALLBACK;
    resolved->baseMetatileId = DIORAMA_MATERIAL_METATILE_SELF;
    resolved->rulePriority = -32768;
    for (i = 0; i < DIORAMA_MATERIAL_FACE_COUNT; i++)
    {
        resolved->materials[i].metatileId = DIORAMA_MATERIAL_METATILE_SELF;
        resolved->materials[i].layer = i == DIORAMA_MATERIAL_FACE_PLANE
            ? DIORAMA_MATERIAL_FOREGROUND : DIORAMA_MATERIAL_FULL;
    }
}

static void ApplyStaticTerrain(const struct DioramaSceneSnapshot *snapshot,
                               const struct DioramaCellSnapshot *cell,
                               struct DioramaResolvedCell *resolved)
{
    const struct DioramaGeneratedLayoutV2 *layout;
    const struct DioramaGeneratedTerrainV2 *record;
    const struct DioramaGeneratedStructureCellV2 *structureCell;
    const struct DioramaGeneratedStructureV2 *structure;
    uint32_t cellOffset;

    if (!(cell->flags & DIORAMA_CELL_SOURCE_VALID) || cell->sourceMapX < 0
     || cell->sourceMapY < 0)
        return;
    layout = FindLayout(cell->sourceLayoutId);
    if (layout == NULL || cell->sourceMapX >= layout->width || cell->sourceMapY >= layout->height)
        return;
    cellOffset = (uint32_t)cell->sourceMapY * layout->width + cell->sourceMapX;
    record = FindStaticTerrain(layout, cellOffset, cell->sourceMapGroup, cell->sourceMapNum);
    if (record == NULL || record->expectedMetatile != cell->metatileId)
        return;
    resolved->shape = record->shape;
    resolved->archetype = record->archetype;
    resolved->terrainClass = record->terrainClass;
    resolved->classifierClass = record->classId;
    resolved->classifierSource = record->sourceId;
    resolved->semanticPool = record->pool;
    resolved->artMode = record->artMode;
    resolved->authored = record->authored;
    resolved->source = record->sourceKind;
    resolved->classifierConfidence = record->confidence;
    resolved->evidenceFlags = record->evidenceFlags;
    resolved->ambiguityFlags = record->ambiguityFlags;
    resolved->evidenceDetailsId = record->evidenceDetailsId;
    resolved->ambiguityDetailsId = record->ambiguityDetailsId;
    resolved->groundMode = record->propGroundMode;
    resolved->baseMetatileId = record->propGroundMetatile;
    resolved->planeAxis = record->axis;
    resolved->featureHeight = record->heightQ16 > 0 ? record->heightQ16 / 16.0f : 0.0f;
    resolved->groundHeight = record->heightQ16 < 0 ? record->heightQ16 / 16.0f : 0.0f;
    if (record->shape == DIORAMA_SHAPE_LEDGE)
        resolved->groundHeight = resolved->featureHeight;
    resolved->topHeight = resolved->groundHeight + resolved->featureHeight;
    if (record->shape == DIORAMA_SHAPE_LEDGE)
        resolved->topHeight = resolved->groundHeight;
    resolved->cliffEdgeMask = record->cliffEdgeMask;
    resolved->cliffBaseMask = record->cliffBaseMask;
    resolved->cliffTransitionMask = record->cliffTransitionMask;
    resolved->cliffCornerMask = record->cliffCornerMask;
    structureCell = FindStaticStructure(layout, cellOffset, cell->metatileId,
                                        cell->sourceMapGroup, cell->sourceMapNum);
    if (structureCell == NULL)
        return;
    resolved->voidKind = structureCell->voidKind;
    if (!StructureIsDirty(snapshot, cell,
                          DioramaRules_GetStructure(structureCell->claimOwner != 0
                              ? structureCell->claimOwner : structureCell->regionId), layout))
    {
        resolved->claimOwner = structureCell->claimOwner;
        resolved->regionId = structureCell->regionId;
        resolved->structureId = resolved->claimOwner != 0
                              ? resolved->claimOwner : resolved->regionId;
        resolved->doorFold = structureCell->doorFold;
        structure = DioramaRules_GetStructure(resolved->structureId);
        if (structure != NULL)
        {
            const struct DioramaGeneratedPixelObjectV2 *pixelObject;

            resolved->structureTemplateId = structure->kind == DIORAMA_OWNER_TEMPLATE
                                          ? structure->id : 0;
            resolved->structureX = structure->x;
            resolved->structureY = structure->y;
            resolved->structureWidth = structure->width;
            resolved->structureHeight = structure->height;
            resolved->structureLocalX = cell->sourceMapX - structure->x;
            resolved->structureLocalY = cell->sourceMapY - structure->y;
            resolved->structureOwnerKind = structure->kind;
            resolved->rulePriority = structure->priority;
            pixelObject = DioramaRules_FindPixelObject(layout->id,
                cell->sourceMapGroup, cell->sourceMapNum, structure->id);
            if (pixelObject != NULL)
            {
                resolved->pixelObjectId = pixelObject->id;
                resolved->groundMode = pixelObject->groundMode;
                resolved->baseMetatileId = pixelObject->groundMetatile;
            }
        }
    }
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

uint32_t DioramaRules_GetGeneration(void)
{
    return gDioramaRulesGeneration;
}

const char *DioramaRules_GetSha256(void)
{
    return gDioramaRulesSha256;
}

const char *DioramaRules_ClassName(uint16_t classId)
{
    static const char *const sNames[] = {
        NULL, "ground", "void", "water", "shallow-water", "waterfall", "current",
        "hot-spring", "grass", "flower", "animated-cutout", "ledge", "mound",
        "stairs", "stairs-n", "stairs-s", "stairs-e", "stairs-w", "stairs-down-n",
        "stairs-down-s", "stairs-down-e", "stairs-down-w", "cliff", "wall",
        "wall-volume", "roof", "top-slab", "bridge", "deck", "rail", "support",
        "tree", "forest-wall", "shrub", "hedge", "rock", "boulder", "building",
        "awning", "claim-only", "counter", "table", "desk", "bed", "bookcase",
        "billboard", "cutout", "console", "signpost", "post", "stump", "round-hull",
        "grouped-hull", "relief"
    };

    return classId < sizeof(sNames) / sizeof(sNames[0]) ? sNames[classId] : NULL;
}

const char *DioramaRules_ArtModeName(uint8_t artMode)
{
    static const char *const sNames[] = {
        "flat", "top", "upright", "grass", "flower", "stair", "cutout"
    };

    return artMode < sizeof(sNames) / sizeof(sNames[0]) ? sNames[artMode] : NULL;
}

const char *DioramaRules_ClassifierSourceName(uint16_t sourceId)
{
    return sourceId != 0 && sourceId <= gDioramaClassifierSourceV2Count
        && gDioramaClassifierSourcesV2[sourceId - 1].id == sourceId
        ? gDioramaClassifierSourcesV2[sourceId - 1].name : NULL;
}

const char *DioramaRules_EvidenceDetails(uint16_t detailsId)
{
    return detailsId != 0 && detailsId <= gDioramaEvidenceDetailV2Count
        && gDioramaEvidenceDetailsV2[detailsId - 1].id == detailsId
        ? gDioramaEvidenceDetailsV2[detailsId - 1].details : NULL;
}

const char *DioramaRules_AmbiguityDetails(uint16_t detailsId)
{
    return detailsId != 0 && detailsId <= gDioramaAmbiguityV2Count
        && gDioramaAmbiguitiesV2[detailsId - 1].id == detailsId
        ? gDioramaAmbiguitiesV2[detailsId - 1].details : NULL;
}

const struct DioramaGeneratedStructureV2 *DioramaRules_GetStructure(uint16_t id)
{
    return id != 0 && id <= gDioramaStructureV2Count
        && gDioramaStructuresV2[id - 1].id == id ? &gDioramaStructuresV2[id - 1] : NULL;
}

const struct DioramaGeneratedPixelObjectV2 *DioramaRules_GetPixelObject(uint16_t id)
{
    return id != 0 && id <= gDioramaPixelObjectV2Count
        && gDioramaPixelObjectsV2[id - 1].id == id ? &gDioramaPixelObjectsV2[id - 1] : NULL;
}

const struct DioramaGeneratedPixelObjectV2 *DioramaRules_FindPixelObject(
    uint16_t layoutId, uint8_t mapGroup, uint8_t mapNum, uint16_t structureId)
{
    const struct DioramaGeneratedPixelObjectV2 *fallback = NULL;
    size_t i;

    for (i = 0; i < gDioramaPixelObjectV2Count; i++)
    {
        const struct DioramaGeneratedPixelObjectV2 *object = &gDioramaPixelObjectsV2[i];

        if (object->layoutId != layoutId || object->structureId != structureId)
            continue;
        if (object->mapGroup == mapGroup && object->mapNumber == mapNum)
            return object;
        if (object->mapGroup == UINT8_MAX && object->mapNumber == UINT8_MAX)
            fallback = object;
    }
    return fallback;
}

const struct DioramaGeneratedPixelV2 *DioramaRules_GetPixelObjectPixels(
    const struct DioramaGeneratedPixelObjectV2 *object, size_t *count)
{
    if (count != NULL)
        *count = 0;
    if (object == NULL || object->pixelOffset > gDioramaPixelV2Count
     || object->pixelCount > gDioramaPixelV2Count - object->pixelOffset)
        return NULL;
    if (count != NULL)
        *count = object->pixelCount;
    return &gDioramaPixelsV2[object->pixelOffset];
}

const char *DioramaRules_StructureKindName(uint8_t kind)
{
    static const char *const sNames[] = {
        "none", "template", "authored", "prop", "volume", "region"
    };

    return kind < sizeof(sNames) / sizeof(sNames[0]) ? sNames[kind] : NULL;
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
    InitializeResolved(resolved);
    ApplyStaticTerrain(snapshot, cell, resolved);
    if (cell->collision != 0)
        resolved->evidenceFlags |= DIORAMA_EVIDENCE_COLLISION;
    if (cell->elevation != 0)
        resolved->evidenceFlags |= DIORAMA_EVIDENCE_ELEVATION;
    if (cell->layerType != 0)
        resolved->evidenceFlags |= DIORAMA_EVIDENCE_LAYER;
    FinalizeSurfaces(snapshot, cell, resolved);
    return true;
}

void DioramaRules_ResolveGrid(const struct DioramaSceneSnapshot *snapshot,
                              struct DioramaResolvedCell *resolvedCells)
{
    uint16_t count;
    uint16_t i;

    if (snapshot == NULL || resolvedCells == NULL)
        return;
    count = snapshot->visibleCellCount;
    if (count > DIORAMA_MAX_VISIBLE_CELLS)
        count = DIORAMA_MAX_VISIBLE_CELLS;
    for (i = 0; i < count; i++)
    {
        const struct DioramaCellSnapshot *cell = &snapshot->cells[i];

        InitializeResolved(&resolvedCells[i]);
        ApplyStaticTerrain(snapshot, cell, &resolvedCells[i]);
        if (cell->collision != 0)
            resolvedCells[i].evidenceFlags |= DIORAMA_EVIDENCE_COLLISION;
        if (cell->elevation != 0)
            resolvedCells[i].evidenceFlags |= DIORAMA_EVIDENCE_ELEVATION;
        if (cell->layerType != 0)
            resolvedCells[i].evidenceFlags |= DIORAMA_EVIDENCE_LAYER;
    }
    for (i = 0; i < count; i++)
    {
        const struct DioramaGeneratedPixelObjectV2 *object =
            DioramaRules_GetPixelObject(resolvedCells[i].pixelObjectId);
        uint16_t previous;
        uint16_t other;

        if (object == NULL)
            continue;
        for (previous = 0; previous < i; previous++)
            if (resolvedCells[previous].pixelObjectId == object->id)
                break;
        if (previous < i)
            continue;
        if (PixelObjectMatchesSnapshot(snapshot, &snapshot->cells[i], object)
         && PixelObjectSupportMatchesSnapshot(snapshot, resolvedCells, count,
                                               &snapshot->cells[i], object))
            continue;
        for (other = 0; other < count; other++)
            if (resolvedCells[other].pixelObjectId == object->id)
            {
                resolvedCells[other].pixelObjectId = 0;
                resolvedCells[other].groundMode = 0;
                resolvedCells[other].baseMetatileId = DIORAMA_MATERIAL_METATILE_SELF;
            }
    }
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
    (void)behavior;
    return 0.0f;
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
