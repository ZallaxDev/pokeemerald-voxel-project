#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "constants/metatile_behaviors.h"
#include "diorama/rules.generated.h"
#include "diorama/rules.h"

static struct DioramaSceneSnapshot sSnapshot;
static struct DioramaResolvedCell sResolvedGrid[DIORAMA_MAX_VISIBLE_CELLS];

static struct DioramaCellSnapshot MakeCell(int x, int y, uint16_t metatileId,
                                           uint8_t behavior)
{
    struct DioramaCellSnapshot cell;

    memset(&cell, 0, sizeof(cell));
    cell.mapX = x + sSnapshot.mapCoordinateOffset;
    cell.mapY = y + sSnapshot.mapCoordinateOffset;
    cell.sourceMapX = x;
    cell.sourceMapY = y;
    cell.sourceMapGroup = sSnapshot.mapGroup;
    cell.sourceMapNum = sSnapshot.mapNum;
    cell.sourceLayoutId = sSnapshot.mapLayoutId;
    cell.flags = DIORAMA_CELL_SOURCE_VALID;
    cell.metatileId = metatileId;
    cell.behavior = behavior;
    return cell;
}

static void InitLittleroot(void)
{
    memset(&sSnapshot, 0, sizeof(sSnapshot));
    sSnapshot.mapGroup = 0;
    sSnapshot.mapNum = 9;
    sSnapshot.mapLayoutId = 10;
    sSnapshot.mapWidth = 20;
    sSnapshot.mapHeight = 20;
    sSnapshot.mapCoordinateOffset = 7;
    sSnapshot.gridOriginX = 7;
    sSnapshot.gridOriginY = 7;
}

static void TestMapSupport(void)
{
    struct DioramaMapProfile profile;

    assert(DioramaRules_IsMapSupported(0, 9, 10));
    assert(DioramaRules_IsMapSupported(0, 16, 17));
    assert(DioramaRules_IsMapSupported(0, 10, 11));
    assert(DioramaRules_IsMapSupported(0, 9, 17));
    assert(!DioramaRules_IsMapSupported(0, 9, 999));
    assert(DioramaRules_GetGeneration() != 0);
    assert(strlen(DioramaRules_GetSha256()) == 64);
    assert(gDioramaTilesetV2Count == 75);
    assert(gDioramaLayoutV2Count == 441);
    assert(gDioramaMapV2Count == 518);
    assert(gDioramaTerrainV2Count == 325276);
    assert(gDioramaAmbiguityV2Count == 95);
    assert(gDioramaStructureV2Count > 2923);
    size_t terrainOffset = 0;
    for (size_t i = 0; i < gDioramaLayoutV2Count; i++)
    {
        const struct DioramaGeneratedLayoutV2 *layout = &gDioramaLayoutsV2[i];

        assert(layout->width != 0 && layout->height != 0);
        assert(layout->terrainRecordOffset == terrainOffset);
        terrainOffset += layout->terrainRecordCount;
    }
    assert(terrainOffset == gDioramaTerrainV2Count);
    assert(DioramaRules_GetMapProfile(0, 9, 10, &profile));
    assert(profile.cameraProfile == DIORAMA_CAMERA_EXTERIOR);
    assert(profile.cameraFocalLength == 130.0f);
    assert(DioramaRules_GetMapProfile(0, 10, 11, &profile));
    assert(DioramaRules_GetUnsupportedReason(0, 10, 11) == NULL);
    assert(DioramaRules_GetUnsupportedReason(0, 9, 10) == NULL);
}

