#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "constants/metatile_behaviors.h"
#include "diorama/rules.h"

static struct DioramaSceneSnapshot sSnapshot;

#define FNV_OFFSET UINT64_C(1469598103934665603)
#define FNV_PRIME UINT64_C(1099511628211)

static uint64_t HashByte(uint64_t hash, uint8_t value)
{
    return (hash ^ value) * FNV_PRIME;
}

static uint16_t ReadU16(FILE *file)
{
    int low = fgetc(file);
    int high = fgetc(file);

    assert(low != EOF && high != EOF);
    return low | (high << 8);
}

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
}

static void TestMapSupport(void)
{
    assert(DioramaRules_IsMapSupported(0, 9, 10));
    assert(DioramaRules_IsMapSupported(0, 16, 17));
    assert(!DioramaRules_IsMapSupported(0, 10, 11));
    assert(!DioramaRules_IsMapSupported(0, 9, 17));
    assert(DioramaRules_GetGeneration() != 0);
}

static void TestPriority(void)
{
    struct DioramaResolvedCell resolved;
    struct DioramaCellSnapshot cell;

    cell = MakeCell(7, 8, 0x003, MB_NORMAL);
    assert(DioramaRules_ResolveCell(&sSnapshot, &cell, &resolved));
    assert(resolved.source == DIORAMA_RULE_SOURCE_MAP);
    assert(resolved.shape == DIORAMA_SHAPE_CUTOUT);
    assert(resolved.baseMetatileId == 1);
    assert(resolved.planeAxis == DIORAMA_PLANE_AXIS_X);
    assert(resolved.featureHeight == 0.85f);

    cell = MakeCell(10, 10, 0x1CE, MB_TALL_GRASS);
    assert(DioramaRules_ResolveCell(&sSnapshot, &cell, &resolved));
    assert(resolved.source == DIORAMA_RULE_SOURCE_TILESET);
    assert(resolved.shape == DIORAMA_SHAPE_CUTOUT);
    assert(resolved.topHeight == 0.0f);
    assert(resolved.groundHeight == 0.0f);
    assert(resolved.materials[DIORAMA_MATERIAL_FACE_TOP].layer == DIORAMA_MATERIAL_BASE);

    cell = MakeCell(5, 8, 0x248, MB_NORMAL);
    assert(DioramaRules_ResolveCell(&sSnapshot, &cell, &resolved));
    assert(resolved.source == DIORAMA_RULE_SOURCE_BUILDING);
    assert(resolved.shape == DIORAMA_SHAPE_BUILDING_PART);
    assert(resolved.structureId != 0);

    cell = MakeCell(10, 10, 1, MB_TALL_GRASS);
    assert(DioramaRules_ResolveCell(&sSnapshot, &cell, &resolved));
    assert(resolved.source == DIORAMA_RULE_SOURCE_BEHAVIOR);
    assert(resolved.shape == DIORAMA_SHAPE_CUTOUT);

    cell = MakeCell(10, 10, 1, MB_NORMAL);
    cell.collision = 1;
    assert(DioramaRules_ResolveCell(&sSnapshot, &cell, &resolved));
    assert(resolved.source == DIORAMA_RULE_SOURCE_COLLISION_ELEVATION);

    cell.collision = 0;
    cell.layerType = 1;
    assert(DioramaRules_ResolveCell(&sSnapshot, &cell, &resolved));
    assert(resolved.source == DIORAMA_RULE_SOURCE_HEURISTIC);

    cell.layerType = 0;
    assert(DioramaRules_ResolveCell(&sSnapshot, &cell, &resolved));
    assert(resolved.source == DIORAMA_RULE_SOURCE_FALLBACK);
    assert(resolved.shape == DIORAMA_SHAPE_FLAT);
}

static void TestBuildingProfile(void)
{
    struct DioramaResolvedCell edge;
    struct DioramaResolvedCell center;
    struct DioramaResolvedCell body;
    struct DioramaCellSnapshot cell = MakeCell(2, 4, 1, MB_NORMAL);

    assert(DioramaRules_ResolveCell(&sSnapshot, &cell, &edge));
    cell = MakeCell(4, 4, 1, MB_NORMAL);
    assert(DioramaRules_ResolveCell(&sSnapshot, &cell, &center));
    cell = MakeCell(4, 7, 1, MB_NORMAL);
    assert(DioramaRules_ResolveCell(&sSnapshot, &cell, &body));
    assert(edge.shape == DIORAMA_SHAPE_ROOF);
    assert(center.shape == DIORAMA_SHAPE_ROOF);
    assert(center.topHeight > edge.topHeight);
    assert(body.shape == DIORAMA_SHAPE_BUILDING_PART);
    assert(body.topHeight == 1.2f);
    assert(body.groundHeight == 0.0f);
    assert(edge.structureId == center.structureId);
    assert(body.structureId == center.structureId);
    assert(center.structureWidth == 5 && center.structureHeight == 5);
    assert(center.structureLocalX == 2 && center.structureLocalY == 0);
    assert(body.materials[DIORAMA_MATERIAL_FACE_EAST].metatileId == 0x212);
}

