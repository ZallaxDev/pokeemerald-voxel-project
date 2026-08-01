#ifdef ENABLE_DIORAMA

#include <float.h>
#include <math.h>
#include <string.h>

#include "constants/metatile_behaviors.h"
#include "diorama/rules.generated.h"
#include "diorama/terrain_mesh.h"

#define FNV_OFFSET UINT64_C(1469598103934665603)
#define FNV_PRIME UINT64_C(1099511628211)

#define OCCUPANCY_INPUT_COLUMNS (DIORAMA_TERRAIN_INPUT_SIZE * DIORAMA_TERRAIN_INPUT_SIZE * DIORAMA_VOXELS_PER_CELL * DIORAMA_VOXELS_PER_CELL * DIORAMA_TERRAIN_MAX_SURFACES)

static struct DioramaOccupancySpan sOccupancySpans[OCCUPANCY_INPUT_COLUMNS];
static struct DioramaShellFace sShellFaces[DIORAMA_TERRAIN_MAX_FACES];

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

static bool AppendVertex(struct DioramaTerrainVertex *vertices, uint32_t capacity,
                          uint32_t *count, float x, float y, float z,
                          float u, float v, float shade, float textureLayer,
                          float reflectionMask)
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
    vertex->reflectionMask = reflectionMask;
    return true;
}

