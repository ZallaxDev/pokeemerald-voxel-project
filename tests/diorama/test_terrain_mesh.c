#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "constants/metatile_behaviors.h"
#include "diorama/terrain_mesh.h"
#include "diorama/tree_model.generated.h"

static struct DioramaTerrainVertex sVertices[DIORAMA_TERRAIN_MAX_VERTICES];

static void InitInput(struct DioramaTerrainChunkInput *input, uint8_t elevation, uint8_t behavior)
{
    int x;
    int y;

    memset(input, 0, sizeof(*input));
    input->chunkX = 0;
    input->chunkY = 0;
    input->mapGeneration = 1;
    for (y = 0; y < DIORAMA_TERRAIN_INPUT_SIZE; y++)
    {
        for (x = 0; x < DIORAMA_TERRAIN_INPUT_SIZE; x++)
        {
            struct DioramaTerrainCell *cell = &input->cells[y * DIORAMA_TERRAIN_INPUT_SIZE + x];

            cell->present = true;
            cell->mapX = x - 1;
            cell->mapY = y - 1;
            cell->metatileId = 1;
            cell->behavior = behavior;
            cell->rawElevation = elevation;
            cell->shape = DIORAMA_SHAPE_FLAT;
            cell->archetype = DIORAMA_ARCHETYPE_GROUND;
            cell->planeAxis = DIORAMA_PLANE_AXIS_X;
            cell->groundHeight = DioramaTerrain_NormalizeElevation(elevation, behavior);
            cell->visualHeight = DioramaTerrain_NormalizeElevation(elevation, behavior);
            for (int face = 0; face < DIORAMA_MATERIAL_FACE_COUNT; face++)
            {
                cell->materials[face].metatileId = 1;
                cell->materials[face].layer = face == DIORAMA_MATERIAL_FACE_PLANE
                                            ? DIORAMA_MATERIAL_FOREGROUND
                                            : DIORAMA_MATERIAL_FULL;
                cell->materials[face].u0 = 0.1f;
                cell->materials[face].v0 = 0.2f;
                cell->materials[face].u1 = 0.3f;
                cell->materials[face].v1 = 0.4f;
                cell->underlayMaterials[face] = cell->materials[face];
                cell->underlayMaterials[face].layer = DIORAMA_MATERIAL_BASE;
            }
        }
    }
}

static void TestFloorDiv(void)
{
    assert(DioramaTerrain_FloorDiv(-9, 8) == -2);
    assert(DioramaTerrain_FloorDiv(-8, 8) == -1);
    assert(DioramaTerrain_FloorDiv(-1, 8) == -1);
    assert(DioramaTerrain_FloorDiv(0, 8) == 0);
    assert(DioramaTerrain_FloorDiv(7, 8) == 0);
    assert(DioramaTerrain_FloorDiv(8, 8) == 1);
    assert(DioramaTerrain_VisibleStructureOrigin(19, 0) == 19);
    assert(DioramaTerrain_VisibleStructureOrigin(19, 2) == 17);
}

static void TestElevationNormalization(void)
{
    assert(DioramaTerrain_NormalizeElevation(0, MB_NORMAL) == 0.0f);
    assert(DioramaTerrain_NormalizeElevation(15, MB_NORMAL) == 0.0f);
    assert(DioramaTerrain_NormalizeElevation(3, MB_NORMAL) == 0.0f);
    assert(DioramaTerrain_NormalizeElevation(4, MB_NORMAL) == 0.0f);
    assert(DioramaTerrain_NormalizeElevation(5, MB_NORMAL) == 0.0f);
    assert(DioramaTerrain_NormalizeElevation(12, MB_NORMAL) == 0.0f);
    assert(DioramaTerrain_NormalizeElevation(14, MB_NORMAL) == 0.0f);
    assert(DioramaTerrain_NormalizeElevation(0, MB_JUMP_SOUTH) == 0.0f);
    assert(DioramaTerrain_NormalizeElevation(3, MB_POND_WATER) == 0.0f);
    assert(DioramaTerrain_NormalizeElevation(3, MB_BRIDGE_OVER_POND_MED) == 0.0f);
    assert(DioramaTerrain_NormalizeElevation(3, MB_BRIDGE_OVER_POND_HIGH) == 0.0f);
    assert(DioramaTerrain_GetElevationSemantics(0) == DIORAMA_ELEVATION_WILDCARD);
    assert(DioramaTerrain_GetElevationSemantics(3) == DIORAMA_ELEVATION_CONCRETE);
    assert(DioramaTerrain_GetElevationSemantics(15) == DIORAMA_ELEVATION_RETAIN);
}

static void TestFlatChunk(void)
{
    struct DioramaTerrainChunkInput input;
    struct DioramaTerrainMesh mesh;

    InitInput(&input, 3, MB_NORMAL);
    assert(DioramaTerrain_BuildChunk(&input, sVertices, DIORAMA_TERRAIN_MAX_VERTICES, &mesh));
    assert(mesh.usedCompressedOccupancy);
    assert(mesh.topFaceCount == 64);
    assert(mesh.bottomFaceCount == 64);
    assert(mesh.sideFaceCount == 0);
    assert(mesh.occupancySpanCount == DIORAMA_TERRAIN_INPUT_SIZE * DIORAMA_TERRAIN_INPUT_SIZE * 16 * 16);
    assert(mesh.shellFaceCount == 2 * 8 * 8 * 16 * 16);
    assert(mesh.vertexCount == 128 * 6);
    assert(mesh.bounds.minX == -0.5f);
    assert(mesh.bounds.maxX == 7.5f);
    assert(mesh.bounds.minZ == -7.5f);
    assert(mesh.bounds.maxZ == 0.5f);
    assert(mesh.bounds.minY == -1.0f / 16.0f && mesh.bounds.maxY == 0.0f);
}

static void TestMaterialRotation(void)
{
    struct DioramaTerrainChunkInput input;
    struct DioramaTerrainMesh mesh;
    int i;

    InitInput(&input, 3, MB_NORMAL);
    for (i = 0; i < DIORAMA_TERRAIN_INPUT_SIZE * DIORAMA_TERRAIN_INPUT_SIZE; i++)
        input.cells[i].materials[DIORAMA_MATERIAL_FACE_TOP].rotation = 1;
    assert(DioramaTerrain_BuildChunk(&input, sVertices,
                                     DIORAMA_TERRAIN_MAX_VERTICES, &mesh));
    assert(sVertices[0].u > 0.299f && sVertices[0].v > 0.2f);
    assert(sVertices[1].u > 0.299f && sVertices[1].v < 0.4f);
    assert(sVertices[2].u > 0.1f && sVertices[2].u < 0.101f
           && sVertices[2].v < 0.4f);
}

static void TestProfiledGeometryUsesPixelFallback(void)
{
    struct DioramaTerrainChunkInput input;
    struct DioramaTerrainMesh mesh;
    struct DioramaTerrainCell *center;

    InitInput(&input, 3, MB_NORMAL);
    center = &input.cells[4 * DIORAMA_TERRAIN_INPUT_SIZE + 4];
    center->shape = DIORAMA_SHAPE_ROOF;
    center->structureId = 1;
    center->structureX = center->mapX;
    center->structureWidth = 1;
    center->profile = DIORAMA_ROOF_GABLE_X;
    center->structureBodyHeight = 0.5f;
    center->structureRoofHeight = 0.5f;
    assert(DioramaTerrain_BuildChunk(&input, sVertices,
                                     DIORAMA_TERRAIN_MAX_VERTICES, &mesh));
    assert(!mesh.usedCompressedOccupancy);
}

static void TestGameplayPlanesStayFlat(void)
{
    struct DioramaTerrainChunkInput input;
    struct DioramaTerrainMesh mesh;

    InitInput(&input, 4, MB_NORMAL);
    input.cells[4 * DIORAMA_TERRAIN_INPUT_SIZE + 4].rawElevation = 0;
    input.cells[5 * DIORAMA_TERRAIN_INPUT_SIZE + 5].rawElevation = 15;
    assert(DioramaTerrain_BuildChunk(&input, sVertices, DIORAMA_TERRAIN_MAX_VERTICES, &mesh));
    assert(mesh.sideFaceCount == 0);
    assert(mesh.bounds.minY == -1.0f / 16.0f && mesh.bounds.maxY == 0.0f);
}