static void TestCompiledStructureClaims(void)
{
    const struct DioramaGeneratedStructureV2 *structure;
    struct DioramaResolvedCell resolved;
    struct DioramaCellSnapshot cell;

    cell = MakeCell(2, 4, 520, MB_NORMAL);
    assert(DioramaRules_ResolveCell(&sSnapshot, &cell, &resolved));
    assert(resolved.claimOwner != 0);
    assert(resolved.structureOwnerKind == DIORAMA_OWNER_TEMPLATE);
    assert(resolved.structureWidth == 5 && resolved.structureHeight == 5);
    structure = DioramaRules_GetStructure(resolved.claimOwner);
    assert(structure != NULL);
    assert(strcmp(structure->owner, "petalburg-house-left-shell") == 0);

    cell = MakeCell(15, 13, 3, MB_NORMAL);
    assert(DioramaRules_ResolveCell(&sSnapshot, &cell, &resolved));
    assert(resolved.claimOwner != 0);
    assert(resolved.structureOwnerKind == DIORAMA_OWNER_TEMPLATE);
    structure = DioramaRules_GetStructure(resolved.claimOwner);
    assert(structure != NULL
        && strcmp(structure->source, "pattern:petalburg-isolated-signpost") == 0);
    assert(resolved.pixelObjectId != 0);
    assert(resolved.baseMetatileId == 1);
    assert(DioramaRules_GetPixelObject(resolved.pixelObjectId) != NULL);

    sSnapshot.mapEditGeneration = 99;
    assert(DioramaRules_ResolveCell(&sSnapshot, &cell, &resolved));
    assert(resolved.claimOwner != 0);
    sSnapshot.dirtyCellCount = 1;
    sSnapshot.dirtyCells[0].mapX = 15 + sSnapshot.mapCoordinateOffset;
    sSnapshot.dirtyCells[0].mapY = 13 + sSnapshot.mapCoordinateOffset;
    assert(DioramaRules_ResolveCell(&sSnapshot, &cell, &resolved));
    assert(resolved.claimOwner == 0 && resolved.regionId == 0);
    assert(resolved.pixelObjectId == 0);
    sSnapshot.dirtyCellCount = 0;
    sSnapshot.mapEditGeneration = 0;

    sSnapshot.mapGroup = 1;
    sSnapshot.mapNum = 0;
    sSnapshot.mapLayoutId = 54;
    cell = MakeCell(8, 0, 543, MB_NORMAL);
    assert(DioramaRules_ResolveCell(&sSnapshot, &cell, &resolved));
    assert(resolved.voidKind == DIORAMA_VOID_BLACK);
    assert(resolved.shape == DIORAMA_SHAPE_HIDDEN);
    InitLittleroot();
}

static void TestPixelObjectCatalog(void)
{
    size_t objectIndex;
    size_t totalPixels = 0;

    assert(gDioramaPixelObjectV2Count != 0);
    for (objectIndex = 0; objectIndex < gDioramaPixelObjectV2Count; objectIndex++)
    {
        const struct DioramaGeneratedPixelObjectV2 *object = &gDioramaPixelObjectsV2[objectIndex];
        const struct DioramaGeneratedPixelV2 *pixels;
        size_t count;
        size_t pixelIndex;

        assert(object->id == objectIndex + 1);
        assert(object->groundMode == DIORAMA_PIXEL_GROUND_REPLACEMENT
            || object->groundMode == DIORAMA_PIXEL_GROUND_SOURCE_BASE);
        if (object->groundMode == DIORAMA_PIXEL_GROUND_SOURCE_BASE)
            assert(object->groundMetatile == DIORAMA_MATERIAL_METATILE_SELF);
        assert(DioramaRules_GetStructure(object->structureId) != NULL);
        if (object->supportStructureId != 0)
        {
            assert(object->supportStructureId != object->structureId);
            assert(DioramaRules_GetStructure(object->supportStructureId) != NULL);
            assert(object->supportOffsetQ16 == 12);
        }
        pixels = DioramaRules_GetPixelObjectPixels(object, &count);
        assert(pixels != NULL && count == object->pixelCount && count != 0);
        assert(object->maskOffset <= gDioramaPixelMaskRowV2Count);
        assert(object->maskCount <= gDioramaPixelMaskRowV2Count - object->maskOffset);
        for (pixelIndex = 0; pixelIndex < count; pixelIndex++)
        {
            assert(pixels[pixelIndex].expectedMetatile < DIORAMA_TILE_COUNT);
            assert(pixels[pixelIndex].sourceColor != 0);
            assert(pixels[pixelIndex].sourceX < 16 && pixels[pixelIndex].sourceY < 16);
        }
        totalPixels += count;
    }
    assert(totalPixels == gDioramaPixelV2Count);
}