static bool AppendQuad(struct DioramaTerrainVertex *vertices, uint32_t capacity,
                        uint32_t *count, const float positions[4][3],
                        const struct DioramaTerrainMaterial *material, float shade,
                        float reflectionMask)
{
    static const uint8_t order[6] = {0, 1, 2, 0, 2, 3};
    static const uint8_t uvX[4] = {0, 1, 1, 0};
    static const uint8_t uvY[4] = {0, 0, 1, 1};
    int i;

    for (i = 0; i < 6; i++)
    {
        int corner = order[i];
        uint8_t x = uvX[corner];
        uint8_t y = uvY[corner];
        uint8_t turn;
        float u;
        float v;

        if (material->flags & 1)
            x = 1 - x;
        if (material->flags & 2)
            y = 1 - y;
        for (turn = 0; turn < material->rotation; turn++)
        {
            uint8_t previousX = x;
            x = 1 - y;
            y = previousX;
        }
        u = x ? material->u1 : material->u0;
        v = y ? material->v1 : material->v0;

        if (!AppendVertex(vertices, capacity, count,
                          positions[corner][0], positions[corner][1], positions[corner][2],
                          u, v, shade, material->layer, reflectionMask))
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

enum DioramaElevationSemantics DioramaTerrain_GetElevationSemantics(uint8_t rawElevation)
{
    if (rawElevation == 0)
        return DIORAMA_ELEVATION_WILDCARD;
    if (rawElevation == 15)
        return DIORAMA_ELEVATION_RETAIN;
    return DIORAMA_ELEVATION_CONCRETE;
}

bool DioramaTerrain_DirtyCellAffectsChunk(int16_t mapX, int16_t mapY,
                                         int16_t chunkX, int16_t chunkY)
{
    int32_t originX = chunkX * DIORAMA_TERRAIN_CHUNK_SIZE;
    int32_t originY = chunkY * DIORAMA_TERRAIN_CHUNK_SIZE;

    return mapX >= originX - DIORAMA_TERRAIN_HALO
        && mapX < originX + DIORAMA_TERRAIN_CHUNK_SIZE + DIORAMA_TERRAIN_HALO
        && mapY >= originY - DIORAMA_TERRAIN_HALO
        && mapY < originY + DIORAMA_TERRAIN_CHUNK_SIZE + DIORAMA_TERRAIN_HALO;
}

bool DioramaTerrain_ShouldInvalidateAll(uint64_t previousSequence,
                                       uint64_t currentSequence,
                                       uint32_t previousEditGeneration,
                                       uint32_t currentEditGeneration,
                                       uint8_t dirtyCellCount,
                                       bool dirtyOverflow)
{
    if (dirtyOverflow)
        return true;
    if (previousEditGeneration == currentEditGeneration)
        return false;
    return previousSequence == 0
        || currentSequence != previousSequence + 1
        || dirtyCellCount == 0;
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
        hash = HashByte(hash, cell->reflective);
        hash = HashByte(hash, cell->shape);
        hash = HashByte(hash, cell->archetype);
        hash = HashByte(hash, cell->terrainClass);
        hash = HashByte(hash, cell->semanticProfile);
        hash = HashByte(hash, cell->profile);
        hash = HashByte(hash, cell->planeAxis);
        hash = HashByte(hash, cell->effectiveElevation);
        hash = HashByte(hash, cell->surfaceCount);
        hash = HashU16(hash, cell->structureId);
        hash = HashU16(hash, cell->rulePriority);
        hash = HashU16(hash, cell->structureTemplateId);
        hash = HashU16(hash, cell->structureX);
        hash = HashU16(hash, cell->structureY);
        hash = HashByte(hash, cell->structureWidth);
        hash = HashByte(hash, cell->structureHeight);
        hash = HashByte(hash, cell->structureRoofRows);
        hash = HashByte(hash, cell->structureLocalX);
        hash = HashByte(hash, cell->structureLocalY);
        hash = HashByte(hash, cell->southFacadeCount);
        hash = HashByte(hash, cell->cliffEdgeMask);
        hash = HashByte(hash, cell->cliffBaseMask);
        hash = HashByte(hash, cell->cliffTransitionMask);
        hash = HashByte(hash, cell->cliffCornerMask);
        hash = HashFloat(hash, cell->groundHeight);
        hash = HashFloat(hash, cell->visualHeight);
        hash = HashFloat(hash, cell->featureHeight);
        hash = HashFloat(hash, cell->structureBodyHeight);
        hash = HashFloat(hash, cell->structureRoofHeight);
        hash = HashFloat(hash, cell->southFacadeUnitHeight);
        for (unsigned surface = 0; surface < cell->surfaceCount; surface++)
        {
            hash = HashFloat(hash, cell->surfaces[surface].bottomHeight);
            hash = HashFloat(hash, cell->surfaces[surface].topHeight);
            hash = HashByte(hash, cell->surfaces[surface].gameplayElevation);
        }
        for (unsigned face = 0; face < DIORAMA_MATERIAL_FACE_COUNT; face++)
        {
            hash = HashU16(hash, cell->materials[face].metatileId);
            hash = HashByte(hash, cell->materials[face].layer);
            hash = HashByte(hash, cell->materials[face].rotation);
            hash = HashByte(hash, cell->materials[face].flags);
            hash = HashFloat(hash, cell->materials[face].u0);
            hash = HashFloat(hash, cell->materials[face].v0);
            hash = HashFloat(hash, cell->materials[face].u1);
            hash = HashFloat(hash, cell->materials[face].v1);
            hash = HashU16(hash, cell->underlayMaterials[face].metatileId);
            hash = HashByte(hash, cell->underlayMaterials[face].layer);
            hash = HashFloat(hash, cell->underlayMaterials[face].u0);
            hash = HashFloat(hash, cell->underlayMaterials[face].v0);
            hash = HashFloat(hash, cell->underlayMaterials[face].u1);
            hash = HashFloat(hash, cell->underlayMaterials[face].v1);
        }
        for (unsigned row = 0; row < DIORAMA_VOXELS_PER_CELL; row++)
            hash = HashU16(hash, cell->foregroundAlpha[row]);
        for (unsigned row = 0; row < cell->southFacadeCount; row++)
        {
            hash = HashU16(hash, cell->southFacadeMaterials[row].metatileId);
            hash = HashByte(hash, cell->southFacadeMaterials[row].layer);
        }
    }
    return hash;
}

static int16_t HeightToVoxel(float height)
{
    float scaled = height * DIORAMA_VOXELS_PER_CELL;

    return (int16_t)(scaled < 0.0f ? scaled - 0.5f : scaled + 0.5f);
}

static uint16_t MaterialId(int cellIndex, int surface, int face)
{
    return (uint16_t)((cellIndex * DIORAMA_TERRAIN_MAX_SURFACES + surface)
                    * DIORAMA_MATERIAL_FACE_COUNT + face + 1);
}

static uint8_t LedgeDirection(uint8_t behavior)
{
    switch (behavior)
    {
    case MB_JUMP_NORTH: return 0;
    case MB_JUMP_NORTHEAST: return 1;
    case MB_JUMP_EAST: return 2;
    case MB_JUMP_SOUTHEAST: return 3;
    case MB_JUMP_SOUTH: return 4;
    case MB_JUMP_SOUTHWEST: return 5;
    case MB_JUMP_WEST: return 6;
    case MB_JUMP_NORTHWEST: return 7;
    default: return 0;
    }
}

static int16_t StairTop(const struct DioramaTerrainCell *cell, int pixelX, int pixelZ)
{
    return HeightToVoxel(DioramaRules_ProfileHeight(
        cell->archetype, cell->behavior, cell->groundHeight, cell->featureHeight,
        pixelX, pixelZ));
}

static bool AppendSpan(uint32_t *count, const struct DioramaTerrainCell *cell,
                       int cellIndex, int pixelX, int pixelZ, int surface,
                       int16_t bottom, int16_t top)
{
    struct DioramaOccupancySpan *span;

    if (bottom >= top)
        return true;
    if (*count >= OCCUPANCY_INPUT_COLUMNS)
        return false;
    span = &sOccupancySpans[(*count)++];
    memset(span, 0, sizeof(*span));
    span->x = cell->mapX * DIORAMA_VOXELS_PER_CELL + pixelX;
    span->z = cell->mapY * DIORAMA_VOXELS_PER_CELL + pixelZ;
    span->yMin = bottom;
    span->yMax = top;
    span->sourceCellX = cell->mapX;
    span->sourceCellY = cell->mapY;
    span->flags = cell->reflective;
    span->material[DIORAMA_OCCUPANCY_FACE_TOP] = MaterialId(cellIndex, surface, DIORAMA_MATERIAL_FACE_TOP);
    span->material[DIORAMA_OCCUPANCY_FACE_BOTTOM] = MaterialId(cellIndex, surface, DIORAMA_MATERIAL_FACE_TOP);
    span->material[DIORAMA_OCCUPANCY_FACE_NORTH] = MaterialId(cellIndex, surface, DIORAMA_MATERIAL_FACE_NORTH);
    span->material[DIORAMA_OCCUPANCY_FACE_EAST] = MaterialId(cellIndex, surface, DIORAMA_MATERIAL_FACE_EAST);
    span->material[DIORAMA_OCCUPANCY_FACE_SOUTH] = MaterialId(cellIndex, surface, DIORAMA_MATERIAL_FACE_SOUTH);
    span->material[DIORAMA_OCCUPANCY_FACE_WEST] = MaterialId(cellIndex, surface, DIORAMA_MATERIAL_FACE_WEST);
    return true;
}

static bool BuildOccupancy(const struct DioramaTerrainChunkInput *input,
                           uint32_t *spanCount)
{
    uint32_t count = 0;
    int cellY;
    int cellX;

    for (cellY = 0; cellY < DIORAMA_TERRAIN_INPUT_SIZE; cellY++)
    {
        for (cellX = 0; cellX < DIORAMA_TERRAIN_INPUT_SIZE; cellX++)
        {
            int cellIndex = cellY * DIORAMA_TERRAIN_INPUT_SIZE + cellX;
            const struct DioramaTerrainCell *cell = &input->cells[cellIndex];
            int pixelY;
            int pixelX;

            if (!cell->present || cell->shape == DIORAMA_SHAPE_HIDDEN)
                continue;
            for (pixelY = 0; pixelY < DIORAMA_VOXELS_PER_CELL; pixelY++)
            {
                for (pixelX = 0; pixelX < DIORAMA_VOXELS_PER_CELL; pixelX++)
                {
                    float topHeight = cell->visualHeight;
                    int16_t top;
                    int16_t bottom;

                    if (!DioramaRules_ProfileOccupies(cell->semanticProfile, pixelX, pixelY))
                        continue;
                    if (cell->shape == DIORAMA_SHAPE_ROOF && cell->structureId != 0)
                    {
                        float worldX = cell->mapX - 0.5f
                                     + (pixelX + 0.5f) / DIORAMA_VOXELS_PER_CELL;
                        topHeight = ResolveRoofHeight(cell, worldX);
                    }
                    top = HeightToVoxel(topHeight);
                    bottom = top > 0 ? -1 : top - 1;
                    if (cell->shape == DIORAMA_SHAPE_BRIDGE)
                    {
                        if (cell->surfaceCount > 1)
                        {
                            uint8_t surface;

                            for (surface = 0; surface < cell->surfaceCount; surface++)
                                if (!AppendSpan(&count, cell, cellIndex, pixelX, pixelY, surface,
                                        HeightToVoxel(cell->surfaces[surface].bottomHeight),
                                        HeightToVoxel(cell->surfaces[surface].topHeight)))
                                    return false;
                        }
                        else if (!AppendSpan(&count, cell, cellIndex, pixelX, pixelY, 1, -1, 0)
                              || !AppendSpan(&count, cell, cellIndex, pixelX, pixelY, 0, top - 1, top))
                            return false;
                    }
                    else if (cell->shape == DIORAMA_SHAPE_LEDGE)
                    {
                        int16_t ledgeTop = HeightToVoxel(cell->groundHeight);

                        if (!AppendSpan(&count, cell, cellIndex, pixelX, pixelY,
                                        0, ledgeTop - 1, ledgeTop))
                            return false;
                    }
                    else if (cell->shape == DIORAMA_SHAPE_STAIRS)
                    {
                        bool descending = cell->archetype >= DIORAMA_ARCHETYPE_STAIRS_DOWN_N
                                       && cell->archetype <= DIORAMA_ARCHETYPE_STAIRS_DOWN_W;
                        top = StairTop(cell, pixelX, pixelY);
                        bottom = descending
                               ? HeightToVoxel(cell->groundHeight -
                                     (cell->featureHeight != 0.0f ? cell->featureHeight : 1.0f)) - 1
                               : -1;
                        if (!AppendSpan(&count, cell, cellIndex, pixelX, pixelY, 0, bottom, top))
                            return false;
                    }
                    else if (!AppendSpan(&count, cell, cellIndex, pixelX, pixelY, 0, bottom, top))
                        return false;
                }
            }
        }
    }
    if (!DioramaOccupancy_Canonicalize(sOccupancySpans, &count))
        return false;
    *spanCount = count;
    return true;
}

static bool AppendCompressedFace(uint32_t *faceCount, uint32_t *rawFaceCount,
                                 const struct DioramaTerrainCell *cell, int cellIndex,
                                 uint8_t occupancyFace, int materialFace,
                                 int32_t plane, int32_t uMin, int32_t uMax,
                                 int16_t vMin, int16_t vMax, uint8_t axis, int8_t sign,
                                 uint32_t rawWeight)
{
    struct DioramaShellFace *face;

    if (*faceCount >= DIORAMA_TERRAIN_MAX_FACES || vMin >= vMax)
        return vMin >= vMax;
    face = &sShellFaces[(*faceCount)++];
    face->plane = plane;
    face->uMin = uMin;
    face->uMax = uMax;
    face->vMin = vMin;
    face->vMax = vMax;
    face->sourceCellX = cell->mapX;
    face->sourceCellY = cell->mapY;
    face->material = MaterialId(cellIndex, 0, materialFace);
    face->flags = cell->reflective;
    face->axis = axis;
    face->sign = sign;
    *rawFaceCount += rawWeight;
    (void)occupancyFace;
    return true;
}

static bool AppendCompressedSide(uint32_t *faceCount, uint32_t *rawFaceCount,
                                 const struct DioramaTerrainCell *cell, int cellIndex,
                                 const struct DioramaTerrainCell *neighbor,
                                 uint8_t occupancyFace, int materialFace,
                                 int32_t plane, int32_t uMin, int32_t uMax,
                                 uint8_t axis, int8_t sign,
                                 int16_t bottom, int16_t top)
{
    int16_t neighborBottom;
    int16_t neighborTop;

    if (!neighbor->present || neighbor->shape == DIORAMA_SHAPE_HIDDEN)
        return AppendCompressedFace(faceCount, rawFaceCount, cell, cellIndex,
                                    occupancyFace, materialFace, plane, uMin, uMax,
                                    bottom, top, axis, sign, DIORAMA_VOXELS_PER_CELL);
    neighborTop = neighbor->shape == DIORAMA_SHAPE_LEDGE
                ? 0 : HeightToVoxel(neighbor->visualHeight);
    neighborBottom = neighborTop > 0 ? -1 : neighborTop - 1;
    if (neighborTop <= bottom || neighborBottom >= top)
        return AppendCompressedFace(faceCount, rawFaceCount, cell, cellIndex,
                                    occupancyFace, materialFace, plane, uMin, uMax,
                                    bottom, top, axis, sign, DIORAMA_VOXELS_PER_CELL);
    if (neighborBottom > bottom
     && !AppendCompressedFace(faceCount, rawFaceCount, cell, cellIndex,
                              occupancyFace, materialFace, plane, uMin, uMax,
                              bottom, neighborBottom < top ? neighborBottom : top,
                              axis, sign, DIORAMA_VOXELS_PER_CELL))
        return false;
    if (neighborTop < top
     && !AppendCompressedFace(faceCount, rawFaceCount, cell, cellIndex,
                              occupancyFace, materialFace, plane, uMin, uMax,
                              neighborTop > bottom ? neighborTop : bottom, top,
                              axis, sign, DIORAMA_VOXELS_PER_CELL))
        return false;
    return true;
}

static bool LedgeFaceIsOccluded(const struct DioramaTerrainChunkInput *input,
                                int gridX, int gridY, uint8_t direction, int16_t height)
{
    static const int8_t offsets[4][2] = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}};
    const struct DioramaTerrainCell *neighbor = GetCell(input,
        gridX + offsets[direction / 2][0], gridY + offsets[direction / 2][1]);
    int16_t top;
    int16_t bottom;

    if (!neighbor->present || neighbor->shape == DIORAMA_SHAPE_HIDDEN
     || neighbor->shape == DIORAMA_SHAPE_STAIRS)
        return false;
    top = neighbor->shape == DIORAMA_SHAPE_LEDGE ? 0 : HeightToVoxel(neighbor->visualHeight);
    bottom = top > 0 ? -1 : top - 1;
    return bottom <= 0 && top >= height;
}

