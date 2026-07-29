#ifdef ENABLE_DIORAMA

#include <float.h>
#include <string.h>

#include "constants/metatile_behaviors.h"
#include "diorama/terrain_mesh.h"

#define FNV_OFFSET UINT64_C(1469598103934665603)
#define FNV_PRIME UINT64_C(1099511628211)

static bool IsLedge(uint8_t behavior)
{
    return behavior >= MB_JUMP_EAST && behavior <= MB_JUMP_SOUTHWEST;
}

static bool IsWater(uint8_t behavior)
{
    switch (behavior)
    {
    case MB_POND_WATER:
    case MB_INTERIOR_DEEP_WATER:
    case MB_DEEP_WATER:
    case MB_WATERFALL:
    case MB_SOOTOPOLIS_DEEP_WATER:
    case MB_OCEAN_WATER:
    case MB_NO_SURFACING:
    case MB_SEAWEED:
    case MB_SEAWEED_NO_SURFACING:
    case MB_EASTWARD_CURRENT:
    case MB_WESTWARD_CURRENT:
    case MB_NORTHWARD_CURRENT:
    case MB_SOUTHWARD_CURRENT:
    case MB_WATER_DOOR:
    case MB_WATER_SOUTH_ARROW_WARP:
    case MB_UNUSED_6F:
        return true;
    default:
        return false;
    }
}

static uint64_t HashByte(uint64_t hash, uint8_t value)
{
    return (hash ^ value) * FNV_PRIME;
}

static uint64_t HashU16(uint64_t hash, uint16_t value)
{
    hash = HashByte(hash, value & 0xFF);
    return HashByte(hash, value >> 8);
}

static uint64_t HashU32(uint64_t hash, uint32_t value)
{
    hash = HashU16(hash, value & 0xFFFF);
    return HashU16(hash, value >> 16);
}

static uint64_t HashFloat(uint64_t hash, float value)
{
    uint32_t bits;

    memcpy(&bits, &value, sizeof(bits));
    return HashU32(hash, bits);
}

static const struct DioramaTerrainCell *GetCell(const struct DioramaTerrainChunkInput *input,
                                                 int x, int y)
{
    return &input->cells[y * DIORAMA_TERRAIN_INPUT_SIZE + x];
}

static float ResolveHeight(const struct DioramaTerrainChunkInput *input, int x, int y)
{
    return GetCell(input, x, y)->visualHeight;
}

static bool AppendVertex(struct DioramaTerrainVertex *vertices, uint32_t capacity,
                         uint32_t *count, float x, float y, float z,
                         float u, float v, float shade)
{
    struct DioramaTerrainVertex *vertex;

    if (*count >= capacity)
        return false;
    vertex = &vertices[(*count)++];
    vertex->x = x;
    vertex->y = y;
    vertex->z = z;
    vertex->u = u;
    vertex->v = v;
    vertex->shade = shade;
    return true;
}

static bool AppendQuad(struct DioramaTerrainVertex *vertices, uint32_t capacity,
                       uint32_t *count, const float positions[4][3],
                       const struct DioramaTerrainCell *cell, float shade)
{
    static const uint8_t order[6] = {0, 1, 2, 0, 2, 3};
    static const uint8_t uvX[4] = {0, 1, 1, 0};
    static const uint8_t uvY[4] = {0, 0, 1, 1};
    int i;

    for (i = 0; i < 6; i++)
    {
        int corner = order[i];
        float u = uvX[corner] ? cell->u1 : cell->u0;
        float v = uvY[corner] ? cell->v1 : cell->v0;

        if (!AppendVertex(vertices, capacity, count,
                          positions[corner][0], positions[corner][1], positions[corner][2],
                          u, v, shade))
            return false;
    }
    return true;
}

static void GetSidePositions(enum DioramaTerrainFace face,
                             float left, float right, float north, float south,
                             float top, float bottom, float positions[4][3])
{
    switch (face)
    {
    case DIORAMA_TERRAIN_FACE_NORTH:
        positions[0][0] = right; positions[0][1] = top;    positions[0][2] = north;
        positions[1][0] = left;  positions[1][1] = top;    positions[1][2] = north;
        positions[2][0] = left;  positions[2][1] = bottom; positions[2][2] = north;
        positions[3][0] = right; positions[3][1] = bottom; positions[3][2] = north;
        break;
    case DIORAMA_TERRAIN_FACE_EAST:
        positions[0][0] = right; positions[0][1] = top;    positions[0][2] = south;
        positions[1][0] = right; positions[1][1] = top;    positions[1][2] = north;
        positions[2][0] = right; positions[2][1] = bottom; positions[2][2] = north;
        positions[3][0] = right; positions[3][1] = bottom; positions[3][2] = south;
        break;
    case DIORAMA_TERRAIN_FACE_SOUTH:
        positions[0][0] = left;  positions[0][1] = top;    positions[0][2] = south;
        positions[1][0] = right; positions[1][1] = top;    positions[1][2] = south;
        positions[2][0] = right; positions[2][1] = bottom; positions[2][2] = south;
        positions[3][0] = left;  positions[3][1] = bottom; positions[3][2] = south;
        break;
    default:
        positions[0][0] = left; positions[0][1] = top;    positions[0][2] = north;
        positions[1][0] = left; positions[1][1] = top;    positions[1][2] = south;
        positions[2][0] = left; positions[2][1] = bottom; positions[2][2] = south;
        positions[3][0] = left; positions[3][1] = bottom; positions[3][2] = north;
        break;
    }
}