static void TestPixelObjectStaysInvalidAfterDirtyPublication(void)
{
    struct DioramaSceneSnapshot snapshot;
    struct DioramaResolvedCell resolved[2];

    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.mapGroup = 2;
    snapshot.mapNum = 0;
    snapshot.mapLayoutId = 59;
    snapshot.visibleCellCount = 2;
    snapshot.cells[0] = MakeCell(8, 1, 656, MB_NORMAL);
    snapshot.cells[1] = MakeCell(8, 2, 664, MB_NORMAL);
    snapshot.cells[0].mapX = snapshot.cells[0].sourceMapX = 8;
    snapshot.cells[0].mapY = snapshot.cells[0].sourceMapY = 1;
    snapshot.cells[1].mapX = snapshot.cells[1].sourceMapX = 8;
    snapshot.cells[1].mapY = snapshot.cells[1].sourceMapY = 2;
    snapshot.cells[0].sourceMapGroup = snapshot.cells[1].sourceMapGroup = 2;
    snapshot.cells[0].sourceMapNum = snapshot.cells[1].sourceMapNum = 0;
    snapshot.cells[0].sourceLayoutId = snapshot.cells[1].sourceLayoutId = 59;
    DioramaRules_ResolveGrid(&snapshot, resolved);
    assert(resolved[0].pixelObjectId != 0);
    assert(resolved[0].pixelObjectId == resolved[1].pixelObjectId);

    snapshot.mapGroup = 0;
    snapshot.mapNum = 9;
    snapshot.mapLayoutId = 10;
    snapshot.cells[0].mapX += 40;
    snapshot.cells[1].mapX += 40;
    DioramaRules_ResolveGrid(&snapshot, resolved);
    assert(resolved[0].pixelObjectId != 0);
    assert(resolved[0].pixelObjectId == resolved[1].pixelObjectId);

    snapshot.mapEditGeneration = 1;
    snapshot.cells[0].metatileId = 1;
    DioramaRules_ResolveGrid(&snapshot, resolved);
    assert(resolved[0].pixelObjectId == 0);
    assert(resolved[1].pixelObjectId == 0);
    assert(resolved[1].baseMetatileId == DIORAMA_MATERIAL_METATILE_SELF);
}

static void TestPixelObjectRequiresActiveAuthoredSupport(void)
{
    struct DioramaSceneSnapshot snapshot;
    struct DioramaResolvedCell resolved[2];

    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.mapGroup = 1;
    snapshot.mapNum = 0;
    snapshot.mapLayoutId = 54;
    snapshot.visibleCellCount = 2;
    snapshot.cells[0] = MakeCell(4, 3, 578, MB_NORMAL);
    snapshot.cells[1] = MakeCell(4, 4, 2, MB_TELEVISION);
    for (int i = 0; i < 2; i++)
    {
        snapshot.cells[i].mapX = snapshot.cells[i].sourceMapX = 4;
        snapshot.cells[i].mapY = snapshot.cells[i].sourceMapY = 3 + i;
        snapshot.cells[i].sourceMapGroup = 1;
        snapshot.cells[i].sourceMapNum = 0;
        snapshot.cells[i].sourceLayoutId = 54;
    }
    DioramaRules_ResolveGrid(&snapshot, resolved);
    assert(resolved[1].pixelObjectId != 0);
    assert(resolved[1].baseMetatileId == 513);

    snapshot.cells[0].metatileId = 513;
    snapshot.mapEditGeneration = 1;
    DioramaRules_ResolveGrid(&snapshot, resolved);
    assert(resolved[1].pixelObjectId == 0);
    assert(resolved[1].baseMetatileId == DIORAMA_MATERIAL_METATILE_SELF);
}