static bool AppendCompressedLedgeFace(const struct DioramaTerrainChunkInput *input,
                                       uint32_t *faceCount, uint32_t *rawFaceCount,
                                       const struct DioramaTerrainCell *cell, int cellIndex,
                                       int gridX, int gridY, uint8_t direction)
{
    int16_t height = HeightToVoxel(cell->featureHeight != 0.0f
                                 ? cell->featureHeight : DIORAMA_TERRAIN_LEDGE_HEIGHT);
    uint8_t occupancyFace = direction == 0 ? DIORAMA_OCCUPANCY_FACE_NORTH
                           : direction == 2 ? DIORAMA_OCCUPANCY_FACE_EAST
                           : direction == 4 ? DIORAMA_OCCUPANCY_FACE_SOUTH
                                            : DIORAMA_OCCUPANCY_FACE_WEST;
    int materialFace = occupancyFace == DIORAMA_OCCUPANCY_FACE_NORTH ? DIORAMA_MATERIAL_FACE_NORTH
                     : occupancyFace == DIORAMA_OCCUPANCY_FACE_EAST ? DIORAMA_MATERIAL_FACE_EAST
                     : occupancyFace == DIORAMA_OCCUPANCY_FACE_SOUTH ? DIORAMA_MATERIAL_FACE_SOUTH
                                                                    : DIORAMA_MATERIAL_FACE_WEST;
    uint8_t axis = occupancyFace == DIORAMA_OCCUPANCY_FACE_EAST
                || occupancyFace == DIORAMA_OCCUPANCY_FACE_WEST ? 0 : 2;
    int8_t sign = occupancyFace == DIORAMA_OCCUPANCY_FACE_NORTH
               || occupancyFace == DIORAMA_OCCUPANCY_FACE_WEST ? -1 : 1;
    int32_t pixelX = cell->mapX * DIORAMA_VOXELS_PER_CELL;
    int32_t pixelZ = cell->mapY * DIORAMA_VOXELS_PER_CELL;
    int32_t plane = axis == 0 ? pixelX + (sign > 0) * DIORAMA_VOXELS_PER_CELL
                               : pixelZ + (sign > 0) * DIORAMA_VOXELS_PER_CELL;
    int32_t sourceU = axis == 0 ? pixelZ : pixelX;
    int sourceY;

    if (LedgeFaceIsOccluded(input, gridX, gridY, direction, height))
        return true;

    for (sourceY = DIORAMA_VOXELS_PER_CELL - height;
         sourceY < DIORAMA_VOXELS_PER_CELL; sourceY++)
    {
        uint16_t row = cell->foregroundAlpha[sourceY];
        int sourceX = 0;

        while (sourceX < DIORAMA_VOXELS_PER_CELL)
        {
            int runStart;

            while (sourceX < DIORAMA_VOXELS_PER_CELL && !(row & (1u << sourceX)))
                sourceX++;
            runStart = sourceX;
            while (sourceX < DIORAMA_VOXELS_PER_CELL && (row & (1u << sourceX)))
                sourceX++;
            if (runStart < sourceX
             && !AppendCompressedFace(faceCount, rawFaceCount, cell, cellIndex,
                    occupancyFace, materialFace, plane,
                    sourceU + runStart, sourceU + sourceX,
                    DIORAMA_VOXELS_PER_CELL - sourceY - 1,
                    DIORAMA_VOXELS_PER_CELL - sourceY,
                    axis, sign, sourceX - runStart))
                return false;
        }
    }
    return true;
}