int32_t DioramaTerrain_FloorDiv(int32_t value, int32_t divisor)
{
    int32_t quotient = value / divisor;
    int32_t remainder = value % divisor;

    if (remainder != 0 && ((remainder < 0) != (divisor < 0)))
        quotient--;
    return quotient;
}

float DioramaTerrain_NormalizeElevation(uint8_t rawElevation, uint8_t behavior)
{
    (void)rawElevation;
    if (behavior >= MB_BRIDGE_OVER_OCEAN && behavior <= MB_BRIDGE_OVER_POND_HIGH)
    {
        static const float bridgeHeights[] = {0.75f, 0.75f, 1.75f, 2.75f};
        return bridgeHeights[behavior - MB_BRIDGE_OVER_OCEAN];
    }
    if (behavior >= MB_BRIDGE_OVER_POND_MED_EDGE_1 && behavior <= MB_BRIDGE_OVER_POND_MED_EDGE_2)
        return 1.75f;
    if (behavior >= MB_BRIDGE_OVER_POND_HIGH_EDGE_1 && behavior <= MB_BRIDGE_OVER_POND_HIGH_EDGE_2)
        return 2.75f;
    if (behavior == MB_FORTREE_BRIDGE || behavior == MB_BIKE_BRIDGE_OVER_BARRIER)
        return 0.75f;
    if (IsLedge(behavior))
        return DIORAMA_TERRAIN_LEDGE_HEIGHT;
    if (IsWater(behavior))
        return DIORAMA_TERRAIN_WATER_HEIGHT;
    return 0.0f;
}

void DioramaTerrain_BuildHeightField(const struct DioramaTerrainHeightCell *cells,
                                     uint16_t width, uint16_t height,
                                     float *visualHeights)
{
    int x;
    int y;

    for (y = 0; y < height; y++)
        for (x = 0; x < width; x++)
            visualHeights[y * width + x] = DioramaTerrain_NormalizeElevation(
                cells[y * width + x].rawElevation, cells[y * width + x].behavior);
}

uint64_t DioramaTerrain_ChunkSignature(const struct DioramaTerrainChunkInput *input)
{
    uint64_t hash = FNV_OFFSET;
    unsigned i;

    hash = HashU16(hash, input->chunkX);
    hash = HashU16(hash, input->chunkY);
    for (i = 0; i < DIORAMA_TERRAIN_INPUT_SIZE * DIORAMA_TERRAIN_INPUT_SIZE; i++)
    {
        const struct DioramaTerrainCell *cell = &input->cells[i];

        hash = HashByte(hash, cell->present);
        hash = HashU16(hash, cell->mapX);
        hash = HashU16(hash, cell->mapY);
        hash = HashU16(hash, cell->metatileId);
        hash = HashByte(hash, cell->behavior);
        hash = HashByte(hash, cell->layerType);
        hash = HashByte(hash, cell->rawElevation);
        hash = HashFloat(hash, cell->visualHeight);
    }
    return hash;
}