static void TestReflectionMask(void)
{
    struct DioramaTerrainChunkInput input;
    struct DioramaTerrainMesh mesh;
    int i;
    int reflected = 0;

    InitInput(&input, 3, MB_NORMAL);
    input.cells[DIORAMA_TERRAIN_INPUT_SIZE + 1].reflective = 1;
    assert(DioramaTerrain_BuildChunk(&input, sVertices,
                                     DIORAMA_TERRAIN_MAX_VERTICES, &mesh));
    for (i = 0; i < (int)mesh.vertexCount; i++)
        if (sVertices[i].reflectionMask == 1.0f)
            reflected++;
    assert(reflected == 6);
}

static void TestGameplayElevationDoesNotCreateVisualHeight(void)
{
    struct DioramaTerrainHeightCell cells[25];
    float heights[25];
    int x;
    int y;

    memset(cells, 0, sizeof(cells));
    for (y = 0; y < 5; y++)
    {
        for (x = 0; x < 5; x++)
        {
            cells[y * 5 + x].mapX = x;
            cells[y * 5 + x].mapY = y;
            cells[y * 5 + x].behavior = MB_NORMAL;
            cells[y * 5 + x].rawElevation = x == 0 ? 4 : 3;
        }
    }
    for (x = 1; x <= 3; x++)
        cells[2 * 5 + x].behavior = MB_JUMP_SOUTH;

    DioramaTerrain_BuildHeightField(cells, 5, 5, heights);
    for (y = 0; y < 5; y++)
    {
        for (x = 0; x < 5; x++)
        {
            assert(heights[y * 5 + x] == 0.0f);
        }
    }
}

static void TestRaisedCellAndCapacity(void)
{
    struct DioramaTerrainChunkInput input;
    struct DioramaTerrainMesh mesh;
    struct DioramaTerrainCell *center;

    InitInput(&input, 3, MB_NORMAL);
    center = &input.cells[4 * DIORAMA_TERRAIN_INPUT_SIZE + 4];
    center->shape = DIORAMA_SHAPE_EXTRUDED;
    center->featureHeight = 0.75f;
    center->visualHeight = 0.75f;
    assert(DioramaTerrain_BuildChunk(&input, sVertices, DIORAMA_TERRAIN_MAX_VERTICES, &mesh));
    assert(mesh.topFaceCount == 64);
    assert(mesh.bottomFaceCount == 64);
    assert(mesh.sideFaceCount == 4);
    assert(mesh.vertexCount == 132 * 6);
    assert(mesh.bounds.maxY == 0.75f);
    assert(!DioramaTerrain_BuildChunk(&input, sVertices, mesh.vertexCount - 1, &mesh));
}

static void TestPartialChunk(void)
{
    struct DioramaTerrainChunkInput input;
    struct DioramaTerrainMesh mesh;
    int y;
    int x;

    InitInput(&input, 3, MB_NORMAL);
    for (y = 1; y <= DIORAMA_TERRAIN_CHUNK_SIZE; y++)
        for (x = 1; x <= 4; x++)
            input.cells[y * DIORAMA_TERRAIN_INPUT_SIZE + x].present = false;
    assert(DioramaTerrain_BuildChunk(&input, sVertices, DIORAMA_TERRAIN_MAX_VERTICES, &mesh));
    assert(mesh.topFaceCount == 32);
    assert(mesh.bottomFaceCount == 32);
    assert(mesh.sideFaceCount == 8);
    assert(mesh.bounds.minX == 3.5f);
    assert(mesh.bounds.maxX == 7.5f);
}

static void TestLedge(void)
{
    struct DioramaTerrainChunkInput input;
    struct DioramaTerrainMesh mesh;

    InitInput(&input, 3, MB_NORMAL);
    for (int x = 0; x < DIORAMA_TERRAIN_INPUT_SIZE; x++)
    {
        struct DioramaTerrainCell *cell = &input.cells[4 * DIORAMA_TERRAIN_INPUT_SIZE + x];
        cell->behavior = MB_JUMP_SOUTH;
        cell->shape = DIORAMA_SHAPE_LEDGE;
        cell->archetype = DIORAMA_ARCHETYPE_LEDGE;
        cell->featureHeight = DIORAMA_TERRAIN_LEDGE_HEIGHT;
        cell->materials[DIORAMA_MATERIAL_FACE_TOP].layer = DIORAMA_MATERIAL_BASE;
        for (int face = DIORAMA_MATERIAL_FACE_NORTH;
             face <= DIORAMA_MATERIAL_FACE_WEST; face++)
            cell->materials[face].layer = DIORAMA_MATERIAL_FOREGROUND;
        for (int row = 10; row < 16; row++)
            cell->foregroundAlpha[row] = 0xFFFF;
    }
    assert(DioramaTerrain_BuildChunk(&input, sVertices, DIORAMA_TERRAIN_MAX_VERTICES, &mesh));
    assert(mesh.usedCompressedOccupancy);
    assert(mesh.topFaceCount == 64);
    assert(mesh.featureFaceCount > 0);
    assert(mesh.bounds.minY == -1.0f / 16.0f);
    assert(mesh.bounds.maxY == DIORAMA_TERRAIN_LEDGE_HEIGHT);
    for (uint32_t vertex = 0; vertex < mesh.vertexCount; vertex++)
        if (sVertices[vertex].y > 0.0f)
            assert(sVertices[vertex].textureLayer == DIORAMA_MATERIAL_FOREGROUND);
}

static void TestLedgeUsesForegroundRuns(void)
{
    struct DioramaTerrainChunkInput input;
    struct DioramaTerrainMesh mesh;
    struct DioramaTerrainCell *center;
    bool foundTopUv = false;

    InitInput(&input, 3, MB_NORMAL);
    center = &input.cells[4 * DIORAMA_TERRAIN_INPUT_SIZE + 4];
    center->behavior = MB_JUMP_SOUTH;
    center->shape = DIORAMA_SHAPE_LEDGE;
    center->archetype = DIORAMA_ARCHETYPE_LEDGE;
    center->featureHeight = DIORAMA_TERRAIN_LEDGE_HEIGHT;
    center->materials[DIORAMA_MATERIAL_FACE_TOP].layer = DIORAMA_MATERIAL_BASE;
    for (int face = DIORAMA_MATERIAL_FACE_NORTH;
         face <= DIORAMA_MATERIAL_FACE_WEST; face++)
        center->materials[face].layer = DIORAMA_MATERIAL_FOREGROUND;
    center->foregroundAlpha[10] = 0xFFFF;
    center->foregroundAlpha[11] = 0xFFFF;
    center->foregroundAlpha[12] = 0xFFFE;
    center->foregroundAlpha[13] = 0xFFFE;
    center->foregroundAlpha[14] = 0xFFFC;
    center->foregroundAlpha[15] = 0xFFF0;

    assert(DioramaTerrain_BuildChunk(&input, sVertices,
                                     DIORAMA_TERRAIN_MAX_VERTICES, &mesh));
    assert(mesh.usedCompressedOccupancy);
    assert(mesh.featureFaceCount == 4);
    for (uint32_t vertex = 0; vertex < mesh.vertexCount; vertex++)
        if (sVertices[vertex].textureLayer == DIORAMA_MATERIAL_FOREGROUND
         && sVertices[vertex].y == DIORAMA_TERRAIN_LEDGE_HEIGHT)
        {
            assert(sVertices[vertex].v > 0.33f && sVertices[vertex].v < 0.34f);
            foundTopUv = true;
        }
    assert(foundTopUv);
}