static bool AppendCompressedLedgeFaces(const struct DioramaTerrainChunkInput *input,
                                        uint32_t *faceCount, uint32_t *rawFaceCount,
                                        const struct DioramaTerrainCell *cell, int cellIndex,
                                        int gridX, int gridY, uint8_t direction)
{
    static const uint8_t first[8] = {0, 0, 2, 2, 4, 4, 6, 6};
    static const uint8_t second[8] = {0, 2, 2, 4, 4, 6, 6, 0};

    if (!AppendCompressedLedgeFace(input, faceCount, rawFaceCount, cell, cellIndex,
                                   gridX, gridY, first[direction]))
        return false;
    return second[direction] == first[direction]
        || AppendCompressedLedgeFace(input, faceCount, rawFaceCount, cell, cellIndex,
                                     gridX, gridY, second[direction]);
}

static bool BuildCompressedOccupancyShell(const struct DioramaTerrainChunkInput *input,
                                          uint32_t *spanCount, uint32_t *faceCount,
                                          uint32_t *rawFaceCount)
{
    uint32_t spans = 0;
    uint32_t faces = 0;
    uint32_t rawFaces = 0;
    int y;
    int x;

    for (y = 0; y < DIORAMA_TERRAIN_INPUT_SIZE; y++)
        for (x = 0; x < DIORAMA_TERRAIN_INPUT_SIZE; x++)
        {
            const struct DioramaTerrainCell *cell = GetCell(input, x, y);
            if (cell->present && cell->shape != DIORAMA_SHAPE_HIDDEN)
            {
                if (cell->shape == DIORAMA_SHAPE_ROOF
                 || cell->shape == DIORAMA_SHAPE_STAIRS
                 || cell->shape == DIORAMA_SHAPE_BRIDGE
                 || cell->surfaceCount > 1
                 || !DioramaRules_ProfileIsFullCell(cell->semanticProfile))
                    return false;
                spans += DIORAMA_VOXELS_PER_CELL * DIORAMA_VOXELS_PER_CELL;
            }
        }
    for (y = DIORAMA_TERRAIN_HALO;
         y < DIORAMA_TERRAIN_HALO + DIORAMA_TERRAIN_CHUNK_SIZE; y++)
    {
        for (x = DIORAMA_TERRAIN_HALO;
             x < DIORAMA_TERRAIN_HALO + DIORAMA_TERRAIN_CHUNK_SIZE; x++)
        {
            static const int8_t offsets[4][2] = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}};
            static const uint8_t occupancyFaces[4] = {DIORAMA_OCCUPANCY_FACE_NORTH,
                DIORAMA_OCCUPANCY_FACE_EAST, DIORAMA_OCCUPANCY_FACE_SOUTH,
                DIORAMA_OCCUPANCY_FACE_WEST};
            const struct DioramaTerrainCell *cell = GetCell(input, x, y);
            int cellIndex = y * DIORAMA_TERRAIN_INPUT_SIZE + x;
            int32_t pixelX;
            int32_t pixelZ;
            int16_t top;
            int16_t bottom;
            int side;

            if (!cell->present || cell->shape == DIORAMA_SHAPE_HIDDEN)
                continue;
            pixelX = cell->mapX * DIORAMA_VOXELS_PER_CELL;
            pixelZ = cell->mapY * DIORAMA_VOXELS_PER_CELL;
            top = HeightToVoxel(cell->visualHeight);
            bottom = top > 0 ? -1 : top - 1;
            if (!AppendCompressedFace(&faces, &rawFaces, cell, cellIndex,
                                      DIORAMA_OCCUPANCY_FACE_TOP,
                                      DIORAMA_MATERIAL_FACE_TOP, top,
                                      pixelX, pixelX + DIORAMA_VOXELS_PER_CELL,
                                      pixelZ, pixelZ + DIORAMA_VOXELS_PER_CELL,
                                      1, 1, DIORAMA_VOXELS_PER_CELL * DIORAMA_VOXELS_PER_CELL)
             || !AppendCompressedFace(&faces, &rawFaces, cell, cellIndex,
                                      DIORAMA_OCCUPANCY_FACE_BOTTOM,
                                      DIORAMA_MATERIAL_FACE_TOP, bottom,
                                      pixelX, pixelX + DIORAMA_VOXELS_PER_CELL,
                                      pixelZ, pixelZ + DIORAMA_VOXELS_PER_CELL,
                                      1, -1, DIORAMA_VOXELS_PER_CELL * DIORAMA_VOXELS_PER_CELL))
                return false;
            for (side = 0; side < 4; side++)
            {
                const struct DioramaTerrainCell *neighbor =
                    GetCell(input, x + offsets[side][0], y + offsets[side][1]);
                uint8_t axis = side == 1 || side == 3 ? 0 : 2;
                int8_t sign = side == 0 || side == 3 ? -1 : 1;
                int32_t plane = axis == 0 ? pixelX + (sign > 0) * DIORAMA_VOXELS_PER_CELL
                                          : pixelZ + (sign > 0) * DIORAMA_VOXELS_PER_CELL;
                int32_t uMin = axis == 0 ? pixelZ : pixelX;

                if (!AppendCompressedSide(&faces, &rawFaces, cell, cellIndex, neighbor,
                                          occupancyFaces[side], side + 1, plane,
                                          uMin, uMin + DIORAMA_VOXELS_PER_CELL,
                                          axis, sign, bottom, top))
                    return false;
            }
            if (cell->shape == DIORAMA_SHAPE_LEDGE && cell->groundHeight == 0.0f
              && !AppendCompressedLedgeFaces(input, &faces, &rawFaces, cell, cellIndex,
                                            x, y,
                                            LedgeDirection(cell->behavior)))
                return false;
        }
    }
    *spanCount = spans;
    *faceCount = faces;
    *rawFaceCount = rawFaces;
    return true;
}

