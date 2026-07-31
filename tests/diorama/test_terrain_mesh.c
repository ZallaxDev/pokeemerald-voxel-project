#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "constants/metatile_behaviors.h"
#include "diorama/terrain_mesh.h"

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
    assert(DioramaTerrain_NormalizeElevation(3, MB_POND_WATER) == DIORAMA_TERRAIN_WATER_HEIGHT);
    assert(DioramaTerrain_NormalizeElevation(3, MB_BRIDGE_OVER_POND_MED) == 1.75f);
    assert(DioramaTerrain_NormalizeElevation(3, MB_BRIDGE_OVER_POND_HIGH) == 2.75f);
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
    bool sawUpperBand = false;

    InitInput(&input, 3, MB_NORMAL);
    center = &input.cells[4 * DIORAMA_TERRAIN_INPUT_SIZE + 4];
    center->shape = DIORAMA_SHAPE_CLIFF;
    center->archetype = DIORAMA_ARCHETYPE_CLIFF;
    center->featureHeight = 2.5f;
    center->visualHeight = 2.5f;
    assert(DioramaTerrain_BuildChunk(&input, sVertices, DIORAMA_TERRAIN_MAX_VERTICES, &mesh));
    assert(mesh.sideFaceCount == 12);
    assert(mesh.bounds.maxY == 2.5f);

    center->visualHeight = 3.0f;
    center->volumeMaterialCount = 3;
    for (int face = 0; face < 4; face++)
        for (int band = 0; band < 3; band++)
        {
            center->volumeMaterials[face][band] = center->materials[face + 1];
            center->volumeMaterials[face][band].u0 = 0.1f + band * 0.3f;
            center->volumeMaterials[face][band].u1 = 0.2f + band * 0.3f;
        }
    assert(DioramaTerrain_BuildChunk(&input, sVertices, DIORAMA_TERRAIN_MAX_VERTICES, &mesh));
    assert(mesh.sideFaceCount == 12);
    for (uint32_t vertex = 0; vertex < mesh.vertexCount; vertex++)
        sawUpperBand |= sVertices[vertex].u >= 0.7f;
    assert(sawUpperBand);

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

int main(void)
{
    TestFloorDiv();
    TestElevationNormalization();
    TestFlatChunk();
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
    TestSignaturesAndHashes();
    TestDirtyChunkCoverage();
    TestHiddenShape();
    TestSpanShellIntervalsAndOwnership();
    TestChunkSeamAndHaloInvalidation();
    TestFrustum();
    puts("terrain mesh tests passed");
    return 0;
}