static void TestGridAndConnectedCoordinates(void)
{
    struct DioramaResolvedCell resolved[2];

    sSnapshot.visibleCellCount = 2;
    sSnapshot.cells[0] = MakeCell(7, 8, 3, MB_NORMAL);
    sSnapshot.cells[1] = MakeCell(5, -1, 1, MB_NORMAL);
    sSnapshot.cells[1].sourceMapX = 5;
    sSnapshot.cells[1].sourceMapY = 8;
    sSnapshot.cells[1].sourceMapGroup = 0;
    sSnapshot.cells[1].sourceMapNum = 9;
    sSnapshot.cells[1].sourceLayoutId = 10;
    sSnapshot.cells[1].flags = DIORAMA_CELL_SOURCE_VALID | DIORAMA_CELL_CONNECTED;
    DioramaRules_ResolveGrid(&sSnapshot, resolved);
    assert(resolved[0].source == DIORAMA_RULE_SOURCE_MAP);
    assert(resolved[1].source == DIORAMA_RULE_SOURCE_BUILDING);
    assert(resolved[1].structureX == 2 + sSnapshot.mapCoordinateOffset);
    assert(resolved[1].structureY == -5 + sSnapshot.mapCoordinateOffset);
}

static uint64_t ResolveMapGolden(const char *mapPath, uint8_t mapNum, uint16_t layoutId)
{
    struct DioramaSceneSnapshot snapshot;
    struct DioramaResolvedCell resolved[400];
    uint16_t primaryAttributes[512];
    uint16_t secondaryAttributes[144];
    FILE *map = fopen(mapPath, "rb");
    FILE *primary = fopen("data/tilesets/primary/general/metatile_attributes.bin", "rb");
    FILE *secondary = fopen("data/tilesets/secondary/petalburg/metatile_attributes.bin", "rb");
    uint64_t hash = FNV_OFFSET;
    int i;

    assert(map != NULL && primary != NULL && secondary != NULL);
    for (i = 0; i < 512; i++) primaryAttributes[i] = ReadU16(primary);
    for (i = 0; i < 144; i++) secondaryAttributes[i] = ReadU16(secondary);
    fclose(primary);
    fclose(secondary);

    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.mapNum = mapNum;
    snapshot.mapLayoutId = layoutId;
    snapshot.mapWidth = 20;
    snapshot.mapHeight = 20;
    snapshot.mapCoordinateOffset = 7;
    snapshot.visibleCellCount = 400;
    for (i = 0; i < 400; i++)
    {
        uint16_t entry = ReadU16(map);
        uint16_t metatileId = entry & 0x3FF;
        uint16_t attributes = metatileId < 512
                            ? primaryAttributes[metatileId]
                            : secondaryAttributes[metatileId - 512];

        snapshot.cells[i].mapX = i % 20 + snapshot.mapCoordinateOffset;
        snapshot.cells[i].mapY = i / 20 + snapshot.mapCoordinateOffset;
        snapshot.cells[i].sourceMapX = i % 20;
        snapshot.cells[i].sourceMapY = i / 20;
        snapshot.cells[i].sourceMapGroup = 0;
        snapshot.cells[i].sourceMapNum = mapNum;
        snapshot.cells[i].sourceLayoutId = layoutId;
        snapshot.cells[i].flags = DIORAMA_CELL_SOURCE_VALID;
        snapshot.cells[i].metatileId = metatileId;
        snapshot.cells[i].behavior = attributes & 0xFF;
        snapshot.cells[i].layerType = attributes >> 12;
        snapshot.cells[i].collision = (entry >> 10) & 3;
        snapshot.cells[i].elevation = entry >> 12;
    }
    assert(fgetc(map) == EOF);
    fclose(map);
    DioramaRules_ResolveGrid(&snapshot, resolved);
    for (i = 0; i < 400; i++)
    {
        uint32_t bits;

        hash = HashByte(hash, resolved[i].shape);
        hash = HashByte(hash, resolved[i].profile);
        hash = HashByte(hash, resolved[i].source);
        hash = HashByte(hash, resolved[i].planeAxis);
        hash = HashByte(hash, resolved[i].baseMetatileId);
        hash = HashByte(hash, resolved[i].baseMetatileId >> 8);
        memcpy(&bits, &resolved[i].groundHeight, sizeof(bits));
        hash = HashByte(hash, bits);
        hash = HashByte(hash, bits >> 8);
        hash = HashByte(hash, bits >> 16);
        hash = HashByte(hash, bits >> 24);
        memcpy(&bits, &resolved[i].topHeight, sizeof(bits));
        hash = HashByte(hash, bits);
        hash = HashByte(hash, bits >> 8);
        hash = HashByte(hash, bits >> 16);
        hash = HashByte(hash, bits >> 24);
        memcpy(&bits, &resolved[i].featureHeight, sizeof(bits));
        hash = HashByte(hash, bits);
        hash = HashByte(hash, bits >> 8);
        hash = HashByte(hash, bits >> 16);
        hash = HashByte(hash, bits >> 24);
        hash = HashByte(hash, resolved[i].structureId);
        hash = HashByte(hash, resolved[i].structureId >> 8);
        hash = HashByte(hash, resolved[i].structureLocalX);
        hash = HashByte(hash, resolved[i].structureLocalY);
        for (int face = 0; face < DIORAMA_MATERIAL_FACE_COUNT; face++)
        {
            hash = HashByte(hash, resolved[i].materials[face].metatileId);
            hash = HashByte(hash, resolved[i].materials[face].metatileId >> 8);
            hash = HashByte(hash, resolved[i].materials[face].layer);
        }
    }
    return hash;
}

static void TestMapGoldens(void)
{
    uint64_t littleroot = ResolveMapGolden("data/layouts/LittlerootTown/map.bin", 9, 10);
    uint64_t route101 = ResolveMapGolden("data/layouts/Route101/map.bin", 16, 17);

    assert(littleroot == UINT64_C(0xED0CDB60065A01D2));
    assert(route101 == UINT64_C(0x51D25147420FAA51));
}

int main(void)
{
    InitLittleroot();
    TestMapSupport();
    TestPriority();
    TestBuildingProfile();
    TestGridAndConnectedCoordinates();
    TestMapGoldens();
    puts("diorama rule tests passed");
    return 0;
}