static bool AppendFallbackLedgeFaces(const struct DioramaTerrainChunkInput *input,
                                     uint32_t *faceCount, uint32_t *rawFaceCount)
{
    int y;
    int x;

    for (y = DIORAMA_TERRAIN_HALO;
         y < DIORAMA_TERRAIN_HALO + DIORAMA_TERRAIN_CHUNK_SIZE; y++)
        for (x = DIORAMA_TERRAIN_HALO;
             x < DIORAMA_TERRAIN_HALO + DIORAMA_TERRAIN_CHUNK_SIZE; x++)
        {
            const struct DioramaTerrainCell *cell = GetCell(input, x, y);
            int cellIndex = y * DIORAMA_TERRAIN_INPUT_SIZE + x;

            if (cell->present && cell->shape == DIORAMA_SHAPE_LEDGE
             && cell->groundHeight == 0.0f
              && !AppendCompressedLedgeFaces(input, faceCount, rawFaceCount, cell, cellIndex,
                     x, y,
                     LedgeDirection(cell->behavior)))
                return false;
        }
    return true;
}

static const struct DioramaTerrainCell *FaceSourceCell(
    const struct DioramaTerrainChunkInput *input, const struct DioramaShellFace *face)
{
    int x = face->sourceCellX - input->chunkX * DIORAMA_TERRAIN_CHUNK_SIZE
          + DIORAMA_TERRAIN_HALO;
    int y = face->sourceCellY - input->chunkY * DIORAMA_TERRAIN_CHUNK_SIZE
          + DIORAMA_TERRAIN_HALO;

    if (x < 0 || y < 0 || x >= DIORAMA_TERRAIN_INPUT_SIZE || y >= DIORAMA_TERRAIN_INPUT_SIZE)
        return NULL;
    return GetCell(input, x, y);
}