static void TestCompiledPinAndGuards(void)
{
    struct DioramaResolvedCell resolved;
    struct DioramaCellSnapshot cell;

    cell = MakeCell(19, 7, 4, MB_NORMAL);
    assert(DioramaRules_ResolveCell(&sSnapshot, &cell, &resolved));
    assert(resolved.source == DIORAMA_RULE_SOURCE_TILESET);
    assert(strcmp(DioramaRules_ClassName(resolved.classifierClass), "flower") == 0);
    assert(resolved.artMode == DIORAMA_ART_FLOWER);
    assert(strcmp(DioramaRules_ArtModeName(resolved.artMode), "flower") == 0);
    assert(strcmp(DioramaRules_ClassifierSourceName(resolved.classifierSource),
                  "pin:gTileset_General:4") == 0);
    assert(strstr(DioramaRules_EvidenceDetails(resolved.evidenceDetailsId),
                  "animation:gTilesetAnims_General_Flower") != NULL);
    assert(resolved.evidenceFlags & DIORAMA_EVIDENCE_ANIMATION);
    assert(resolved.shape == DIORAMA_SHAPE_FLAT);
    assert(resolved.featureHeight == 0.0f);

    cell = MakeCell(19, 7, 5, MB_NORMAL);
    assert(DioramaRules_ResolveCell(&sSnapshot, &cell, &resolved));
    assert(resolved.source == DIORAMA_RULE_SOURCE_FALLBACK);
    assert(strcmp(DioramaRules_ClassName(resolved.classifierClass), "ground") == 0);

    cell = MakeCell(19, 7, 4, MB_NORMAL);
    cell.sourceLayoutId = 11;
    assert(DioramaRules_ResolveCell(&sSnapshot, &cell, &resolved));
    assert(resolved.source == DIORAMA_RULE_SOURCE_FALLBACK);

    cell = MakeCell(19, 7, 4, MB_NORMAL);
    cell.sourceMapX = 20;
    assert(DioramaRules_ResolveCell(&sSnapshot, &cell, &resolved));
    assert(resolved.source == DIORAMA_RULE_SOURCE_FALLBACK);
}

static void TestNeutralFallbackWithoutCompiledRules(void)
{
    static const struct
    {
        uint8_t behavior;
        uint8_t collision;
        uint8_t layerType;
    } cases[] =
    {
        {MB_JUMP_SOUTH, 0, 0},
        {MB_STAIRS_OUTSIDE_ABANDONED_SHIP, 0, 0},
        {MB_POND_WATER, 0, 0},
        {MB_SAND, 0, 0},
        {MB_CAVE, 0, 0},
        {MB_NORMAL, 1, 0},
        {MB_NORMAL, 0, 1},
    };
    struct DioramaResolvedCell resolved;
    struct DioramaCellSnapshot cell;

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
    {
        cell = MakeCell(10, 10, 2, cases[i].behavior);
        cell.flags = 0;
        cell.collision = cases[i].collision;
        cell.layerType = cases[i].layerType;
        assert(DioramaRules_ResolveCell(&sSnapshot, &cell, &resolved));
        assert(resolved.shape == DIORAMA_SHAPE_FLAT);
        assert(resolved.terrainClass == DIORAMA_TERRAIN_CLASS_GROUND);
        assert(resolved.groundHeight == 0.0f);
        assert(resolved.topHeight == 0.0f);
        assert(resolved.featureHeight == 0.0f);
        assert(resolved.source == DIORAMA_RULE_SOURCE_FALLBACK);
        if (cases[i].collision)
            assert(resolved.evidenceFlags & DIORAMA_EVIDENCE_COLLISION);
        if (cases[i].layerType)
            assert(resolved.evidenceFlags & DIORAMA_EVIDENCE_LAYER);
    }
}