static void TestDiagonalLedgeAndOcclusion(void)
{
    struct DioramaTerrainChunkInput input;
    struct DioramaTerrainMesh mesh;
    struct DioramaTerrainCell *center;
    struct DioramaTerrainCell *south;

    InitInput(&input, 3, MB_NORMAL);
    center = &input.cells[4 * DIORAMA_TERRAIN_INPUT_SIZE + 4];
    center->behavior = MB_JUMP_SOUTHEAST;
    center->shape = DIORAMA_SHAPE_LEDGE;
    center->archetype = DIORAMA_ARCHETYPE_LEDGE;
    center->featureHeight = DIORAMA_TERRAIN_LEDGE_HEIGHT;
    for (int row = 10; row < 16; row++)
        center->foregroundAlpha[row] = 0xFFFF;
    for (int face = DIORAMA_MATERIAL_FACE_NORTH;
         face <= DIORAMA_MATERIAL_FACE_WEST; face++)
        center->materials[face].layer = DIORAMA_MATERIAL_FOREGROUND;
    assert(DioramaTerrain_BuildChunk(&input, sVertices,
                                     DIORAMA_TERRAIN_MAX_VERTICES, &mesh));
    assert(mesh.featureFaceCount == 2);

    south = &input.cells[5 * DIORAMA_TERRAIN_INPUT_SIZE + 4];
    south->shape = DIORAMA_SHAPE_CLIFF;
    south->visualHeight = 1.0f;
    assert(DioramaTerrain_BuildChunk(&input, sVertices,
                                     DIORAMA_TERRAIN_MAX_VERTICES, &mesh));
    assert(mesh.featureFaceCount == 1);
}

static void TestBridgeHasTwoSurfaces(void)
{
    struct DioramaTerrainChunkInput input;
    struct DioramaTerrainMesh mesh;
    struct DioramaTerrainCell *center;

    InitInput(&input, 3, MB_NORMAL);
    center = &input.cells[4 * DIORAMA_TERRAIN_INPUT_SIZE + 4];
    center->behavior = MB_BRIDGE_OVER_POND_MED;
    center->shape = DIORAMA_SHAPE_BRIDGE;
    center->archetype = DIORAMA_ARCHETYPE_BRIDGE;
    center->groundHeight = 1.75f;
    center->visualHeight = 1.75f;
    center->surfaceCount = 2;
    center->surfaces[0].bottomHeight = 1.6875f;
    center->surfaces[0].topHeight = 1.75f;
    center->surfaces[0].gameplayElevation = 3;
    center->surfaces[1].bottomHeight = -0.1875f;
    center->surfaces[1].topHeight = -0.125f;
    center->surfaces[1].gameplayElevation = 1;
    assert(DioramaTerrain_BuildChunk(&input, sVertices, DIORAMA_TERRAIN_MAX_VERTICES, &mesh));
    assert(!mesh.usedCompressedOccupancy);
    assert(mesh.topFaceCount == 65);
    assert(mesh.bottomFaceCount == 65);
    assert(mesh.sideFaceCount == 12);
    assert(mesh.bounds.minY == -3.0f / 16.0f);
    assert(mesh.bounds.maxY == 1.75f);
}

static void TestStairsAndDescendingStairwell(void)
{
    static const uint8_t up[4] = {
        DIORAMA_ARCHETYPE_STAIRS_N, DIORAMA_ARCHETYPE_STAIRS_S,
        DIORAMA_ARCHETYPE_STAIRS_E, DIORAMA_ARCHETYPE_STAIRS_W
    };
    struct DioramaTerrainChunkInput input;
    struct DioramaTerrainMesh mesh;
    uint64_t hashes[4];

    for (int direction = 0; direction < 4; direction++)
    {
        struct DioramaTerrainCell *center;

        InitInput(&input, 3, MB_NORMAL);
        center = &input.cells[4 * DIORAMA_TERRAIN_INPUT_SIZE + 4];
        center->shape = DIORAMA_SHAPE_STAIRS;
        center->archetype = up[direction];
        center->featureHeight = 1.0f;
        assert(DioramaTerrain_BuildChunk(&input, sVertices,
                                         DIORAMA_TERRAIN_MAX_VERTICES, &mesh));
        assert(!mesh.usedCompressedOccupancy);
        assert(mesh.bounds.maxY == 1.0f);
        hashes[direction] = mesh.geometryHash;
    }
    assert(hashes[0] != hashes[1]);
    assert(hashes[0] != hashes[2]);
    assert(hashes[2] != hashes[3]);

    InitInput(&input, 3, MB_NORMAL);
    input.cells[4 * DIORAMA_TERRAIN_INPUT_SIZE + 4].shape = DIORAMA_SHAPE_STAIRS;
    input.cells[4 * DIORAMA_TERRAIN_INPUT_SIZE + 4].archetype = DIORAMA_ARCHETYPE_STAIRS_DOWN_S;
    input.cells[4 * DIORAMA_TERRAIN_INPUT_SIZE + 4].featureHeight = 1.0f;
    assert(DioramaTerrain_BuildChunk(&input, sVertices, DIORAMA_TERRAIN_MAX_VERTICES, &mesh));
    assert(mesh.bounds.minY == -17.0f / 16.0f);
    assert(mesh.bounds.maxY == 0.0f);
}

static void TestCliffFaceBandsAndRamp(void)
{
    struct DioramaTerrainChunkInput input;
    struct DioramaTerrainMesh mesh;
    struct DioramaTerrainCell *center;

    InitInput(&input, 3, MB_NORMAL);
    center = &input.cells[4 * DIORAMA_TERRAIN_INPUT_SIZE + 4];
    center->shape = DIORAMA_SHAPE_CLIFF;
    center->archetype = DIORAMA_ARCHETYPE_CLIFF;
    center->featureHeight = 2.5f;
    center->visualHeight = 2.5f;
    assert(DioramaTerrain_BuildChunk(&input, sVertices, DIORAMA_TERRAIN_MAX_VERTICES, &mesh));
    assert(mesh.sideFaceCount == 12);
    assert(mesh.bounds.maxY == 2.5f);

    InitInput(&input, 3, MB_NORMAL);
    center = &input.cells[4 * DIORAMA_TERRAIN_INPUT_SIZE + 4];
    center->behavior = MB_BUMPY_SLOPE;
    center->shape = DIORAMA_SHAPE_STAIRS;
    center->archetype = DIORAMA_ARCHETYPE_STAIRS_N;
    center->featureHeight = 1.0f;
    assert(DioramaTerrain_BuildChunk(&input, sVertices, DIORAMA_TERRAIN_MAX_VERTICES, &mesh));
    assert(mesh.topFaceCount == 79);
    assert(mesh.bounds.maxY == 1.0f);
}

static void TestCliffBaseCornersAndHeightTransition(void)
{
    struct DioramaTerrainChunkInput input;
    struct DioramaTerrainMesh mesh;
    struct DioramaTerrainCell *high;
    struct DioramaTerrainCell *low;

    InitInput(&input, 3, MB_NORMAL);
    high = &input.cells[4 * DIORAMA_TERRAIN_INPUT_SIZE + 4];
    low = &input.cells[4 * DIORAMA_TERRAIN_INPUT_SIZE + 5];
    high->shape = low->shape = DIORAMA_SHAPE_CLIFF;
    high->archetype = low->archetype = DIORAMA_ARCHETYPE_CLIFF;
    high->visualHeight = 2.0f;
    low->visualHeight = 1.0f;
    high->cliffEdgeMask = DIORAMA_CLIFF_EDGE_NORTH | DIORAMA_CLIFF_EDGE_EAST
                        | DIORAMA_CLIFF_EDGE_SOUTH | DIORAMA_CLIFF_EDGE_WEST;
    high->cliffBaseMask = DIORAMA_CLIFF_EDGE_NORTH | DIORAMA_CLIFF_EDGE_SOUTH
                        | DIORAMA_CLIFF_EDGE_WEST;
    high->cliffTransitionMask = DIORAMA_CLIFF_EDGE_EAST;
    high->cliffCornerMask = DIORAMA_CLIFF_CORNER_NORTH_WEST
                          | DIORAMA_CLIFF_CORNER_SOUTH_WEST;
    low->cliffEdgeMask = DIORAMA_CLIFF_EDGE_NORTH | DIORAMA_CLIFF_EDGE_EAST
                       | DIORAMA_CLIFF_EDGE_SOUTH;
    low->cliffBaseMask = low->cliffEdgeMask;
    low->cliffCornerMask = DIORAMA_CLIFF_CORNER_NORTH_EAST
                         | DIORAMA_CLIFF_CORNER_SOUTH_EAST;
    assert(DioramaTerrain_BuildChunk(&input, sVertices,
                                     DIORAMA_TERRAIN_MAX_VERTICES, &mesh));
    assert(mesh.cliffBaseFaceCount >= 6);
    assert(mesh.cliffCornerCount == 4);
    assert(mesh.cliffTransitionFaceCount == 1);
    assert(mesh.bounds.maxY == 2.0f);
}