static float VoxelX(int32_t value)
{
    return (value - DIORAMA_VOXELS_PER_CELL / 2) / (float)DIORAMA_VOXELS_PER_CELL;
}

static float VoxelY(int32_t value)
{
    return value / (float)DIORAMA_VOXELS_PER_CELL;
}

static float VoxelZ(int32_t value)
{
    return -(value - DIORAMA_VOXELS_PER_CELL / 2) / (float)DIORAMA_VOXELS_PER_CELL;
}

static void FacePositions(const struct DioramaShellFace *face, float positions[4][3])
{
    if (face->axis == 1)
    {
        float left = VoxelX(face->uMin);
        float right = VoxelX(face->uMax);
        float north = VoxelZ(face->vMin);
        float south = VoxelZ(face->vMax);
        float height = VoxelY(face->plane);

        positions[0][0] = left;  positions[0][1] = height; positions[0][2] = north;
        positions[1][0] = right; positions[1][1] = height; positions[1][2] = north;
        positions[2][0] = right; positions[2][1] = height; positions[2][2] = south;
        positions[3][0] = left;  positions[3][1] = height; positions[3][2] = south;
        if (face->sign < 0)
        {
            float temporary[3];
            memcpy(temporary, positions[1], sizeof(temporary));
            memcpy(positions[1], positions[3], sizeof(temporary));
            memcpy(positions[3], temporary, sizeof(temporary));
        }
    }
    else if (face->axis == 0)
    {
        float x = VoxelX(face->plane);
        float north = VoxelZ(face->uMin);
        float south = VoxelZ(face->uMax);
        float bottom = VoxelY(face->vMin);
        float top = VoxelY(face->vMax);

        positions[0][0] = x; positions[0][1] = top;    positions[0][2] = south;
        positions[1][0] = x; positions[1][1] = top;    positions[1][2] = north;
        positions[2][0] = x; positions[2][1] = bottom; positions[2][2] = north;
        positions[3][0] = x; positions[3][1] = bottom; positions[3][2] = south;
    }
    else
    {
        float left = VoxelX(face->uMin);
        float right = VoxelX(face->uMax);
        float z = VoxelZ(face->plane);
        float bottom = VoxelY(face->vMin);
        float top = VoxelY(face->vMax);

        positions[0][0] = left;  positions[0][1] = top;    positions[0][2] = z;
        positions[1][0] = right; positions[1][1] = top;    positions[1][2] = z;
        positions[2][0] = right; positions[2][1] = bottom; positions[2][2] = z;
        positions[3][0] = left;  positions[3][1] = bottom; positions[3][2] = z;
    }
}

