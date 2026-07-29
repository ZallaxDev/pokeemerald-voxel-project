#ifdef ENABLE_DIORAMA

#include <math.h>
#include <stddef.h>
#include <string.h>

#include "diorama/rules.generated.h"
#include "diorama/rules.h"

static void ApplyDefinition(const struct DioramaRuleDefinition *rule,
                            enum DioramaRuleSource source,
                            struct DioramaResolvedCell *resolved)
{
    memset(resolved, 0, sizeof(*resolved));
    resolved->shape = rule->shape;
    resolved->profile = rule->profile;
    resolved->planeAxis = rule->planeAxis;
    resolved->source = source;
    resolved->baseMetatileId = rule->baseMetatileId;
    resolved->groundHeight = rule->groundHeight;
    resolved->featureHeight = rule->height;
    memcpy(resolved->materials, rule->materials, sizeof(resolved->materials));
    resolved->topHeight = rule->groundHeight;
    if (rule->shape == DIORAMA_SHAPE_EXTRUDED
     || rule->shape == DIORAMA_SHAPE_BUILDING_PART
     || rule->shape == DIORAMA_SHAPE_ROOF)
        resolved->topHeight += rule->height;
}

static const struct DioramaGeneratedLayoutRule *FindLayout(uint16_t layoutId)
{
    size_t i;

    for (i = 0; i < gDioramaLayoutRuleCount; i++)
        if (gDioramaLayoutRules[i].layoutId == layoutId)
            return &gDioramaLayoutRules[i];
    return NULL;
}

static const struct DioramaGeneratedBuildingTemplate *FindBuildingTemplate(uint16_t id)
{
    size_t i;

    for (i = 0; i < gDioramaBuildingTemplateCount; i++)
        if (gDioramaBuildingTemplates[i].id == id)
            return &gDioramaBuildingTemplates[i];
    return NULL;
}

static bool ResolveMapOverride(const struct DioramaSceneSnapshot *snapshot,
                               int mapX, int mapY,
                               struct DioramaResolvedCell *resolved)
{
    size_t i;

    for (i = 0; i < gDioramaMapOverrideCount; i++)
    {
        const struct DioramaGeneratedMapOverride *override = &gDioramaMapOverrides[i];

        if (override->mapGroup == snapshot->mapGroup
         && override->mapNum == snapshot->mapNum
         && override->x == mapX && override->y == mapY)
        {
            ApplyDefinition(&override->rule, DIORAMA_RULE_SOURCE_MAP, resolved);
            return true;
        }
    }
    return false;
}

static bool ResolveTileset(const struct DioramaSceneSnapshot *snapshot,
                           const struct DioramaCellSnapshot *cell,
                           struct DioramaResolvedCell *resolved)
{
    const struct DioramaGeneratedLayoutRule *layout = FindLayout(snapshot->mapLayoutId);
    uint8_t tileset;
    uint16_t metatileId;
    size_t i;

    if (layout == NULL)
        return false;
    if (cell->metatileId < 512)
    {
        tileset = layout->primaryTileset;
        metatileId = cell->metatileId;
    }
    else
    {
        tileset = layout->secondaryTileset;
        metatileId = cell->metatileId - 512;
    }
    for (i = 0; i < gDioramaTilesetRuleCount; i++)
    {
        const struct DioramaGeneratedTilesetRule *rule = &gDioramaTilesetRules[i];

        if (rule->tileset == tileset && rule->metatileId == metatileId)
        {
            ApplyDefinition(&rule->rule, DIORAMA_RULE_SOURCE_TILESET, resolved);
            return true;
        }
    }
    return false;
}

static bool ResolveBuilding(const struct DioramaSceneSnapshot *snapshot,
                            int mapX, int mapY,
                            struct DioramaResolvedCell *resolved)
{
    size_t i;