static void TestMountainArtworkMask(void)
{
    struct DioramaTerrainChunkInput input;
    struct DioramaTerrainMesh mesh;
    struct DioramaTerrainCell *center;
    uint32_t flatSpans = DIORAMA_TERRAIN_INPUT_SIZE * DIORAMA_TERRAIN_INPUT_SIZE
                       * DIORAMA_VOXELS_PER_CELL * DIORAMA_VOXELS_PER_CELL;

    InitInput(&input, 7, MB_NORMAL);
    center = &input.cells[4 * DIORAMA_TERRAIN_INPUT_SIZE + 4];
    center->behavior = MB_MOUNTAIN_TOP;
    center->shape = DIORAMA_SHAPE_CLIFF;
    center->archetype = DIORAMA_ARCHETYPE_CLIFF;
    center->featureHeight = 1.0f;
    center->visualHeight = 1.0f;
    center->measuredFlags = DIORAMA_MEASURED_MOUNTAIN_ART;
    for (int row = 0; row < DIORAMA_VOXELS_PER_CELL; row++)
    {
        center->foregroundAlpha[row] = row < 2 || row > 13 ? 0x0FF0 : 0x3FFC;
        for (int x = 0; x < DIORAMA_VOXELS_PER_CELL; x++)
            if (center->foregroundAlpha[row] & (1u << x))
                center->mountainHeight[row * DIORAMA_VOXELS_PER_CELL + x] =
                    row < 4 || row > 11 || x < 4 || x > 11 ? 4 : 16;
    }

    assert(DioramaTerrain_BuildChunk(&input, sVertices,
                                     DIORAMA_TERRAIN_MAX_VERTICES, &mesh));
    assert(!mesh.usedCompressedOccupancy);
    assert(mesh.bounds.maxY == 1.0f);
    /* Every source pixel keeps a column: masked pixels rise and the rest retain base. */
    assert(mesh.occupancySpanCount == flatSpans);
    assert(mesh.sideFaceCount > 4);
    assert(mesh.topFaceCount > 64);
    bool hasIntermediateHeight = false;
    for (uint32_t vertex = 0; vertex < mesh.vertexCount; vertex++)
        hasIntermediateHeight |= sVertices[vertex].y > 0.0f && sVertices[vertex].y < 1.0f;
    assert(hasIntermediateHeight);
}

static void TestTerraceArtworkCourse(void)
{
    struct DioramaTerrainChunkInput input;
    struct DioramaTerrainMesh mesh;
    struct DioramaTerrainCell *center;
    uint16_t artwork[DIORAMA_VOXELS_PER_CELL];
    uint16_t rows[DIORAMA_VOXELS_PER_CELL];

    InitInput(&input, 7, MB_NORMAL);
    for (int cell = 0;
         cell < DIORAMA_TERRAIN_INPUT_SIZE * DIORAMA_TERRAIN_INPUT_SIZE; cell++)
    {
        input.cells[cell].groundHeight = -1.0f;
        input.cells[cell].visualHeight = -1.0f;
    }
    center = &input.cells[4 * DIORAMA_TERRAIN_INPUT_SIZE + 4];
    center->shape = DIORAMA_SHAPE_CLIFF;
    center->archetype = DIORAMA_ARCHETYPE_CLIFF;
    center->featureHeight = 1.0f;
    center->visualHeight = 0.0f;
    center->measuredFlags = DIORAMA_MEASURED_MOUNTAIN_ART
                          | DIORAMA_MEASURED_TERRACE_COURSE
                          | DIORAMA_MEASURED_TERRACE_TOPOLOGY;
    center->terraceProfile = DIORAMA_TERRACE_VERTICAL;
    for (int row = 0; row < DIORAMA_VOXELS_PER_CELL; row++)
        artwork[row] = (uint16_t)(UINT16_MAX << 2);
    DioramaTerrain_BuildTerraceHeightmap(center->terraceProfile, artwork, true,
                                         rows, center->mountainHeight);

    assert(DioramaTerrain_BuildChunk(&input, sVertices,
                                     DIORAMA_TERRAIN_MAX_VERTICES, &mesh));
    assert(mesh.bounds.minY == -17.0f / 16.0f);
    assert(mesh.bounds.maxY == 0.0f);
    for (uint32_t vertex = 0; vertex < mesh.vertexCount; vertex += 6)
    {
        const struct DioramaTerrainVertex *a = &sVertices[vertex];
        const struct DioramaTerrainVertex *b = &sVertices[vertex + 1];
        const struct DioramaTerrainVertex *c = &sVertices[vertex + 2];
        float normalY = (b->z - a->z) * (c->x - a->x)
                      - (b->x - a->x) * (c->z - a->z);

        if (a->y == b->y && a->y == c->y && normalY < 0.0f)
            assert(a->y == -17.0f / 16.0f);
    }
}

static void TestTerraceProfileUsesOriginalArtwork(void)
{
    uint16_t artwork[DIORAMA_VOXELS_PER_CELL];
    uint16_t rows[DIORAMA_VOXELS_PER_CELL];
    uint16_t horizontalRows[DIORAMA_VOXELS_PER_CELL];
    uint8_t heights[DIORAMA_VOXELS_PER_CELL * DIORAMA_VOXELS_PER_CELL];
    uint8_t horizontal[DIORAMA_VOXELS_PER_CELL * DIORAMA_VOXELS_PER_CELL];

    for (int row = 0; row < DIORAMA_VOXELS_PER_CELL; row++)
        artwork[row] = UINT16_MAX << (2 + row / 6);
    DioramaTerrain_BuildTerraceHeightmap(DIORAMA_TERRACE_INNER, artwork, true,
                                         rows, heights);
    DioramaTerrain_BuildTerraceHeightmap(DIORAMA_TERRACE_HORIZONTAL, artwork, false,
                                         horizontalRows, horizontal);
    for (int row = 0; row < DIORAMA_VOXELS_PER_CELL; row++)
    {
        int start = row == 0 || row == DIORAMA_VOXELS_PER_CELL - 1
                  ? 2 : 2 + row / 6;

        assert(rows[row] == (row == DIORAMA_VOXELS_PER_CELL - 1
            ? 0 : (uint16_t)(UINT16_MAX << start)));
        for (int x = 0; x < DIORAMA_VOXELS_PER_CELL; x++)
            assert((heights[row * DIORAMA_VOXELS_PER_CELL + x] != 0)
                == (x >= start && row != DIORAMA_VOXELS_PER_CELL - 1));

        assert((heights[row * DIORAMA_VOXELS_PER_CELL + start] > 0)
            == (row != DIORAMA_VOXELS_PER_CELL - 1));
        assert(heights[row * DIORAMA_VOXELS_PER_CELL + start]
             < DIORAMA_VOXELS_PER_CELL);
        assert(heights[row * DIORAMA_VOXELS_PER_CELL + start + 5]
            == horizontal[row * DIORAMA_VOXELS_PER_CELL]);
    }
}