static void FaceMaterial(const struct DioramaTerrainMaterial *source,
                         const struct DioramaShellFace *face,
                         struct DioramaTerrainMaterial *output)
{
    const float inset = 0.02f / DIORAMA_VOXELS_PER_CELL;
    float uMin = 0.0f;
    float uMax = 1.0f;
    float vMin = 0.0f;
    float vMax = 1.0f;
    int32_t sourcePixelX = face->sourceCellX * DIORAMA_VOXELS_PER_CELL;
    int32_t sourcePixelZ = face->sourceCellY * DIORAMA_VOXELS_PER_CELL;

    *output = *source;
    if (face->axis == 1)
    {
        uMin = (face->uMin - sourcePixelX) / (float)DIORAMA_VOXELS_PER_CELL;
        uMax = (face->uMax - sourcePixelX) / (float)DIORAMA_VOXELS_PER_CELL;
        vMin = (face->vMin - sourcePixelZ) / (float)DIORAMA_VOXELS_PER_CELL;
        vMax = (face->vMax - sourcePixelZ) / (float)DIORAMA_VOXELS_PER_CELL;
    }
    else
    {
        int32_t source = face->axis == 0 ? sourcePixelZ : sourcePixelX;
        uMin = (face->uMin - source) / (float)DIORAMA_VOXELS_PER_CELL;
        uMax = (face->uMax - source) / (float)DIORAMA_VOXELS_PER_CELL;
    }
    output->u0 = source->u0 + (source->u1 - source->u0) * (uMin + inset);
    output->u1 = source->u0 + (source->u1 - source->u0) * (uMax - inset);
    output->v0 = source->v0 + (source->v1 - source->v0) * (vMin + inset);
    output->v1 = source->v0 + (source->v1 - source->v0) * (vMax - inset);
}

bool DioramaTerrain_BuildChunk(const struct DioramaTerrainChunkInput *input,
                               struct DioramaTerrainVertex *vertices,
                               uint32_t vertexCapacity,
                               struct DioramaTerrainMesh *mesh)
{
    uint32_t vertexCount = 0;
    uint32_t spanCount;
    uint32_t shellFaceCount;
    uint32_t rawShellFaceCount;
    uint32_t faceIndex;
    int32_t ownerMinX = input->chunkX * DIORAMA_TERRAIN_CHUNK_SIZE * DIORAMA_VOXELS_PER_CELL;
    int32_t ownerMinZ = input->chunkY * DIORAMA_TERRAIN_CHUNK_SIZE * DIORAMA_VOXELS_PER_CELL;
    int gridY;
    int gridX;

    memset(mesh, 0, sizeof(*mesh));
    mesh->bounds.minX = FLT_MAX;
    mesh->bounds.minY = FLT_MAX;
    mesh->bounds.minZ = FLT_MAX;
    mesh->bounds.maxX = -FLT_MAX;
    mesh->bounds.maxY = -FLT_MAX;
    mesh->bounds.maxZ = -FLT_MAX;
    for (gridY = DIORAMA_TERRAIN_HALO;
         gridY < DIORAMA_TERRAIN_HALO + DIORAMA_TERRAIN_CHUNK_SIZE; gridY++)
        for (gridX = DIORAMA_TERRAIN_HALO;
             gridX < DIORAMA_TERRAIN_HALO + DIORAMA_TERRAIN_CHUNK_SIZE; gridX++)
        {
            const struct DioramaTerrainCell *cell = GetCell(input, gridX, gridY);
            uint8_t corners;

            if (!cell->present || cell->shape != DIORAMA_SHAPE_CLIFF)
                continue;
            corners = cell->cliffCornerMask;
            while (corners != 0)
            {
                mesh->cliffCornerCount += corners & 1;
                corners >>= 1;
            }
        }