bool DioramaTerrain_BuildChunk(const struct DioramaTerrainChunkInput *input,
                               struct DioramaTerrainVertex *vertices,
                               uint32_t vertexCapacity,
                               struct DioramaTerrainMesh *mesh)
{
    static const int neighborOffsets[4][2] = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}};
    uint32_t vertexCount = 0;
    int y;
    int x;

    memset(mesh, 0, sizeof(*mesh));
    mesh->bounds.minX = FLT_MAX;
    mesh->bounds.minY = FLT_MAX;
    mesh->bounds.minZ = FLT_MAX;
    mesh->bounds.maxX = -FLT_MAX;
    mesh->bounds.maxY = -FLT_MAX;
    mesh->bounds.maxZ = -FLT_MAX;

    for (y = 1; y <= DIORAMA_TERRAIN_CHUNK_SIZE; y++)
    {
        for (x = 1; x <= DIORAMA_TERRAIN_CHUNK_SIZE; x++)
        {
            const struct DioramaTerrainCell *cell = GetCell(input, x, y);
            float height = ResolveHeight(input, x, y);
            float left = cell->mapX - 0.5f;
            float right = cell->mapX + 0.5f;
            float north = -cell->mapY + 0.5f;
            float south = -cell->mapY - 0.5f;
            float topPositions[4][3] = {
                {left, height, north}, {right, height, north},
                {right, height, south}, {left, height, south},
            };
            int face;

            if (!cell->present)
                continue;
            if (!AppendQuad(vertices, vertexCapacity, &vertexCount, topPositions, cell, 1.0f))
                return false;
            mesh->topFaceCount++;
            if (left < mesh->bounds.minX) mesh->bounds.minX = left;
            if (right > mesh->bounds.maxX) mesh->bounds.maxX = right;
            if (south < mesh->bounds.minZ) mesh->bounds.minZ = south;
            if (north > mesh->bounds.maxZ) mesh->bounds.maxZ = north;
            if (height < mesh->bounds.minY) mesh->bounds.minY = height;
            if (height > mesh->bounds.maxY) mesh->bounds.maxY = height;

            for (face = DIORAMA_TERRAIN_FACE_NORTH; face <= DIORAMA_TERRAIN_FACE_WEST; face++)
            {
                const struct DioramaTerrainCell *neighbor = GetCell(input,
                                                                    x + neighborOffsets[face - 1][0],
                                                                    y + neighborOffsets[face - 1][1]);
                float neighborHeight;
                float bottom;
                float sidePositions[4][3];

                if (!neighbor->present)
                    continue;
                neighborHeight = ResolveHeight(input,
                                               x + neighborOffsets[face - 1][0],
                                               y + neighborOffsets[face - 1][1]);
                bottom = neighborHeight;
                if (height <= neighborHeight)
                    continue;
                GetSidePositions(face, left, right, north, south, height, bottom, sidePositions);
                if (!AppendQuad(vertices, vertexCapacity, &vertexCount, sidePositions, cell,
                                face == DIORAMA_TERRAIN_FACE_NORTH ? 0.82f : 0.68f))
                    return false;
                mesh->sideFaceCount++;
                if (bottom < mesh->bounds.minY) mesh->bounds.minY = bottom;
            }
        }
    }

    mesh->vertexCount = vertexCount;
    mesh->faceCount = mesh->topFaceCount + mesh->sideFaceCount;
    mesh->geometryHash = FNV_OFFSET;
    for (uint32_t i = 0; i < vertexCount; i++)
    {
        mesh->geometryHash = HashFloat(mesh->geometryHash, vertices[i].x);
        mesh->geometryHash = HashFloat(mesh->geometryHash, vertices[i].y);
        mesh->geometryHash = HashFloat(mesh->geometryHash, vertices[i].z);
        mesh->geometryHash = HashFloat(mesh->geometryHash, vertices[i].u);
        mesh->geometryHash = HashFloat(mesh->geometryHash, vertices[i].v);
        mesh->geometryHash = HashFloat(mesh->geometryHash, vertices[i].shade);
    }
    return true;
}

bool DioramaTerrain_IsBoundsVisible(const struct DioramaTerrainBounds *bounds,
                                    float cameraX, float cameraZ)
{
    const float cameraHeight = 16.0f;
    const float cameraDistance = 18.0f;
    const float pitchSin = 0.65f;
    const float pitchCos = 0.759934f;
    const float focalLength = 130.0f;
    const float nearDepth = 0.1f;
    const float farDepth = 80.0f;
    bool outside[6] = {true, true, true, true, true, true};
    int i;

    for (i = 0; i < 8; i++)
    {
        float x = (i & 1) ? bounds->maxX : bounds->minX;
        float y = (i & 2) ? bounds->maxY : bounds->minY;
        float z = (i & 4) ? bounds->maxZ : bounds->minZ;
        float relativeX = x - cameraX;
        float relativeZ = z - cameraZ;
        float depth = (cameraHeight - y) * pitchSin + (relativeZ + cameraDistance) * pitchCos;
        float vertical = -(cameraHeight - y) * pitchCos + (relativeZ + cameraDistance) * pitchSin;
        float clipX = relativeX * focalLength * 2.0f / 240.0f;
        float clipY = -0.04f * depth + vertical * focalLength * 2.0f / 160.0f;

        if (depth >= nearDepth) outside[0] = false;
        if (depth <= farDepth) outside[1] = false;
        if (clipX >= -depth) outside[2] = false;
        if (clipX <= depth) outside[3] = false;
        if (clipY >= -depth) outside[4] = false;
        if (clipY <= depth) outside[5] = false;
    }
    for (i = 0; i < 6; i++)
        if (outside[i])
            return false;
    return true;
}

#endif