static void TestTerraceProfilesShareExactEdges(void)
{
    uint16_t artwork[DIORAMA_VOXELS_PER_CELL];
    uint16_t rows[4][DIORAMA_VOXELS_PER_CELL];
    uint8_t heights[4][DIORAMA_VOXELS_PER_CELL * DIORAMA_VOXELS_PER_CELL];

    for (int row = 0; row < DIORAMA_VOXELS_PER_CELL; row++)
        artwork[row] = (uint16_t)(UINT16_MAX << 2);
    DioramaTerrain_BuildTerraceHeightmap(DIORAMA_TERRACE_HORIZONTAL,
                                         artwork, false, rows[0], heights[0]);
    DioramaTerrain_BuildTerraceHeightmap(DIORAMA_TERRACE_VERTICAL,
                                         artwork, true, rows[1], heights[1]);
    DioramaTerrain_BuildTerraceHeightmap(DIORAMA_TERRACE_INNER,
                                         artwork, true, rows[2], heights[2]);
    DioramaTerrain_BuildTerraceHeightmap(DIORAMA_TERRACE_OUTER,
                                         artwork, false, rows[3], heights[3]);
    for (int coordinate = 0; coordinate < DIORAMA_VOXELS_PER_CELL; coordinate++)
    {
        assert(heights[2][coordinate]
            == heights[1][(DIORAMA_VOXELS_PER_CELL - 1)
                         * DIORAMA_VOXELS_PER_CELL + coordinate]);
        assert(heights[2][coordinate * DIORAMA_VOXELS_PER_CELL
                        + DIORAMA_VOXELS_PER_CELL - 1]
            == heights[0][coordinate * DIORAMA_VOXELS_PER_CELL]);
        assert(heights[3][coordinate * DIORAMA_VOXELS_PER_CELL]
            == heights[0][coordinate * DIORAMA_VOXELS_PER_CELL
                        + DIORAMA_VOXELS_PER_CELL - 1]);
        assert(heights[3][(DIORAMA_VOXELS_PER_CELL - 1)
                        * DIORAMA_VOXELS_PER_CELL + coordinate]
            == heights[1][coordinate]);
        assert(heights[0][coordinate] == DIORAMA_VOXELS_PER_CELL);
        assert(heights[0][(DIORAMA_VOXELS_PER_CELL - 1)
                         * DIORAMA_VOXELS_PER_CELL + coordinate] == 0);
        assert(heights[1][coordinate * DIORAMA_VOXELS_PER_CELL] == 0);
        assert(heights[1][coordinate * DIORAMA_VOXELS_PER_CELL
                         + DIORAMA_VOXELS_PER_CELL - 1]
            == DIORAMA_VOXELS_PER_CELL);
        assert(heights[2][(DIORAMA_VOXELS_PER_CELL - 1)
                         * DIORAMA_VOXELS_PER_CELL + coordinate] == 0);
        assert(heights[2][coordinate * DIORAMA_VOXELS_PER_CELL] == 0);
        assert(heights[3][coordinate] == DIORAMA_VOXELS_PER_CELL);
        assert(heights[3][coordinate * DIORAMA_VOXELS_PER_CELL
                         + DIORAMA_VOXELS_PER_CELL - 1]
            == DIORAMA_VOXELS_PER_CELL);
    }
    assert(heights[2][7 * DIORAMA_VOXELS_PER_CELL + 4] == 4);
    assert(heights[3][7 * DIORAMA_VOXELS_PER_CELL + 4] == 12);
}

static void TestSignaturesAndHashes(void)
{
    struct DioramaTerrainChunkInput input;
    struct DioramaTerrainMesh first;
    struct DioramaTerrainMesh second;
    uint64_t signature;

    InitInput(&input, 3, MB_NORMAL);
    signature = DioramaTerrain_ChunkSignature(&input);
    assert(DioramaTerrain_BuildChunk(&input, sVertices, DIORAMA_TERRAIN_MAX_VERTICES, &first));
    assert(DioramaTerrain_BuildChunk(&input, sVertices, DIORAMA_TERRAIN_MAX_VERTICES, &second));
    assert(first.geometryHash == second.geometryHash);
    input.cells[0].collision = 2;
    assert(DioramaTerrain_ChunkSignature(&input) == signature);
    input.cells[0].rawElevation = 4;
    assert(DioramaTerrain_ChunkSignature(&input) == signature);
    input.cells[0].shape = DIORAMA_SHAPE_EXTRUDED;
    assert(DioramaTerrain_ChunkSignature(&input) != signature);
    input.cells[0].shape = DIORAMA_SHAPE_FLAT;
    input.cells[0].foregroundAlpha[15] = 1;
    assert(DioramaTerrain_ChunkSignature(&input) != signature);
}

static void TestDirtyChunkCoverage(void)
{
    assert(DioramaTerrain_DirtyCellAffectsChunk(3, 3, 0, 0));
    assert(!DioramaTerrain_DirtyCellAffectsChunk(3, 3, 1, 0));
    assert(DioramaTerrain_DirtyCellAffectsChunk(7, 3, 0, 0));
    assert(DioramaTerrain_DirtyCellAffectsChunk(7, 3, 1, 0));
    assert(DioramaTerrain_DirtyCellAffectsChunk(7, 7, 0, 0));
    assert(DioramaTerrain_DirtyCellAffectsChunk(7, 7, 1, 0));
    assert(DioramaTerrain_DirtyCellAffectsChunk(7, 7, 0, 1));
    assert(DioramaTerrain_DirtyCellAffectsChunk(7, 7, 1, 1));
    assert(DioramaTerrain_DirtyCellAffectsChunk(-1, -1, 0, 0));
    assert(DioramaTerrain_DirtyCellAffectsChunk(-1, -1, -1, -1));

    assert(!DioramaTerrain_ShouldInvalidateAll(10, 11, 4, 5, 1, false));
    assert(DioramaTerrain_ShouldInvalidateAll(10, 12, 4, 5, 1, false));
    assert(DioramaTerrain_ShouldInvalidateAll(10, 11, 4, 5, 0, false));
    assert(DioramaTerrain_ShouldInvalidateAll(10, 11, 5, 5, 0, true));
    assert(!DioramaTerrain_ShouldInvalidateAll(10, 12, 5, 5, 0, false));
}

static void TestHiddenShape(void)
{
    struct DioramaTerrainChunkInput input;
    struct DioramaTerrainMesh mesh;
    struct DioramaTerrainCell *center;

    InitInput(&input, 3, MB_NORMAL);
    center = &input.cells[4 * DIORAMA_TERRAIN_INPUT_SIZE + 4];
    center->shape = DIORAMA_SHAPE_HIDDEN;
    assert(DioramaTerrain_BuildChunk(&input, sVertices, DIORAMA_TERRAIN_MAX_VERTICES, &mesh));
    assert(mesh.topFaceCount == 63);
    assert(mesh.bottomFaceCount == 63);
    assert(mesh.sideFaceCount == 4);
    assert(mesh.featureFaceCount == 0);
}

static void TestSpanShellIntervalsAndOwnership(void)
{
    struct DioramaOccupancySpan spans[2] = {
        {.x = 127, .z = 0, .yMin = 0, .yMax = 4, .sourceCellX = 7},
        {.x = 128, .z = 0, .yMin = 2, .yMax = 6, .sourceCellX = 8},
    };
    struct DioramaShellFace faces[16];
    uint32_t spanCount = 2;
    uint32_t faceCount;
    bool foundLower = false;

    for (int face = 0; face < DIORAMA_OCCUPANCY_FACE_COUNT; face++)
    {
        spans[0].material[face] = face + 1;
        spans[1].material[face] = face + 1;
    }
    assert(DioramaOccupancy_Canonicalize(spans, &spanCount));
    assert(DioramaOccupancy_BuildShell(spans, spanCount, 0, 0, 128, 128,
                                       faces, 16, &faceCount));
    for (uint32_t i = 0; i < faceCount; i++)
        if (faces[i].axis == 0 && faces[i].sign == 1
         && faces[i].plane == 128 && faces[i].vMin == 0 && faces[i].vMax == 2)
            foundLower = true;
    assert(foundLower);
    assert(DioramaOccupancy_BuildShell(spans, spanCount, 128, 0, 256, 128,
                                       faces, 16, &faceCount));
    for (uint32_t i = 0; i < faceCount; i++)
        assert(faces[i].sourceCellX == 8);
}

static void TestChunkSeamAndHaloInvalidation(void)
{
    struct DioramaTerrainChunkInput west;
    struct DioramaTerrainChunkInput east;
    struct DioramaTerrainMesh westMesh;
    struct DioramaTerrainMesh eastMesh;
    uint64_t westSignature;
    uint64_t eastSignature;
    int y;

    InitInput(&west, 4, MB_NORMAL);
    InitInput(&east, 3, MB_NORMAL);
    east.chunkX = 1;
    for (y = 0; y < DIORAMA_TERRAIN_INPUT_SIZE; y++)
    {
        int x;

        for (x = 0; x < DIORAMA_TERRAIN_INPUT_SIZE; x++)
        {
            east.cells[y * DIORAMA_TERRAIN_INPUT_SIZE + x].mapX = 7 + x;
            east.cells[y * DIORAMA_TERRAIN_INPUT_SIZE + x].mapY = y - 1;
        }
        west.cells[y * DIORAMA_TERRAIN_INPUT_SIZE + 9].rawElevation = 3;
        east.cells[y * DIORAMA_TERRAIN_INPUT_SIZE].rawElevation = 4;
        for (x = 0; x < DIORAMA_TERRAIN_INPUT_SIZE; x++)
            west.cells[y * DIORAMA_TERRAIN_INPUT_SIZE + x].visualHeight = 0.75f;
        west.cells[y * DIORAMA_TERRAIN_INPUT_SIZE + 9].visualHeight = 0.0f;
    }

    assert(DioramaTerrain_BuildChunk(&west, sVertices, DIORAMA_TERRAIN_MAX_VERTICES, &westMesh));
    assert(westMesh.sideFaceCount == 8);
    assert(westMesh.bounds.maxX == 7.5f);
    assert(DioramaTerrain_BuildChunk(&east, sVertices, DIORAMA_TERRAIN_MAX_VERTICES, &eastMesh));
    assert(eastMesh.sideFaceCount == 0);
    assert(eastMesh.bounds.minX == 7.5f);

    westSignature = DioramaTerrain_ChunkSignature(&west);
    eastSignature = DioramaTerrain_ChunkSignature(&east);
    west.cells[4 * DIORAMA_TERRAIN_INPUT_SIZE + 9].metatileId++;
    east.cells[4 * DIORAMA_TERRAIN_INPUT_SIZE].metatileId++;
    assert(DioramaTerrain_ChunkSignature(&west) != westSignature);
    assert(DioramaTerrain_ChunkSignature(&east) != eastSignature);
}

