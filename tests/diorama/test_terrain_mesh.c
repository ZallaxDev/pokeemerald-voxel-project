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
            cell->visualHeight = DioramaTerrain_NormalizeElevation(elevation, behavior);
            cell->u0 = 0.1f;
            cell->v0 = 0.2f;
            cell->u1 = 0.3f;
            cell->v1 = 0.4f;
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
    assert(DioramaTerrain_NormalizeElevation(0, MB_JUMP_SOUTH) == DIORAMA_TERRAIN_LEDGE_HEIGHT);
    assert(DioramaTerrain_NormalizeElevation(3, MB_POND_WATER) == DIORAMA_TERRAIN_WATER_HEIGHT);
    assert(DioramaTerrain_NormalizeElevation(3, MB_BRIDGE_OVER_POND_MED) == 1.75f);
    assert(DioramaTerrain_NormalizeElevation(3, MB_BRIDGE_OVER_POND_HIGH) == 2.75f);
}

static void TestFlatChunk(void)
{
    struct DioramaTerrainChunkInput input;
    struct DioramaTerrainMesh mesh;

    InitInput(&input, 3, MB_NORMAL);
    assert(DioramaTerrain_BuildChunk(&input, sVertices, DIORAMA_TERRAIN_MAX_VERTICES, &mesh));
    assert(mesh.topFaceCount == 64);
    assert(mesh.sideFaceCount == 0);
    assert(mesh.vertexCount == 64 * 6);
    assert(mesh.bounds.minX == -0.5f);
    assert(mesh.bounds.maxX == 7.5f);
    assert(mesh.bounds.minZ == -7.5f);
    assert(mesh.bounds.maxZ == 0.5f);
    assert(mesh.bounds.minY == 0.0f && mesh.bounds.maxY == 0.0f);
    assert(sVertices[0].x == -0.5f && sVertices[0].z == 0.5f);
    assert(sVertices[1].x == 0.5f && sVertices[1].z == 0.5f);
    assert(sVertices[2].x == 0.5f && sVertices[2].z == -0.5f);
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
    assert(mesh.bounds.minY == 0.0f && mesh.bounds.maxY == 0.0f);
}

static void TestDirectionalLedgeHeightField(void)
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
            float expected = x >= 1 && x <= 3 && y == 2
                           ? DIORAMA_TERRAIN_LEDGE_HEIGHT : 0.0f;
            assert(heights[y * 5 + x] == expected);
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
    center->visualHeight = 0.75f;
    assert(DioramaTerrain_BuildChunk(&input, sVertices, DIORAMA_TERRAIN_MAX_VERTICES, &mesh));
    assert(mesh.topFaceCount == 64);
    assert(mesh.sideFaceCount == 4);
    assert(mesh.vertexCount == 68 * 6);
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
    assert(mesh.sideFaceCount == 0);
    assert(mesh.bounds.minX == 3.5f);
    assert(mesh.bounds.maxX == 7.5f);
}

static void TestLedge(void)
{
    struct DioramaTerrainChunkInput input;
    struct DioramaTerrainMesh mesh;

    InitInput(&input, 3, MB_NORMAL);
    for (int y = 0; y < DIORAMA_TERRAIN_INPUT_SIZE; y++)
        for (int x = 0; x < DIORAMA_TERRAIN_INPUT_SIZE; x++)
            input.cells[y * DIORAMA_TERRAIN_INPUT_SIZE + x].visualHeight = 0.0f;
    for (int x = 0; x < DIORAMA_TERRAIN_INPUT_SIZE; x++)
        input.cells[4 * DIORAMA_TERRAIN_INPUT_SIZE + x].visualHeight = DIORAMA_TERRAIN_LEDGE_HEIGHT;
    assert(DioramaTerrain_BuildChunk(&input, sVertices, DIORAMA_TERRAIN_MAX_VERTICES, &mesh));
    assert(mesh.topFaceCount == 64);
    assert(mesh.sideFaceCount == 16);
    assert(mesh.bounds.minY == 0.0f);
    assert(mesh.bounds.maxY == DIORAMA_TERRAIN_LEDGE_HEIGHT);
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
    assert(DioramaTerrain_ChunkSignature(&input) != signature);
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

    assert(DioramaTerrain_IsBoundsVisible(&visible, 0.0f, 0.0f));
    assert(!DioramaTerrain_IsBoundsVisible(&behind, 0.0f, 0.0f));
    assert(!DioramaTerrain_IsBoundsVisible(&side, 0.0f, 0.0f));
}

int main(void)
{
    TestFloorDiv();
    TestElevationNormalization();
    TestFlatChunk();
    TestGameplayPlanesStayFlat();
    TestDirectionalLedgeHeightField();
    TestRaisedCellAndCapacity();
    TestPartialChunk();
    TestLedge();
    TestSignaturesAndHashes();
    TestChunkSeamAndHaloInvalidation();
    TestFrustum();
    puts("terrain mesh tests passed");
    return 0;
}