    for (i = 0; i < gDioramaBuildingPlacementCount; i++)
    {
        const struct DioramaGeneratedBuildingPlacement *placement = &gDioramaBuildingPlacements[i];
        const struct DioramaGeneratedBuildingTemplate *building;
        int relativeX;
        int relativeY;
        float roofFactor;

        if (placement->mapGroup != snapshot->mapGroup || placement->mapNum != snapshot->mapNum)
            continue;
        building = FindBuildingTemplate(placement->templateId);
        if (building == NULL)
            continue;
        relativeX = mapX - placement->x;
        relativeY = mapY - placement->y;
        if (relativeX < 0 || relativeY < 0
         || relativeX >= building->width || relativeY >= building->height)
            continue;

        resolved->source = DIORAMA_RULE_SOURCE_BUILDING;
        resolved->baseMetatileId = DIORAMA_MATERIAL_METATILE_SELF;
        resolved->structureId = i + 1;
        resolved->structureX = placement->x + snapshot->mapCoordinateOffset;
        resolved->structureY = placement->y + snapshot->mapCoordinateOffset;
        resolved->structureWidth = building->width;
        resolved->structureHeight = building->height;
        resolved->structureRoofRows = building->roofRows;
        resolved->structureLocalX = relativeX;
        resolved->structureLocalY = relativeY;
        resolved->structureBodyHeight = building->bodyHeight;
        resolved->structureRoofHeight = building->roofHeight;
        resolved->groundHeight = 0.0f;
        resolved->profile = building->profile;
        resolved->planeAxis = DIORAMA_PLANE_AXIS_X;
        memcpy(resolved->materials, building->materials, sizeof(resolved->materials));
        if (relativeY < building->roofRows)
        {
            resolved->shape = DIORAMA_SHAPE_ROOF;
            if (building->profile == DIORAMA_ROOF_GABLE_X)
            {
                roofFactor = 1.0f - fabsf((2.0f * (relativeX + 0.5f) / building->width) - 1.0f);
            }
            else if (building->profile == DIORAMA_ROOF_GABLE_Z)
            {
                roofFactor = 1.0f - fabsf((2.0f * (relativeY + 0.5f) / building->roofRows) - 1.0f);
            }
            else
            {
                roofFactor = 1.0f;
            }
            resolved->featureHeight = building->bodyHeight + building->roofHeight * roofFactor;
        }
        else
        {
            resolved->shape = DIORAMA_SHAPE_BUILDING_PART;
            resolved->featureHeight = building->bodyHeight;
        }
        resolved->topHeight = resolved->featureHeight;
        return true;
    }
    return false;
}

static bool ResolveBehavior(uint8_t behavior, struct DioramaResolvedCell *resolved)
{
    size_t i;

    for (i = 0; i < gDioramaBehaviorRuleCount; i++)
    {
        if (gDioramaBehaviorRules[i].behavior == behavior)
        {
            ApplyDefinition(&gDioramaBehaviorRules[i].rule,
                            DIORAMA_RULE_SOURCE_BEHAVIOR, resolved);
            return true;
        }
    }
    return false;
}

uint32_t DioramaRules_GetGeneration(void)
{
    return gDioramaRulesGeneration;
}

bool DioramaRules_IsMapSupported(uint8_t mapGroup, uint8_t mapNum, uint16_t layoutId)
{
    size_t i;

    for (i = 0; i < gDioramaMapRuleCount; i++)
        if (gDioramaMapRules[i].mapGroup == mapGroup
         && gDioramaMapRules[i].mapNum == mapNum
         && gDioramaMapRules[i].layoutId == layoutId)
            return true;
    return false;
}

bool DioramaRules_ResolveCell(const struct DioramaSceneSnapshot *snapshot,
                              const struct DioramaCellSnapshot *cell,
                              struct DioramaResolvedCell *resolved)
{
    int mapX;
    int mapY;

    if (snapshot == NULL || cell == NULL || resolved == NULL)
        return false;
    mapX = cell->mapX - snapshot->mapCoordinateOffset;
    mapY = cell->mapY - snapshot->mapCoordinateOffset;
    ApplyDefinition(&gDioramaDefaultRule, DIORAMA_RULE_SOURCE_FALLBACK, resolved);

    if (mapX >= 0 && mapY >= 0 && mapX < snapshot->mapWidth && mapY < snapshot->mapHeight
     && ResolveMapOverride(snapshot, mapX, mapY, resolved))
        return true;
    if (ResolveTileset(snapshot, cell, resolved))
        return true;
    if (mapX >= 0 && mapY >= 0 && mapX < snapshot->mapWidth && mapY < snapshot->mapHeight
     && ResolveBuilding(snapshot, mapX, mapY, resolved))
        return true;
    if (ResolveBehavior(cell->behavior, resolved))
        return true;
    if (cell->collision != 0 || (cell->elevation != 0 && cell->elevation != 3 && cell->elevation != 15))
    {
        resolved->source = DIORAMA_RULE_SOURCE_COLLISION_ELEVATION;
        return true;
    }
    if (cell->layerType != 0)
    {
        resolved->source = DIORAMA_RULE_SOURCE_HEURISTIC;
        return true;
    }
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
        DioramaRules_ResolveCell(snapshot, &snapshot->cells[i], &resolvedCells[i]);
}

float DioramaRules_DefaultGroundHeight(uint8_t behavior)
{
    struct DioramaResolvedCell resolved;

    resolved.groundHeight = 0.0f;
    if (ResolveBehavior(behavior, &resolved))
        return resolved.groundHeight;
    return 0.0f;
}

#endif