static void TestFrustum(void)
{
    const struct DioramaTerrainBounds visible = {-4.0f, 0.0f, -4.0f, 4.0f, 1.0f, 4.0f};
    const struct DioramaTerrainBounds behind = {-4.0f, 0.0f, -80.0f, 4.0f, 1.0f, -70.0f};
    const struct DioramaTerrainBounds side = {100.0f, 0.0f, -4.0f, 108.0f, 1.0f, 4.0f};
    const struct DioramaTerrainBounds zoomEdge = {20.0f, 0.0f, -1.0f, 22.0f, 1.0f, 1.0f};
    const struct DioramaTerrainBounds heightEdge = {-1.0f, 0.0f, -10.25f, 1.0f, 0.0f, -10.0f};

    assert(DioramaTerrain_IsBoundsVisible(&visible, 0.0f, 0.0f, 0.70758444f, 130.0f));
    assert(!DioramaTerrain_IsBoundsVisible(&behind, 0.0f, 0.0f, 0.70758444f, 130.0f));
    assert(!DioramaTerrain_IsBoundsVisible(&side, 0.0f, 0.0f, 0.70758444f, 130.0f));
    assert(DioramaTerrain_IsBoundsVisible(&zoomEdge, 0.0f, 0.0f, 0.70758444f, 80.0f));
    assert(!DioramaTerrain_IsBoundsVisible(&zoomEdge, 0.0f, 0.0f, 0.70758444f, 200.0f));
    assert(DioramaTerrain_IsBoundsVisible(&heightEdge, 0.0f, 0.0f, 0.34906585f, 130.0f));
    assert(!DioramaTerrain_IsBoundsVisible(&heightEdge, 0.0f, 0.0f, 1.22173048f, 130.0f));
}

static void TestPixelPrismProvenanceAndSupport(void)
{
    struct DioramaTerrainChunkInput input;
    struct DioramaTerrainMesh first;
    struct DioramaTerrainMesh second;

    memset(&input, 0, sizeof(input));
    input.rulesGeneration = 7;
    input.pixelCount = 1;
    input.pixels[0].xQ32 = 16;
    input.pixels[0].yQ32 = 24;
    input.pixels[0].zQ32 = 16;
    input.pixels[0].sizeXQ32 = 2;
    input.pixels[0].sizeYQ32 = 2;
    input.pixels[0].sizeZQ32 = 2;
    input.pixels[0].rgba = UINT32_C(0xFF332211);
    input.pixels[0].sourceCellOffset = 12;
    input.pixels[0].objectId = 3;
    input.pixels[0].structureId = 9;
    input.pixels[0].expectedMetatile = 2;
    input.pixels[0].expectedTileEntry = 0x412;
    assert(DioramaTerrain_BuildChunk(&input, sVertices,
                                     DIORAMA_TERRAIN_MAX_VERTICES, &first));
    assert(first.vertexCount == 36);
    assert(first.featureFaceCount == 6);
    assert(first.bounds.minY == 0.75f);
    assert(first.bounds.maxY == 0.8125f);
    assert(sVertices[0].textureLayer == DIORAMA_TERRAIN_TEXTURE_VERTEX_COLOR);
    assert(sVertices[0].color == UINT32_C(0xFF332211));
    assert(sVertices[12].shade == 1.0f && sVertices[24].shade == 1.0f);
    input.pixels[0].rgba ^= 1;
    assert(DioramaTerrain_BuildChunk(&input, sVertices,
                                     DIORAMA_TERRAIN_MAX_VERTICES, &second));
    assert(first.geometryHash != second.geometryHash);
    assert(!DioramaTerrain_BuildChunk(&input, sVertices, 35, &second));
}

static void TestMeasuredVolumeBandsAndRoof(void)
{
    struct DioramaTerrainChunkInput input;
    struct DioramaTerrainMesh first;
    struct DioramaTerrainMesh second;
    struct DioramaTerrainCell *center;
    uint32_t fullOccupancy;

    InitInput(&input, 3, MB_NORMAL);
    center = &input.cells[4 * DIORAMA_TERRAIN_INPUT_SIZE + 4];
    center->shape = DIORAMA_SHAPE_EXTRUDED;
    center->visualHeight = 1.0f;
    center->featureHeight = 1.0f;
    center->measuredBandCount = 2;
    center->measuredPeriodBands = 2;
    center->measuredBands[0] = center->materials[DIORAMA_MATERIAL_FACE_SOUTH];
    center->measuredBands[1] = center->materials[DIORAMA_MATERIAL_FACE_SOUTH];
    center->measuredBands[0].v1 = 0.3f;
    center->measuredBands[1].v0 = 0.3f;
    assert(DioramaTerrain_BuildChunk(&input, sVertices,
                                     DIORAMA_TERRAIN_MAX_VERTICES, &first));
    assert(first.bounds.maxY == 1.0f);
    center->measuredBands[1].u0 += 0.01f;
    assert(DioramaTerrain_BuildChunk(&input, sVertices,
                                     DIORAMA_TERRAIN_MAX_VERTICES, &second));
    assert(first.geometryHash != second.geometryHash);
    fullOccupancy = second.occupancySpanCount;

    center->measuredFlags = DIORAMA_MEASURED_SILHOUETTE;
    for (int row = 0; row < DIORAMA_VOXELS_PER_CELL; row++)
        center->foregroundAlpha[row] = 0x0FF0;
    assert(DioramaTerrain_BuildChunk(&input, sVertices,
                                     DIORAMA_TERRAIN_MAX_VERTICES, &second));
    assert(!second.usedCompressedOccupancy);
    assert(second.occupancySpanCount < fullOccupancy);

    center->shape = DIORAMA_SHAPE_ROOF;
    center->measuredFlags = 0;
    center->structureId = 1;
    center->profile = DIORAMA_ROOF_GABLE_Z;
    center->measuredAxis = DIORAMA_PLANE_AXIS_Z;
    center->measuredRunLength = 1;
    center->measuredRunLocal = 0;
    center->structureBodyHeight = 0.5f;
    center->structureRoofHeight = 0.5f;
    assert(DioramaTerrain_BuildChunk(&input, sVertices,
                                     DIORAMA_TERRAIN_MAX_VERTICES, &second));
    assert(!second.usedCompressedOccupancy);
    assert(second.bounds.maxY > 0.5f && second.bounds.maxY <= 1.0f);
}

static void SetTreeMaterial(struct DioramaTerrainCell *cell, float u0, float v0,
                            float u1, float v1)
{
    cell->treeMaterial = cell->materials[DIORAMA_MATERIAL_FACE_TOP];
    cell->treeMaterial.layer = DIORAMA_MATERIAL_FULL;
    cell->treeHullReady = true;
    cell->treeMaterial.u0 = u0;
    cell->treeMaterial.v0 = v0;
    cell->treeMaterial.u1 = u1;
    cell->treeMaterial.v1 = v1;
}

static void SetTreeShade(struct DioramaTerrainCell *cell, int x, int y, uint8_t shade)
{
    cell->treeShade[y * DIORAMA_VOXELS_PER_CELL + x] = shade;
}