    if (BuildCompressedOccupancyShell(input, &spanCount, &shellFaceCount,
                                      &rawShellFaceCount))
        mesh->usedCompressedOccupancy = true;
    else
    {
        if (!BuildOccupancy(input, &spanCount)
         || !DioramaOccupancy_BuildShell(sOccupancySpans, spanCount,
                ownerMinX, ownerMinZ,
                ownerMinX + DIORAMA_TERRAIN_CHUNK_SIZE * DIORAMA_VOXELS_PER_CELL,
                ownerMinZ + DIORAMA_TERRAIN_CHUNK_SIZE * DIORAMA_VOXELS_PER_CELL,
                sShellFaces, DIORAMA_TERRAIN_MAX_FACES, &shellFaceCount))
            return false;
        rawShellFaceCount = shellFaceCount;
        if (!AppendFallbackLedgeFaces(input, &shellFaceCount, &rawShellFaceCount))
            return false;
    }
    mesh->occupancySpanCount = spanCount;
    mesh->shellFaceCount = rawShellFaceCount;
    shellFaceCount = DioramaOccupancy_MergeFaces(sShellFaces, shellFaceCount);
    for (faceIndex = 0; faceIndex < shellFaceCount; faceIndex++)
    {
        const struct DioramaShellFace *face = &sShellFaces[faceIndex];
        const struct DioramaTerrainCell *cell = FaceSourceCell(input, face);
        unsigned materialIndex = face->material - 1;
        unsigned materialFace = materialIndex % DIORAMA_MATERIAL_FACE_COUNT;
        unsigned surface = (materialIndex / DIORAMA_MATERIAL_FACE_COUNT)
                         % DIORAMA_TERRAIN_MAX_SURFACES;
        const struct DioramaTerrainMaterial *sourceMaterial;
        struct DioramaTerrainMaterial material;
        struct DioramaShellFace band = *face;
        int16_t bandEnd = face->vMax;

        if (cell == NULL || materialFace >= DIORAMA_MATERIAL_FACE_COUNT)
            return false;
        sourceMaterial = surface == 0 ? &cell->materials[materialFace]
                                      : &cell->underlayMaterials[materialFace];
        if (sourceMaterial->layer == DIORAMA_MATERIAL_NONE)
            continue;
        do
        {
            float positions[4][3];
            float shade;
            int corner;

            if (face->axis != 1 && band.vMin + DIORAMA_VOXELS_PER_CELL < bandEnd)
                band.vMax = band.vMin + DIORAMA_VOXELS_PER_CELL;
            else
                band.vMax = bandEnd;
            FacePositions(&band, positions);
            FaceMaterial(sourceMaterial, &band, &material);
            if (cell->shape == DIORAMA_SHAPE_LEDGE && face->axis != 1)
            {
                float sourceU0 = sourceMaterial->u0;
                float sourceV0 = sourceMaterial->v0;
                float sourceWidth = sourceMaterial->u1 - sourceMaterial->u0;
                float sourceHeight = sourceMaterial->v1 - sourceMaterial->v0;
                int32_t sourcePixel = face->axis == 0
                                    ? face->sourceCellY * DIORAMA_VOXELS_PER_CELL
                                    : face->sourceCellX * DIORAMA_VOXELS_PER_CELL;
                int sourceY0 = DIORAMA_VOXELS_PER_CELL - face->vMax;
                int sourceY1 = DIORAMA_VOXELS_PER_CELL - face->vMin - 1;
                int sourceX0 = face->uMin - sourcePixel;
                int sourceX1 = face->uMax - sourcePixel - 1;

                material.u0 = sourceU0 + sourceWidth * sourceX0
                            / (DIORAMA_VOXELS_PER_CELL - 1);
                material.u1 = sourceU0 + sourceWidth * sourceX1
                            / (DIORAMA_VOXELS_PER_CELL - 1);
                material.v0 = sourceV0 + sourceHeight * sourceY0
                            / (DIORAMA_VOXELS_PER_CELL - 1);
                material.v1 = sourceV0 + sourceHeight * sourceY1
                            / (DIORAMA_VOXELS_PER_CELL - 1);
            }
            shade = face->axis == 1 ? 1.0f
                  : (face->axis == 2 && face->sign < 0 ? 0.82f : 0.68f);
            if (!AppendQuad(vertices, vertexCapacity, &vertexCount, positions, &material, shade,
                            face->axis == 1 && face->sign > 0 && (face->flags & 1)
                                ? 1.0f : 0.0f))
                return false;
            if (face->axis == 1 && face->sign > 0)
                mesh->topFaceCount++;
            else if (face->axis == 1)
                mesh->bottomFaceCount++;
            else if (cell->shape == DIORAMA_SHAPE_LEDGE && face->vMax > 0)
                mesh->featureFaceCount++;
            else
                mesh->sideFaceCount++;
            if (cell->shape == DIORAMA_SHAPE_CLIFF && face->axis != 1
             && materialFace >= DIORAMA_MATERIAL_FACE_NORTH
             && materialFace <= DIORAMA_MATERIAL_FACE_WEST)
            {
                uint8_t edge = 1u << (materialFace - DIORAMA_MATERIAL_FACE_NORTH);

                if ((cell->cliffBaseMask & edge)
                 && face->vMin <= HeightToVoxel(cell->groundHeight))
                    mesh->cliffBaseFaceCount++;
                if (cell->cliffTransitionMask & edge)
                    mesh->cliffTransitionFaceCount++;
            }
            for (corner = 0; corner < 4; corner++)
            {
                if (positions[corner][0] < mesh->bounds.minX) mesh->bounds.minX = positions[corner][0];
                if (positions[corner][0] > mesh->bounds.maxX) mesh->bounds.maxX = positions[corner][0];
                if (positions[corner][1] < mesh->bounds.minY) mesh->bounds.minY = positions[corner][1];
                if (positions[corner][1] > mesh->bounds.maxY) mesh->bounds.maxY = positions[corner][1];
                if (positions[corner][2] < mesh->bounds.minZ) mesh->bounds.minZ = positions[corner][2];
                if (positions[corner][2] > mesh->bounds.maxZ) mesh->bounds.maxZ = positions[corner][2];
            }
            band.vMin = band.vMax;
        } while (face->axis != 1 && band.vMin < bandEnd);
    }

    mesh->vertexCount = vertexCount;
    mesh->faceCount = mesh->topFaceCount + mesh->bottomFaceCount
                    + mesh->sideFaceCount + mesh->featureFaceCount;
    if (mesh->faceCount == 0)
        memset(&mesh->bounds, 0, sizeof(mesh->bounds));
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
        mesh->geometryHash = HashFloat(mesh->geometryHash, vertices[i].reflectionMask);
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
