#ifdef ENABLE_DIORAMA

#include <float.h>
#include <math.h>
#include <string.h>

#include "diorama/terrain_mesh.h"

#define FNV_OFFSET UINT64_C(1469598103934665603)
#define FNV_PRIME UINT64_C(1099511628211)

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
                         float u, float v, float shade, float textureLayer)
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
    vertex->textureLayer = textureLayer;
    return true;
}

static bool AppendQuad(struct DioramaTerrainVertex *vertices, uint32_t capacity,
                        uint32_t *count, const float positions[4][3],
                        const struct DioramaTerrainMaterial *material, float shade)
{
    static const uint8_t order[6] = {0, 1, 2, 0, 2, 3};
    static const uint8_t uvX[4] = {0, 1, 1, 0};
    static const uint8_t uvY[4] = {0, 0, 1, 1};
    int i;

    for (i = 0; i < 6; i++)
    {
        int corner = order[i];
        float u = uvX[corner] ? material->u1 : material->u0;
        float v = uvY[corner] ? material->v1 : material->v0;

        if (!AppendVertex(vertices, capacity, count,
                          positions[corner][0], positions[corner][1], positions[corner][2],
                          u, v, shade, material->layer))
            return false;
    }
    return true;
}

static float ResolveRoofHeight(const struct DioramaTerrainCell *cell, float worldX)
{
    float factor;
    float localX;

    if (cell->structureId == 0 || cell->profile != DIORAMA_ROOF_GABLE_X)
        return cell->visualHeight;
    localX = worldX - (cell->structureX - 0.5f);
    factor = 1.0f - fabsf(2.0f * localX / cell->structureWidth - 1.0f);
    if ((cell->structureWidth & 1) != 0)
        factor /= 1.0f - 1.0f / cell->structureWidth;
    if (factor < 0.0f)
        factor = 0.0f;
    if (factor > 1.0f)
        factor = 1.0f;
    return cell->groundHeight + cell->structureBodyHeight
         + cell->structureRoofHeight * factor;
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
    return DioramaRules_DefaultGroundHeight(behavior);
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
    hash = HashU32(hash, input->rulesGeneration);
    for (i = 0; i < DIORAMA_TERRAIN_INPUT_SIZE * DIORAMA_TERRAIN_INPUT_SIZE; i++)
    {
        const struct DioramaTerrainCell *cell = &input->cells[i];

        hash = HashByte(hash, cell->present);
        hash = HashU16(hash, cell->mapX);
        hash = HashU16(hash, cell->mapY);
        hash = HashU16(hash, cell->metatileId);
        hash = HashByte(hash, cell->behavior);
        hash = HashByte(hash, cell->layerType);
        hash = HashByte(hash, cell->shape);
        hash = HashByte(hash, cell->profile);
        hash = HashByte(hash, cell->planeAxis);
        hash = HashU16(hash, cell->structureId);
        hash = HashU16(hash, cell->structureX);
        hash = HashU16(hash, cell->structureY);
        hash = HashByte(hash, cell->structureWidth);
        hash = HashByte(hash, cell->structureHeight);
        hash = HashByte(hash, cell->structureRoofRows);
        hash = HashByte(hash, cell->structureLocalX);
        hash = HashByte(hash, cell->structureLocalY);
        hash = HashFloat(hash, cell->groundHeight);
        hash = HashFloat(hash, cell->visualHeight);
        hash = HashFloat(hash, cell->featureHeight);
        hash = HashFloat(hash, cell->structureBodyHeight);
        hash = HashFloat(hash, cell->structureRoofHeight);
        for (unsigned face = 0; face < DIORAMA_MATERIAL_FACE_COUNT; face++)
        {
            hash = HashU16(hash, cell->materials[face].metatileId);
            hash = HashByte(hash, cell->materials[face].layer);
        }
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

            if (!cell->present || cell->shape == DIORAMA_SHAPE_HIDDEN)
                continue;
            if (cell->shape == DIORAMA_SHAPE_ROOF && cell->structureId != 0)
            {
                topPositions[0][1] = ResolveRoofHeight(cell, left);
                topPositions[1][1] = ResolveRoofHeight(cell, right);
                topPositions[2][1] = ResolveRoofHeight(cell, right);
                topPositions[3][1] = ResolveRoofHeight(cell, left);
            }
            if (cell->materials[DIORAMA_MATERIAL_FACE_TOP].layer != DIORAMA_MATERIAL_NONE)
            {
                if (!AppendQuad(vertices, vertexCapacity, &vertexCount, topPositions,
                                &cell->materials[DIORAMA_MATERIAL_FACE_TOP], 1.0f))
                    return false;
                mesh->topFaceCount++;
            }
            if (left < mesh->bounds.minX) mesh->bounds.minX = left;
            if (right > mesh->bounds.maxX) mesh->bounds.maxX = right;
            if (south < mesh->bounds.minZ) mesh->bounds.minZ = south;
            if (north > mesh->bounds.maxZ) mesh->bounds.maxZ = north;
            for (int corner = 0; corner < 4; corner++)
            {
                if (topPositions[corner][1] < mesh->bounds.minY)
                    mesh->bounds.minY = topPositions[corner][1];
                if (topPositions[corner][1] > mesh->bounds.maxY)
                    mesh->bounds.maxY = topPositions[corner][1];
            }

            if (cell->shape == DIORAMA_SHAPE_CUTOUT)
            {
                float bottom = cell->groundHeight;
                float top = bottom + cell->featureHeight;
                float centerX = (left + right) * 0.5f;
                float centerZ = (north + south) * 0.5f;
                const float cutoutX[4][3] = {
                    {left, top, centerZ}, {right, top, centerZ},
                    {right, bottom, centerZ}, {left, bottom, centerZ},
                };
                const float cutoutZ[4][3] = {
                    {centerX, top, south}, {centerX, top, north},
                    {centerX, bottom, north}, {centerX, bottom, south},
                };

                const struct DioramaTerrainMaterial *planeMaterial =
                    &cell->materials[DIORAMA_MATERIAL_FACE_PLANE];

                if (planeMaterial->layer != DIORAMA_MATERIAL_NONE)
                {
                    if ((cell->planeAxis == DIORAMA_PLANE_AXIS_X
                      || cell->planeAxis == DIORAMA_PLANE_AXIS_CROSS)
                     && !AppendQuad(vertices, vertexCapacity, &vertexCount, cutoutX,
                                    planeMaterial, 1.0f))
                        return false;
                    if (cell->planeAxis == DIORAMA_PLANE_AXIS_X
                     || cell->planeAxis == DIORAMA_PLANE_AXIS_CROSS)
                        mesh->featureFaceCount++;
                    if ((cell->planeAxis == DIORAMA_PLANE_AXIS_Z
                      || cell->planeAxis == DIORAMA_PLANE_AXIS_CROSS)
                     && !AppendQuad(vertices, vertexCapacity, &vertexCount, cutoutZ,
                                    planeMaterial, 0.86f))
                        return false;
                    if (cell->planeAxis == DIORAMA_PLANE_AXIS_Z
                     || cell->planeAxis == DIORAMA_PLANE_AXIS_CROSS)
                        mesh->featureFaceCount++;
                }
                if (top > mesh->bounds.maxY) mesh->bounds.maxY = top;
            }

            for (face = DIORAMA_TERRAIN_FACE_NORTH; face <= DIORAMA_TERRAIN_FACE_WEST; face++)
            {
                const struct DioramaTerrainCell *neighbor = GetCell(input,
                                                                    x + neighborOffsets[face - 1][0],
                                                                    y + neighborOffsets[face - 1][1]);
                float neighborHeight;
                float bottom;
                float sidePositions[4][3];
                const struct DioramaTerrainMaterial *sideMaterial = &cell->materials[face];

                if (!neighbor->present)
                    continue;
                if (sideMaterial->layer == DIORAMA_MATERIAL_NONE)
                    continue;
                if (cell->structureId != 0 && neighbor->structureId == cell->structureId)
                    continue;
                neighborHeight = ResolveHeight(input,
                                               x + neighborOffsets[face - 1][0],
                                               y + neighborOffsets[face - 1][1]);
                bottom = neighborHeight;
                if (height <= neighborHeight)
                    continue;
                GetSidePositions(face, left, right, north, south, height, bottom, sidePositions);
                if (cell->shape == DIORAMA_SHAPE_ROOF && cell->structureId != 0)
                {
                    if (face == DIORAMA_TERRAIN_FACE_NORTH || face == DIORAMA_TERRAIN_FACE_SOUTH)
                    {
                        sidePositions[0][1] = ResolveRoofHeight(cell, sidePositions[0][0]);
                        sidePositions[1][1] = ResolveRoofHeight(cell, sidePositions[1][0]);
                    }
                    else
                    {
                        float edgeHeight = ResolveRoofHeight(cell,
                            face == DIORAMA_TERRAIN_FACE_EAST ? right : left);
                        sidePositions[0][1] = edgeHeight;
                        sidePositions[1][1] = edgeHeight;
                    }
                }
                if (!AppendQuad(vertices, vertexCapacity, &vertexCount, sidePositions,
                                 sideMaterial,
                                 face == DIORAMA_TERRAIN_FACE_NORTH ? 0.82f : 0.68f))
                    return false;
                mesh->sideFaceCount++;
                if (bottom < mesh->bounds.minY) mesh->bounds.minY = bottom;
            }
        }
    }

    mesh->vertexCount = vertexCount;
    mesh->faceCount = mesh->topFaceCount + mesh->sideFaceCount + mesh->featureFaceCount;
    mesh->geometryHash = FNV_OFFSET;
    for (uint32_t i = 0; i < vertexCount; i++)
    {
        mesh->geometryHash = HashFloat(mesh->geometryHash, vertices[i].x);
        mesh->geometryHash = HashFloat(mesh->geometryHash, vertices[i].y);
        mesh->geometryHash = HashFloat(mesh->geometryHash, vertices[i].z);
        mesh->geometryHash = HashFloat(mesh->geometryHash, vertices[i].u);
        mesh->geometryHash = HashFloat(mesh->geometryHash, vertices[i].v);
        mesh->geometryHash = HashFloat(mesh->geometryHash, vertices[i].shade);
        mesh->geometryHash = HashFloat(mesh->geometryHash, vertices[i].textureLayer);
    }
    return true;
}

bool DioramaTerrain_IsBoundsVisible(const struct DioramaTerrainBounds *bounds,
                                    float cameraX, float cameraZ,
                                    float cameraPitch, float focalLength)
{
    const float cameraHeight = 16.0f;
    const float cameraTargetVertical = -0.458944f;
    const float pitchSin = sinf(cameraPitch);
    const float pitchCos = cosf(cameraPitch);
    const float cameraDistance = (cameraHeight * pitchCos + cameraTargetVertical) / pitchSin;
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