static void SetGroupedTreeShade(struct DioramaTerrainChunkInput *input,
                                int gridX, int gridY, int x, int y, uint8_t shade)
{
    struct DioramaTerrainCell *cell = &input->cells[
        (gridY + y / DIORAMA_VOXELS_PER_CELL) * DIORAMA_TERRAIN_INPUT_SIZE
        + gridX + x / DIORAMA_VOXELS_PER_CELL];

    SetTreeShade(cell, x % DIORAMA_VOXELS_PER_CELL,
                 y % DIORAMA_VOXELS_PER_CELL, shade);
}

static void TestRoundHullUsesArtworkChords(void)
{
    struct DioramaTerrainChunkInput input;
    struct DioramaTerrainMesh box;
    struct DioramaTerrainMesh round;
    struct DioramaTerrainCell *center;
    static const uint8_t left[11] = {7, 5, 4, 3, 3, 3, 3, 4, 5, 7, 7};
    static const uint8_t right[11] = {8, 10, 11, 12, 12, 12, 12, 11, 10, 8, 8};

    InitInput(&input, 3, MB_NORMAL);
    center = &input.cells[4 * DIORAMA_TERRAIN_INPUT_SIZE + 4];
    center->shape = DIORAMA_SHAPE_EXTRUDED;
    center->visualHeight = 1.0f;
    center->featureHeight = 1.0f;
    assert(DioramaTerrain_BuildChunk(&input, sVertices,
                                      DIORAMA_TERRAIN_MAX_VERTICES, &box));
    center->archetype = DIORAMA_ARCHETYPE_ROUND_HULL;
    SetTreeMaterial(center, 0.1f, 0.2f, 0.3f, 0.4f);
    for (int row = 0; row < 11; row++)
        for (int x = left[row]; x <= right[row]; x++)
            SetTreeShade(center, x, row + 2,
                         (x + row) & 1 ? DIORAMA_TREE_SHADE_BLACK
                                       : DIORAMA_TREE_SHADE_DARK);
    assert(DioramaTerrain_BuildChunk(&input, sVertices,
                                      DIORAMA_TERRAIN_MAX_VERTICES, &round));
    assert(!round.usedCompressedOccupancy);
    assert(round.geometryHash != box.geometryHash);
    assert(round.bounds.maxY == 14.0f / 16.0f);
    assert(round.bounds.minY == -1.0f / 16.0f);
    assert(round.sideFaceCount > box.sideFaceCount);
    for (uint32_t vertex = 0; vertex < round.vertexCount; vertex++)
        if (sVertices[vertex].y > 0.0f)
            assert(sVertices[vertex].textureLayer == DIORAMA_TERRAIN_TEXTURE_FULL);
}

static void TestTreeFloodRemovesGrassAndDrawnShadow(void)
{
    struct DioramaTerrainChunkInput input;
    struct DioramaTerrainMesh withoutShadow;
    struct DioramaTerrainMesh withShadow;
    struct DioramaTerrainCell *center;

    InitInput(&input, 3, MB_NORMAL);
    center = &input.cells[4 * DIORAMA_TERRAIN_INPUT_SIZE + 4];
    center->shape = DIORAMA_SHAPE_EXTRUDED;
    center->archetype = DIORAMA_ARCHETYPE_ROUND_HULL;
    center->visualHeight = 1.0f;
    center->featureHeight = 1.0f;
    SetTreeMaterial(center, 0.1f, 0.2f, 0.3f, 0.4f);
    memset(center->treeShade, DIORAMA_TREE_SHADE_LIGHT, sizeof(center->treeShade));
    for (int y = 3; y <= 10; y++)
        for (int x = 4; x <= 11; x++)
            SetTreeShade(center, x, y,
                x == 4 || x == 11 || y == 3 || y == 10
                    ? DIORAMA_TREE_SHADE_BLACK : DIORAMA_TREE_SHADE_DARK);
    assert(DioramaTerrain_BuildChunk(&input, sVertices,
                                      DIORAMA_TERRAIN_MAX_VERTICES, &withoutShadow));
    for (int x = 4; x <= 11; x++)
        SetTreeShade(center, x, 13, DIORAMA_TREE_SHADE_DARK);
    assert(DioramaTerrain_BuildChunk(&input, sVertices,
                                      DIORAMA_TERRAIN_MAX_VERTICES, &withShadow));
    assert(withoutShadow.geometryHash == withShadow.geometryHash);
    assert(withShadow.bounds.maxY == 13.0f / 16.0f);
}

static void TestLargeGroupedTreeUsesAuthoredVoxelModel(void)
{
    struct DioramaTerrainChunkInput input;
    struct DioramaTerrainMesh mesh;
    bool sawBase = false;
    bool sawShadowedGrass = false;

    InitInput(&input, 3, MB_NORMAL);
    for (int localY = 0; localY < 3; localY++)
    {
        for (int localX = 0; localX < 2; localX++)
        {
            struct DioramaTerrainCell *cell =
                &input.cells[(3 + localY) * DIORAMA_TERRAIN_INPUT_SIZE + 3 + localX];

            cell->shape = DIORAMA_SHAPE_EXTRUDED;
            cell->archetype = DIORAMA_ARCHETYPE_GROUPED_HULL;
            cell->visualHeight = 2.0f;
            cell->featureHeight = 2.0f;
            cell->structureId = 9;
            cell->structureX = 2;
            cell->structureY = 2;
            cell->structureWidth = 2;
            cell->structureHeight = 3;
            cell->structureLocalX = localX;
            cell->structureLocalY = localY;
            cell->treeHullReady = true;
            for (int face = 0; face < DIORAMA_MATERIAL_FACE_COUNT; face++)
                cell->materials[face].layer = DIORAMA_MATERIAL_FOREGROUND;
            if (localY > 0)
                SetTreeMaterial(cell, 0.1f, 0.1f, 0.2f, 0.2f);
        }
    }
    assert(DioramaTerrain_BuildChunk(&input, sVertices,
                                       DIORAMA_TERRAIN_MAX_VERTICES, &mesh));
    assert(!mesh.usedCompressedOccupancy);
    assert(DIORAMA_TREE_MODEL_SIZE_X == 32);
    assert(DIORAMA_TREE_MODEL_SIZE_Z == 24);
    assert(DIORAMA_TREE_MODEL_SIZE_Y == 48);
    assert(gDioramaTreeModelVoxelCount == 6782);
    assert(gDioramaTreeModelFaceCount < 3182);
    assert(mesh.bounds.maxY
        == (DIORAMA_TREE_MODEL_MAX_Y - DIORAMA_TREE_MODEL_MIN_Y) / 16.0f);
    assert(mesh.bounds.minY == -1.0f / 16.0f);
    assert(mesh.treeInstanceCount == 1);
    assert(mesh.treeInstances[0].x == 2.0f);
    assert(mesh.treeInstances[0].y == 0.0f);
    assert(mesh.treeInstances[0].z == -3.0f);
    assert(mesh.vertexCount < 4000);
    for (uint32_t vertex = 0; vertex < mesh.vertexCount; vertex++)
    {
        sawBase |= sVertices[vertex].textureLayer == DIORAMA_TERRAIN_TEXTURE_BASE;
        sawShadowedGrass |= sVertices[vertex].textureLayer == DIORAMA_TERRAIN_TEXTURE_BASE
                         && sVertices[vertex].shade == 0.78f;
        assert(sVertices[vertex].textureLayer != DIORAMA_TERRAIN_TEXTURE_VERTEX_COLOR);
    }
    assert(sawBase && sawShadowedGrass);
}

static void TestAuthoredTreeModelBuildsOnceInLocalSpace(void)
{
    struct DioramaTerrainBounds bounds;
    uint32_t vertexCount;

    assert(DioramaTerrain_BuildTreeModel(sVertices, DIORAMA_TERRAIN_MAX_VERTICES,
                                         &vertexCount, &bounds));
    assert(gDioramaTreeModelFaceCount == 2424);
    assert(vertexCount == gDioramaTreeModelFaceCount * 6);
    assert(bounds.minX == -0.5f && bounds.maxX == 1.5f);
    assert(bounds.minY == 0.0f && bounds.maxY == 2.125f);
    assert(bounds.minZ == -1.25f && bounds.maxZ == 0.1875f);
    for (uint32_t vertex = 0; vertex < vertexCount; vertex++)
        assert(sVertices[vertex].textureLayer == DIORAMA_TERRAIN_TEXTURE_VERTEX_COLOR);
}