static void TestNominalFeatureHeights(void)
{
    struct DioramaResolvedCell resolved;
    struct DioramaCellSnapshot cell;

    sSnapshot.mapGroup = 0;
    sSnapshot.mapNum = 0;
    sSnapshot.mapLayoutId = 1;
    cell = MakeCell(22, 2, 176, MB_POND_WATER);
    assert(DioramaRules_ResolveCell(&sSnapshot, &cell, &resolved));
    assert(resolved.shape == DIORAMA_SHAPE_WATER);
    assert(resolved.groundHeight == -0.125f);
    assert(resolved.featureHeight == 0.0f);
    assert(resolved.topHeight == -0.125f);

    sSnapshot.mapNum = 5;
    sSnapshot.mapLayoutId = 6;
    cell = MakeCell(48, 9, 135, MB_JUMP_SOUTH);
    assert(DioramaRules_ResolveCell(&sSnapshot, &cell, &resolved));
    assert(resolved.shape == DIORAMA_SHAPE_LEDGE);
    assert(resolved.groundHeight == 0.375f);
    assert(resolved.featureHeight == 0.375f);

    sSnapshot.mapNum = 23;
    sSnapshot.mapLayoutId = 24;
    cell = MakeCell(29, 6, 874, MB_STAIRS_OUTSIDE_ABANDONED_SHIP);
    assert(DioramaRules_ResolveCell(&sSnapshot, &cell, &resolved));
    assert(resolved.shape == DIORAMA_SHAPE_STAIRS);
    assert(resolved.featureHeight == 1.0f);
    assert(resolved.topHeight == 1.0f);
    InitLittleroot();
}

static void TestGridAndConnectedCoordinates(void)
{
    struct DioramaResolvedCell resolved[2];

    sSnapshot.visibleCellCount = 2;
    sSnapshot.cells[0] = MakeCell(7, 8, 3, MB_NORMAL);
    sSnapshot.cells[1] = MakeCell(5, -1, 1, MB_NORMAL);
    sSnapshot.cells[1].sourceMapX = 16;
    sSnapshot.cells[1].sourceMapY = 8;
    sSnapshot.cells[1].sourceMapGroup = 0;
    sSnapshot.cells[1].sourceMapNum = 9;
    sSnapshot.cells[1].sourceLayoutId = 10;
    sSnapshot.cells[1].flags = DIORAMA_CELL_SOURCE_VALID | DIORAMA_CELL_CONNECTED;
    DioramaRules_ResolveGrid(&sSnapshot, resolved);
    assert(resolved[0].source != DIORAMA_RULE_SOURCE_MAP);
    assert(resolved[1].source != DIORAMA_RULE_SOURCE_BUILDING);
    assert(resolved[0].claimOwner != 0 && resolved[0].pixelObjectId != 0);
    assert(resolved[0].groundMode == DIORAMA_PIXEL_GROUND_REPLACEMENT);
    assert(resolved[1].claimOwner == 0);
}

static void TestContextualGameplayPlanes(void)
{
    struct DioramaResolvedCell resolved[3];

    sSnapshot.visibleCellCount = 3;
    sSnapshot.cells[0] = MakeCell(0, 0, 1, MB_NORMAL);
    sSnapshot.cells[0].elevation = 3;
    sSnapshot.cells[1] = MakeCell(1, 0, 1, MB_NORMAL);
    sSnapshot.cells[1].elevation = 0;
    sSnapshot.cells[2] = MakeCell(2, 0, 1, MB_NORMAL);
    sSnapshot.cells[2].elevation = 15;
    DioramaRules_ResolveGrid(&sSnapshot, resolved);
    assert(resolved[0].effectiveElevation == 3);
    assert(resolved[1].effectiveElevation == 3);
    assert(resolved[2].effectiveElevation == 3);
    assert(resolved[1].surfaceCount == 1);
    assert(resolved[1].surfaces[0].gameplayElevation == 3);
}

