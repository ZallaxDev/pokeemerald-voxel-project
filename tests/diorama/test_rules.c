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
    assert(gDioramaTerrainV2Count == 0);
    for (size_t i = 0; i < gDioramaLayoutV2Count; i++)
    {
        const struct DioramaGeneratedLayoutV2 *layout = &gDioramaLayoutsV2[i];

        assert(layout->width != 0 && layout->height != 0);
        assert(layout->terrainRecordOffset == 0);
        assert(layout->terrainRecordCount == 0);
    }
    assert(DioramaRules_GetMapProfile(0, 9, 10, &profile));
    assert(profile.cameraProfile == DIORAMA_CAMERA_EXTERIOR);
    assert(profile.cameraFocalLength == 130.0f);
    assert(DioramaRules_GetMapProfile(0, 10, 11, &profile));
    assert(DioramaRules_GetUnsupportedReason(0, 10, 11) == NULL);
    assert(DioramaRules_GetUnsupportedReason(0, 9, 10) == NULL);
}

static void TestNoMapBuildingOrPinGeometry(void)
{
    struct DioramaResolvedCell resolved;
    struct DioramaCellSnapshot cell;

    cell = MakeCell(7, 8, 0x003, MB_NORMAL);
    assert(DioramaRules_ResolveCell(&sSnapshot, &cell, &resolved));
    assert(resolved.source != DIORAMA_RULE_SOURCE_MAP);
    assert(resolved.structureId == 0);

    cell = MakeCell(2, 4, 0x208, MB_NORMAL);
    assert(DioramaRules_ResolveCell(&sSnapshot, &cell, &resolved));
    assert(resolved.source != DIORAMA_RULE_SOURCE_MAP);
    assert(resolved.source != DIORAMA_RULE_SOURCE_BUILDING);
    assert(resolved.structureId == 0);

    cell = MakeCell(10, 10, 0x1CE, MB_TALL_GRASS);
    assert(DioramaRules_ResolveCell(&sSnapshot, &cell, &resolved));
    assert(resolved.source != DIORAMA_RULE_SOURCE_TILESET);
    assert(resolved.shape == DIORAMA_SHAPE_FLAT);
    assert(resolved.structureId == 0);

    cell = MakeCell(16, 8, 0x248, MB_NORMAL);
    assert(DioramaRules_ResolveCell(&sSnapshot, &cell, &resolved));
    assert(resolved.source != DIORAMA_RULE_SOURCE_BUILDING);
    assert(resolved.shape == DIORAMA_SHAPE_FLAT);
    assert(resolved.structureId == 0);
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
        cell.collision = cases[i].collision;
        cell.layerType = cases[i].layerType;
        assert(DioramaRules_ResolveCell(&sSnapshot, &cell, &resolved));
        assert(resolved.shape == DIORAMA_SHAPE_FLAT);
        assert(resolved.terrainClass == DIORAMA_TERRAIN_CLASS_GROUND);
        assert(resolved.groundHeight == 0.0f);
        assert(resolved.topHeight == 0.0f);
        assert(resolved.featureHeight == 0.0f);
    }
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
    assert(resolved[0].structureId == 0 && resolved[1].structureId == 0);
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
    TestNoMapBuildingOrPinGeometry();
    TestNeutralFallbackWithoutCompiledRules();
    TestGridAndConnectedCoordinates();
    TestContextualGameplayPlanes();
    TestGenericMesherUtilities();
    TestFullGridPlaneResolution();
    puts("diorama rule tests passed");
    return 0;
}