static void TestIncompleteLargeTreeFallsBackToFlatGround(void)
{
    struct DioramaTerrainChunkInput input;
    struct DioramaTerrainMesh mesh;
    struct DioramaTerrainCell *center;

    InitInput(&input, 3, MB_NORMAL);
    center = &input.cells[4 * DIORAMA_TERRAIN_INPUT_SIZE + 4];
    center->shape = DIORAMA_SHAPE_EXTRUDED;
    center->archetype = DIORAMA_ARCHETYPE_GROUPED_HULL;
    center->visualHeight = 2.0f;
    center->featureHeight = 2.0f;
    center->structureId = 9;
    center->structureWidth = 2;
    center->structureHeight = 2;
    center->treeHullReady = false;
    assert(DioramaTerrain_BuildChunk(&input, sVertices,
                                      DIORAMA_TERRAIN_MAX_VERTICES, &mesh));
    assert(mesh.bounds.maxY == 0.0f);
    for (uint32_t vertex = 0; vertex < mesh.vertexCount; vertex++)
        assert(sVertices[vertex].textureLayer != DIORAMA_TERRAIN_TEXTURE_VERTEX_COLOR);
}

static void TestWideGroupedHullCentersItsDepth(void)
{
    struct DioramaTerrainChunkInput input;
    struct DioramaTerrainMesh mesh;

    InitInput(&input, 3, MB_NORMAL);
    for (int localX = 0; localX < 2; localX++)
    {
        struct DioramaTerrainCell *cell =
            &input.cells[3 * DIORAMA_TERRAIN_INPUT_SIZE + 3 + localX];

        cell->shape = DIORAMA_SHAPE_EXTRUDED;
        cell->archetype = DIORAMA_ARCHETYPE_GROUPED_HULL;
        cell->visualHeight = 1.0f;
        cell->featureHeight = 1.0f;
        cell->structureId = 10;
        cell->structureWidth = 2;
        cell->structureHeight = 1;
        cell->structureLocalX = localX;
        cell->structureLocalY = 0;
        SetTreeMaterial(cell, 0.1f + localX * 0.3f, 0.1f,
                        0.3f + localX * 0.3f, 0.3f);
    }
    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 32; x++)
            SetGroupedTreeShade(&input, 3, 3, x, y, DIORAMA_TREE_SHADE_BLACK);
    assert(DioramaTerrain_BuildChunk(&input, sVertices,
                                      DIORAMA_TERRAIN_MAX_VERTICES, &mesh));
    assert(mesh.treeInstanceCount == 1);
    assert(mesh.treeInstances[0].x == 2.0f);
    assert(mesh.treeInstances[0].z == -1.0f);
    assert(mesh.bounds.maxY == 2.125f);
}

static void TestDenseRoundHullsStayWithinCapacity(void)
{
    struct DioramaTerrainChunkInput input;
    struct DioramaTerrainMesh mesh;

    InitInput(&input, 3, MB_NORMAL);
    for (int gridY = DIORAMA_TERRAIN_HALO;
         gridY < DIORAMA_TERRAIN_HALO + DIORAMA_TERRAIN_CHUNK_SIZE; gridY++)
        for (int gridX = DIORAMA_TERRAIN_HALO;
             gridX < DIORAMA_TERRAIN_HALO + DIORAMA_TERRAIN_CHUNK_SIZE; gridX++)
        {
            struct DioramaTerrainCell *cell =
                &input.cells[gridY * DIORAMA_TERRAIN_INPUT_SIZE + gridX];

            cell->shape = DIORAMA_SHAPE_EXTRUDED;
            cell->archetype = DIORAMA_ARCHETYPE_ROUND_HULL;
            cell->visualHeight = 1.0f;
            cell->featureHeight = 1.0f;
            SetTreeMaterial(cell, 0.1f, 0.1f, 0.3f, 0.3f);
            for (int y = 0; y < DIORAMA_VOXELS_PER_CELL; y++)
                for (int x = 0; x < DIORAMA_VOXELS_PER_CELL; x++)
                    SetTreeShade(cell, x, y,
                        (x + y) & 1 ? DIORAMA_TREE_SHADE_BLACK
                                    : DIORAMA_TREE_SHADE_DARK);
        }
    assert(DioramaTerrain_BuildChunk(&input, sVertices,
                                      DIORAMA_TERRAIN_MAX_VERTICES, &mesh));
    assert(mesh.vertexCount < DIORAMA_TERRAIN_MAX_VERTICES);
    assert(mesh.bounds.maxY == 1.0f);
}

static void TestDenseAuthoredTreesStayWithinCapacity(void)
{
    struct DioramaTerrainChunkInput input;
    struct DioramaTerrainMesh mesh;
    int structureId = 1;

    InitInput(&input, 3, MB_NORMAL);
    for (int mapY = 0; mapY < DIORAMA_TERRAIN_CHUNK_SIZE; mapY += 2)
        for (int mapX = 0; mapX < DIORAMA_TERRAIN_CHUNK_SIZE; mapX += 2)
        {
            for (int localY = 0; localY < 2; localY++)
                for (int localX = 0; localX < 2; localX++)
                {
                    struct DioramaTerrainCell *cell = &input.cells[
                        (mapY + localY + DIORAMA_TERRAIN_HALO) * DIORAMA_TERRAIN_INPUT_SIZE
                        + mapX + localX + DIORAMA_TERRAIN_HALO];

                    cell->shape = DIORAMA_SHAPE_EXTRUDED;
                    cell->archetype = DIORAMA_ARCHETYPE_GROUPED_HULL;
                    cell->structureId = structureId;
                    cell->structureX = mapX;
                    cell->structureY = mapY;
                    cell->structureWidth = 2;
                    cell->structureHeight = 2;
                    cell->structureLocalX = localX;
                    cell->structureLocalY = localY;
                    cell->treeHullReady = true;
                }
            structureId++;
        }
    assert(DioramaTerrain_BuildChunk(&input, sVertices,
                                      DIORAMA_TERRAIN_MAX_VERTICES, &mesh));
    assert(mesh.vertexCount < DIORAMA_TERRAIN_MAX_VERTICES);
    assert(mesh.treeInstanceCount == 16);
    assert(mesh.vertexCount < 10000);
    for (uint32_t vertex = 0; vertex < mesh.vertexCount; vertex++)
        assert(sVertices[vertex].textureLayer != DIORAMA_TERRAIN_TEXTURE_VERTEX_COLOR);
}

int main(void)
{
    TestFloorDiv();
    TestElevationNormalization();
    TestFlatChunk();
    TestMaterialRotation();
    TestProfiledGeometryUsesPixelFallback();
    TestGameplayPlanesStayFlat();
    TestReflectionMask();
    TestGameplayElevationDoesNotCreateVisualHeight();
    TestRaisedCellAndCapacity();
    TestPartialChunk();
    TestLedge();
    TestLedgeUsesForegroundRuns();
    TestDiagonalLedgeAndOcclusion();
    TestBridgeHasTwoSurfaces();
    TestStairsAndDescendingStairwell();
    TestCliffFaceBandsAndRamp();
    TestCliffBaseCornersAndHeightTransition();
    TestMountainArtworkMask();
    TestTerraceArtworkCourse();
    TestTerraceProfileUsesOriginalArtwork();
    TestTerraceProfilesShareExactEdges();
    TestSignaturesAndHashes();
    TestDirtyChunkCoverage();
    TestHiddenShape();
    TestSpanShellIntervalsAndOwnership();
    TestChunkSeamAndHaloInvalidation();
    TestPixelPrismProvenanceAndSupport();
    TestMeasuredVolumeBandsAndRoof();
    TestRoundHullUsesArtworkChords();
    TestTreeFloodRemovesGrassAndDrawnShadow();
    TestAuthoredTreeModelBuildsOnceInLocalSpace();
    TestLargeGroupedTreeUsesAuthoredVoxelModel();
    TestIncompleteLargeTreeFallsBackToFlatGround();
    TestWideGroupedHullCentersItsDepth();
    TestDenseRoundHullsStayWithinCapacity();
    TestDenseAuthoredTreesStayWithinCapacity();
    TestFrustum();
    puts("terrain mesh tests passed");
    return 0;
}