static void TestGenericMesherUtilities(void)
{
    struct DioramaResolvedCell bridge;
    float northHigh = DioramaRules_ProfileHeight(DIORAMA_ARCHETYPE_STAIRS_N,
        MB_NORMAL, 0.0f, 1.0f, 8, 0);
    float northLow = DioramaRules_ProfileHeight(DIORAMA_ARCHETYPE_STAIRS_N,
        MB_NORMAL, 0.0f, 1.0f, 8, 15);

    assert(northHigh == 1.0f);
    assert(northLow == 0.25f);
    assert(DioramaRules_ProfileHeight(DIORAMA_ARCHETYPE_STAIRS_S,
        MB_NORMAL, 0.0f, 1.0f, 8, 15) == 1.0f);
    assert(DioramaRules_ProfileHeight(DIORAMA_ARCHETYPE_STAIRS_E,
        MB_NORMAL, 0.0f, 1.0f, 0, 8) == 1.0f);
    assert(DioramaRules_ProfileHeight(DIORAMA_ARCHETYPE_STAIRS_W,
        MB_NORMAL, 0.0f, 1.0f, 15, 8) == 1.0f);
    assert(DioramaRules_ProfileHeight(DIORAMA_ARCHETYPE_STAIRS_DOWN_N,
        MB_NORMAL, 0.0f, 1.0f, 8, 0) == -1.0f);
    assert(DioramaRules_ProfileHeight(DIORAMA_ARCHETYPE_STAIRS_N,
        MB_BUMPY_SLOPE, 0.0f, 1.0f, 8, 8) == 0.5f);
    memset(&bridge, 0, sizeof(bridge));
    bridge.archetype = DIORAMA_ARCHETYPE_STAIRS_N;
    bridge.featureHeight = 1.0f;
    assert(DioramaRules_ObjectGroundHeight(&bridge, 3, 3, MB_BUMPY_SLOPE,
                                           8, 8) == 0.5f);

    memset(&bridge, 0, sizeof(bridge));
    bridge.shape = DIORAMA_SHAPE_BRIDGE;
    bridge.groundHeight = 1.75f;
    bridge.surfaceCount = 2;
    bridge.surfaces[0].gameplayElevation = 3;
    bridge.surfaces[0].topHeight = 1.75f;
    bridge.surfaces[1].gameplayElevation = 1;
    bridge.surfaces[1].topHeight = -0.125f;
    assert(DioramaRules_ObjectGroundHeight(&bridge, 15, 1, MB_NORMAL, 8, 8) == -0.125f);
    assert(DioramaRules_ObjectGroundHeight(&bridge, 15, 3, MB_NORMAL, 8, 8) == 1.75f);
}

static void TestFullGridPlaneResolution(void)
{
    InitLittleroot();
    sSnapshot.visibleCellCount = DIORAMA_MAX_VISIBLE_CELLS;
    for (int y = 0; y < DIORAMA_GRID_HEIGHT; y++)
        for (int x = 0; x < DIORAMA_GRID_WIDTH; x++)
        {
            int index = y * DIORAMA_GRID_WIDTH + x;
            sSnapshot.cells[index] = MakeCell(x, y, 1, MB_NORMAL);
            sSnapshot.cells[index].elevation = 0;
        }
    sSnapshot.cells[DIORAMA_MAX_VISIBLE_CELLS - 1].elevation = 3;
    DioramaRules_ResolveGrid(&sSnapshot, sResolvedGrid);
    for (int i = 0; i < DIORAMA_MAX_VISIBLE_CELLS; i++)
        assert(sResolvedGrid[i].effectiveElevation == 3);
}

int main(void)
{
    InitLittleroot();
    TestMapSupport();
    TestCompiledPinAndGuards();
    TestCompiledStructureClaims();
    TestPixelObjectCatalog();
    TestPixelObjectStaysInvalidAfterDirtyPublication();
    TestPixelObjectRequiresActiveAuthoredSupport();
    TestNeutralFallbackWithoutCompiledRules();
    TestNominalFeatureHeights();
    TestGridAndConnectedCoordinates();
    TestContextualGameplayPlanes();
    TestGenericMesherUtilities();
    TestFullGridPlaneResolution();
    puts("diorama rule tests passed");
    return 0;
}
