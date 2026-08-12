#ifdef ENABLE_DIORAMA

#include <float.h>
#include <math.h>
#include <string.h>

#include "constants/metatile_behaviors.h"
#include "diorama/rules.generated.h"
#include "diorama/terrain_mesh.h"
#include "diorama/tree_model.generated.h"

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
                          float reflectionMask, uint32_t color)
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
    vertex->color = color;
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
                          u, v, shade, material->layer, reflectionMask,
                          UINT32_C(0xFFFFFFFF)))
            return false;
    }
    return true;
}

static float ResolveRoofHeight(const struct DioramaTerrainCell *cell,
                               int pixelX, int pixelZ)
{
    float factor;
    float coordinate;
    float length;

    if (cell->structureId == 0 || cell->measuredRunLength == 0
     || (cell->profile != DIORAMA_ROOF_GABLE_X
      && cell->profile != DIORAMA_ROOF_GABLE_Z))
        return cell->visualHeight;
    coordinate = cell->measuredRunLocal
               + ((cell->measuredAxis == DIORAMA_PLANE_AXIS_Z ? pixelZ : pixelX) + 0.5f)
               / DIORAMA_VOXELS_PER_CELL;
    length = cell->measuredRunLength;
    factor = 1.0f - fabsf(2.0f * coordinate / length - 1.0f);
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

int16_t DioramaTerrain_VisibleStructureOrigin(int16_t mapCoordinate,
                                              uint8_t structureLocalCoordinate)
{
    return mapCoordinate - structureLocalCoordinate;
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

void DioramaTerrain_BuildTerraceHeightmap(uint8_t profile,
                                          const uint16_t artworkRows[DIORAMA_VOXELS_PER_CELL],
                                          bool useArtwork,
                                          uint16_t rows[DIORAMA_VOXELS_PER_CELL],
                                          uint8_t heights[DIORAMA_VOXELS_PER_CELL
                                                          * DIORAMA_VOXELS_PER_CELL])
{
    int row;

    memset(heights, 0,
           DIORAMA_VOXELS_PER_CELL * DIORAMA_VOXELS_PER_CELL);
    for (row = 0; row < DIORAMA_VOXELS_PER_CELL; row++)
    {
        int x;
        uint16_t artwork = artworkRows[row];
        int artworkStart = 2;

        if (useArtwork)
        {
            artworkStart = 0;
            while (artworkStart < DIORAMA_VOXELS_PER_CELL
                && !(artwork & (1u << artworkStart)))
                artworkStart++;
        }
        if (row == 0 || row == DIORAMA_VOXELS_PER_CELL - 1)
            artworkStart = 2;

        rows[row] = 0;
        for (x = 0; x < DIORAMA_VOXELS_PER_CELL; x++)
        {
            int eastHeight;
            int northHeight = (DIORAMA_VOXELS_PER_CELL - 1 - row)
                            * DIORAMA_VOXELS_PER_CELL
                            / (DIORAMA_VOXELS_PER_CELL - 1);
            int height;

            if (x >= artworkStart)
            {
                int rise = x - artworkStart + 1;

                eastHeight = rise >= 6 ? DIORAMA_VOXELS_PER_CELL
                                       : rise * DIORAMA_VOXELS_PER_CELL / 6;
            }
            else
                eastHeight = 0;
            if (eastHeight > 0 && eastHeight < DIORAMA_VOXELS_PER_CELL)
            {
                eastHeight = (eastHeight * eastHeight
                            * (24 - eastHeight) + 64) / 128;
                if (eastHeight < 1)
                    eastHeight = 1;
                if (eastHeight >= DIORAMA_VOXELS_PER_CELL)
                    eastHeight = DIORAMA_VOXELS_PER_CELL - 1;
            }
            if (northHeight > 0 && northHeight < DIORAMA_VOXELS_PER_CELL)
            {
                northHeight = (northHeight * northHeight
                             * (24 - northHeight) + 64) / 128;
                if (northHeight < 1)
                    northHeight = 1;
                if (northHeight >= DIORAMA_VOXELS_PER_CELL)
                    northHeight = DIORAMA_VOXELS_PER_CELL - 1;
            }
            if (profile == DIORAMA_TERRACE_HORIZONTAL)
                height = northHeight;
            else if (profile == DIORAMA_TERRACE_VERTICAL)
                height = eastHeight;
            else if (profile == DIORAMA_TERRACE_INNER)
                height = (eastHeight * northHeight
                        + DIORAMA_VOXELS_PER_CELL - 1)
                       / DIORAMA_VOXELS_PER_CELL;
            else if (profile == DIORAMA_TERRACE_OUTER)
                height = DIORAMA_VOXELS_PER_CELL
                       - ((DIORAMA_VOXELS_PER_CELL - eastHeight)
                        * (DIORAMA_VOXELS_PER_CELL - northHeight)
                        + DIORAMA_VOXELS_PER_CELL - 1)
                       / DIORAMA_VOXELS_PER_CELL;
            else
                height = 0;
            if (height > DIORAMA_VOXELS_PER_CELL)
                height = DIORAMA_VOXELS_PER_CELL;

            if (height > 0)
            {
                rows[row] |= 1u << x;
                heights[row * DIORAMA_VOXELS_PER_CELL + x] = height;
            }
        }
    }
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
        hash = HashByte(hash, cell->treeHullReady);
        hash = HashByte(hash, cell->treeMaskDirect);
        hash = HashByte(hash, cell->structureRoofRows);
        hash = HashByte(hash, cell->structureLocalX);
        hash = HashByte(hash, cell->structureLocalY);
        hash = HashByte(hash, cell->southFacadeCount);
        hash = HashByte(hash, cell->measuredAxis);
        hash = HashU16(hash, cell->measuredExtentBands);
        hash = HashByte(hash, cell->measuredPeriodBands);
        hash = HashByte(hash, cell->measuredRoofBands);
        hash = HashU16(hash, cell->measuredRunLocal);
        hash = HashU16(hash, cell->measuredRunLength);
        hash = HashByte(hash, cell->measuredFlags);
        hash = HashByte(hash, cell->measuredBandCount);
        hash = HashByte(hash, cell->cliffEdgeMask);
        hash = HashByte(hash, cell->cliffBaseMask);
        hash = HashByte(hash, cell->cliffTransitionMask);
        hash = HashByte(hash, cell->cliffCornerMask);
        hash = HashByte(hash, cell->terraceProfile);
        hash = HashFloat(hash, cell->groundHeight);
        hash = HashFloat(hash, cell->visualHeight);
        hash = HashFloat(hash, cell->featureHeight);
        hash = HashFloat(hash, cell->structureBodyHeight);
        hash = HashFloat(hash, cell->structureRoofHeight);
        hash = HashFloat(hash, cell->southFacadeUnitHeight);
        hash = HashFloat(hash, cell->measuredConfidence);
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
        if (cell->measuredFlags & DIORAMA_MEASURED_MOUNTAIN_ART)
            for (unsigned pixel = 0;
                 pixel < DIORAMA_VOXELS_PER_CELL * DIORAMA_VOXELS_PER_CELL; pixel++)
                hash = HashByte(hash, cell->mountainHeight[pixel]);
        if (cell->archetype == DIORAMA_ARCHETYPE_ROUND_HULL
         || cell->archetype == DIORAMA_ARCHETYPE_GROUPED_HULL)
        {
            hash = HashU16(hash, cell->treeMaterial.metatileId);
            hash = HashByte(hash, cell->treeMaterial.layer);
            hash = HashFloat(hash, cell->treeMaterial.u0);
            hash = HashFloat(hash, cell->treeMaterial.v0);
            hash = HashFloat(hash, cell->treeMaterial.u1);
            hash = HashFloat(hash, cell->treeMaterial.v1);
            for (unsigned pixel = 0;
                 pixel < DIORAMA_VOXELS_PER_CELL * DIORAMA_VOXELS_PER_CELL; pixel++)
                hash = HashByte(hash, cell->treeShade[pixel]);
        }
        for (unsigned row = 0; row < cell->southFacadeCount; row++)
        {
            hash = HashU16(hash, cell->southFacadeMaterials[row].metatileId);
            hash = HashByte(hash, cell->southFacadeMaterials[row].layer);
        }
        for (unsigned band = 0; band < cell->measuredBandCount; band++)
        {
            hash = HashU16(hash, cell->measuredBands[band].metatileId);
            hash = HashByte(hash, cell->measuredBands[band].layer);
            hash = HashFloat(hash, cell->measuredBands[band].u0);
            hash = HashFloat(hash, cell->measuredBands[band].v0);
            hash = HashFloat(hash, cell->measuredBands[band].u1);
            hash = HashFloat(hash, cell->measuredBands[band].v1);
        }
    }
    hash = HashU32(hash, input->pixelCount);
    for (i = 0; i < input->pixelCount && i < DIORAMA_TERRAIN_MAX_PIXEL_PRIMITIVES; i++)
    {
        const struct DioramaTerrainPixelPrimitive *pixel = &input->pixels[i];

        hash = HashU32(hash, pixel->xQ32);
        hash = HashU32(hash, pixel->yQ32);
        hash = HashU32(hash, pixel->zQ32);
        hash = HashU32(hash, pixel->rgba);
        hash = HashU32(hash, pixel->sourceCellOffset);
        hash = HashU16(hash, pixel->objectId);
        hash = HashU16(hash, pixel->structureId);
        hash = HashU16(hash, pixel->expectedMetatile);
        hash = HashU16(hash, pixel->expectedTileEntry);
        hash = HashByte(hash, pixel->sizeXQ32);
        hash = HashByte(hash, pixel->sizeYQ32);
        hash = HashByte(hash, pixel->sizeZQ32);
        hash = HashByte(hash, pixel->sourceLayer);
        hash = HashByte(hash, pixel->sourceX);
        hash = HashByte(hash, pixel->sourceY);
        hash = HashByte(hash, pixel->kind);
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
                    int16_t ground = HeightToVoxel(cell->groundHeight);

                    top = HeightToVoxel(topHeight);

                    if (((cell->archetype == DIORAMA_ARCHETYPE_GROUPED_HULL
                       || cell->archetype == DIORAMA_ARCHETYPE_ROUND_HULL)
                      && cell->treeHullReady)
                     || (cell->archetype == DIORAMA_ARCHETYPE_GROUPED_HULL
                      && cell->structureWidth == 2))
                    {
                        if (!AppendSpan(&count, cell, cellIndex, pixelX, pixelY,
                                        1, ground - 1, ground))
                            return false;
                        continue;
                    }
                    if (cell->measuredFlags & DIORAMA_MEASURED_MOUNTAIN_ART)
                    {
                        uint8_t mountainHeight = cell->mountainHeight[
                            pixelY * DIORAMA_VOXELS_PER_CELL + pixelX];
                        bool raised = mountainHeight != 0;
                        int16_t mountainTop = ground + mountainHeight;

                        if (!AppendSpan(&count, cell, cellIndex, pixelX, pixelY,
                                        raised ? 0 : 1,
                                        ground - 1,
                                        raised ? mountainTop : ground))
                            return false;
                        continue;
                    }
                    if (!DioramaRules_ProfileOccupies(cell->semanticProfile, pixelX, pixelY))
                        continue;
                    if ((cell->measuredFlags & DIORAMA_MEASURED_SILHOUETTE)
                     && !(cell->foregroundAlpha[pixelY] & (1u << pixelX)))
                        continue;
                    if (cell->shape == DIORAMA_SHAPE_ROOF && cell->structureId != 0)
                    {
                        topHeight = ResolveRoofHeight(cell, pixelX, pixelY);
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
                                        0, -1, ledgeTop))
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
                  || !DioramaRules_ProfileIsFullCell(cell->semanticProfile)
                   || (cell->measuredFlags & (DIORAMA_MEASURED_SILHOUETTE
                                            | DIORAMA_MEASURED_MOUNTAIN_ART))
                  || cell->archetype == DIORAMA_ARCHETYPE_ROUND_HULL
                  || cell->archetype == DIORAMA_ARCHETYPE_GROUPED_HULL)
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

#define TREE_HULL_MAX_SIZE (DIORAMA_VOXELS_PER_CELL * 2)
#define TREE_HULL_MAX_PIXELS (TREE_HULL_MAX_SIZE * TREE_HULL_MAX_SIZE)

struct TreeHull
{
    int width;
    int height;
    int depth;
    int originX;
    int originZ;
    int16_t ground;
    bool directMask;
    const struct DioramaTerrainCell *sourceCells[4];
    uint8_t shade[TREE_HULL_MAX_PIXELS];
    uint8_t mask[TREE_HULL_MAX_PIXELS];
    uint8_t outside[TREE_HULL_MAX_PIXELS];
    uint8_t selected[TREE_HULL_MAX_PIXELS];
    uint8_t sourceRow[TREE_HULL_MAX_PIXELS];
    int8_t zMin[TREE_HULL_MAX_PIXELS];
    int8_t zMax[TREE_HULL_MAX_PIXELS];
    uint16_t queue[TREE_HULL_MAX_PIXELS];
};

static const struct DioramaTerrainCell *FindInputCell(
    const struct DioramaTerrainChunkInput *input, int mapX, int mapY)
{
    int x = mapX - input->chunkX * DIORAMA_TERRAIN_CHUNK_SIZE
          + DIORAMA_TERRAIN_HALO;
    int y = mapY - input->chunkY * DIORAMA_TERRAIN_CHUNK_SIZE
          + DIORAMA_TERRAIN_HALO;
    const struct DioramaTerrainCell *cell;

    if (x < 0 || y < 0 || x >= DIORAMA_TERRAIN_INPUT_SIZE
     || y >= DIORAMA_TERRAIN_INPUT_SIZE)
        return NULL;
    cell = GetCell(input, x, y);
    if (!cell->present || cell->mapX != mapX || cell->mapY != mapY)
        return NULL;
    return cell;
}

static bool TreeShadeIsPassable(uint8_t shade, bool ditherFallback)
{
    if (ditherFallback)
        return shade == DIORAMA_TREE_SHADE_OFF
            || shade == DIORAMA_TREE_SHADE_LIGHT
            || shade == DIORAMA_TREE_SHADE_WHITE;
    return shade != DIORAMA_TREE_SHADE_BLACK;
}

static void FloodTreeOutside(struct TreeHull *hull, bool ditherFallback)
{
    int readIndex = 0;
    int writeIndex = 0;
    int x;
    int y;

    memset(hull->outside, 0, sizeof(hull->outside));
#define SEED_TREE_PIXEL(index) do { \
    int seedIndex = (index); \
    if (!hull->outside[seedIndex] \
     && TreeShadeIsPassable(hull->shade[seedIndex], ditherFallback)) \
    { \
        hull->outside[seedIndex] = 1; \
        hull->queue[writeIndex++] = seedIndex; \
    } \
} while (0)
    for (x = 0; x < hull->width; x++)
    {
        SEED_TREE_PIXEL(x);
        SEED_TREE_PIXEL((hull->height - 1) * hull->width + x);
    }
    for (y = 0; y < hull->height; y++)
    {
        SEED_TREE_PIXEL(y * hull->width);
        SEED_TREE_PIXEL(y * hull->width + hull->width - 1);
    }
    while (readIndex < writeIndex)
    {
        int index = hull->queue[readIndex++];
        int pixelX = index % hull->width;
        int neighbors[4] = {
            pixelX > 0 ? index - 1 : -1,
            pixelX + 1 < hull->width ? index + 1 : -1,
            index >= hull->width ? index - hull->width : -1,
            index + hull->width < hull->width * hull->height
                ? index + hull->width : -1,
        };

        for (int side = 0; side < 4; side++)
        {
            int neighbor = neighbors[side];

            if (neighbor >= 0 && !hull->outside[neighbor]
             && TreeShadeIsPassable(hull->shade[neighbor], ditherFallback))
            {
                hull->outside[neighbor] = 1;
                hull->queue[writeIndex++] = neighbor;
            }
        }
    }
#undef SEED_TREE_PIXEL
}

static bool KeepLargestUpperTreeComponent(struct TreeHull *hull)
{
    int pixelCount = hull->width * hull->height;
    int bestSize = 0;

    memset(hull->outside, 0, sizeof(hull->outside));
    memset(hull->selected, 0, sizeof(hull->selected));
    for (int start = 0; start < pixelCount; start++)
    {
        int readIndex = 0;
        int writeIndex = 0;
        bool upper = false;

        if (!hull->mask[start] || hull->outside[start])
            continue;
        hull->outside[start] = 1;
        hull->queue[writeIndex++] = start;
        while (readIndex < writeIndex)
        {
            int index = hull->queue[readIndex++];
            int x = index % hull->width;
            int neighbors[4] = {
                x > 0 ? index - 1 : -1,
                x + 1 < hull->width ? index + 1 : -1,
                index >= hull->width ? index - hull->width : -1,
                index + hull->width < pixelCount ? index + hull->width : -1,
            };

            upper |= index / hull->width < hull->height / 2;
            for (int side = 0; side < 4; side++)
            {
                int neighbor = neighbors[side];

                if (neighbor >= 0 && hull->mask[neighbor] && !hull->outside[neighbor])
                {
                    hull->outside[neighbor] = 1;
                    hull->queue[writeIndex++] = neighbor;
                }
            }
        }
        if (upper && writeIndex > bestSize)
        {
            memset(hull->selected, 0, sizeof(hull->selected));
            for (int index = 0; index < writeIndex; index++)
                hull->selected[hull->queue[index]] = 1;
            bestSize = writeIndex;
        }
    }
    memcpy(hull->mask, hull->selected, sizeof(hull->mask));
    return bestSize != 0;
}

static bool BuildTreeSilhouette(struct TreeHull *hull)
{
    int enclosed = 0;
    int any = 0;
    int bottomRow = -1;
    int pixelCount = hull->width * hull->height;
    int y;

    if (hull->directMask)
    {
        for (int index = 0; index < pixelCount; index++)
            hull->mask[index] = hull->shade[index] != DIORAMA_TREE_SHADE_OFF;
        if (!KeepLargestUpperTreeComponent(hull))
            return false;
    }
    else
    {
        FloodTreeOutside(hull, false);
        for (int index = 0; index < pixelCount; index++)
        {
            hull->mask[index] = !hull->outside[index];
            enclosed += hull->mask[index]
                     && hull->shade[index] != DIORAMA_TREE_SHADE_BLACK;
        }
        if (enclosed < pixelCount / 8)
        {
            FloodTreeOutside(hull, true);
            for (int index = 0; index < pixelCount; index++)
                hull->mask[index] = !hull->outside[index]
                                 && hull->shade[index] != DIORAMA_TREE_SHADE_OFF;
        }
    }
    memset(hull->zMin, -1, sizeof(hull->zMin));
    memset(hull->zMax, -1, sizeof(hull->zMax));
    memset(hull->sourceRow, 0, sizeof(hull->sourceRow));
    for (y = 0; y < hull->height; y++)
    {
        int low = -1;
        int high = -1;

        for (int x = 0; x < hull->width; x++)
            if (hull->mask[y * hull->width + x])
            {
                if (low < 0)
                    low = x;
                high = x;
            }
        if (low < 0)
            continue;
        bottomRow = y;
        any = 1;
        for (int x = low; x <= high; x++)
        {
            int index = y * hull->width + x;
            float center;
            float halfWidth;
            float dx;
            int chord = 1;

            if (!hull->mask[index])
                continue;
            center = (low + high + 1) * 0.5f;
            halfWidth = (high - low + 1) * 0.5f;
            dx = x + 0.5f - center;
            if (halfWidth * halfWidth > dx * dx)
                chord = (int)floorf(2.0f * sqrtf(halfWidth * halfWidth - dx * dx) + 0.5f);
            if (chord < 1)
                chord = 1;
            hull->zMin[index] = (int8_t)floorf(hull->depth * 0.5f - chord * 0.5f + 0.5f);
            hull->zMax[index] = hull->zMin[index] + chord;
            hull->sourceRow[index] = y;
        }
    }
    if (!any)
        return false;
    for (y = bottomRow + 1; y < hull->height; y++)
        for (int x = 0; x < hull->width; x++)
        {
            int source = bottomRow * hull->width + x;
            int target = y * hull->width + x;

            if (hull->zMin[source] < 0)
                continue;
            hull->zMin[target] = hull->zMin[source];
            hull->zMax[target] = hull->zMax[source];
            hull->sourceRow[target] = bottomRow;
        }
    return true;
}

static bool TreeSolidAt(const struct TreeHull *hull, int x, int y, int z)
{
    int index;

    if (x < 0 || y < 0 || x >= hull->width || y >= hull->height)
        return false;
    index = y * hull->width + x;
    return hull->zMin[index] >= 0 && z >= hull->zMin[index] && z < hull->zMax[index];
}

static const struct DioramaTerrainCell *TreeSourceCell(const struct TreeHull *hull,
                                                        int pixelX, int pixelY)
{
    int cellX = pixelX / DIORAMA_VOXELS_PER_CELL;
    int cellY = pixelY / DIORAMA_VOXELS_PER_CELL;
    int cellsWide = hull->width / DIORAMA_VOXELS_PER_CELL;

    return hull->sourceCells[cellY * cellsWide + cellX];
}

static struct DioramaTerrainMaterial TreePixelMaterial(const struct TreeHull *hull,
                                                        int pixelX0, int pixelX1,
                                                        int pixelY)
{
    const struct DioramaTerrainCell *cell = TreeSourceCell(hull, pixelX0, pixelY);
    struct DioramaTerrainMaterial material = cell->treeMaterial;
    int localX0 = pixelX0 % DIORAMA_VOXELS_PER_CELL;
    int localX1 = (pixelX1 - 1) % DIORAMA_VOXELS_PER_CELL;
    int localY = pixelY % DIORAMA_VOXELS_PER_CELL;
    float v = material.v0 + (material.v1 - material.v0)
            * localY / (DIORAMA_VOXELS_PER_CELL - 1);
    float originalU0 = material.u0;
    float originalU1 = material.u1;

    material.u0 = originalU0 + (originalU1 - originalU0)
                * localX0 / (DIORAMA_VOXELS_PER_CELL - 1);
    material.u1 = originalU0 + (originalU1 - originalU0)
                * localX1 / (DIORAMA_VOXELS_PER_CELL - 1);
    material.v0 = material.v1 = v;
    material.rotation = 0;
    material.flags = 0;
    return material;
}

static void TreeSideSource(const struct TreeHull *hull, int x, int y,
                           int *sourceX, int *sourceY)
{
    int row = hull->sourceRow[y * hull->width + x];
    int low = hull->width;
    int high = -1;
    int direction;

    for (int candidate = 0; candidate < hull->width; candidate++)
        if (hull->mask[row * hull->width + candidate])
        {
            if (candidate < low)
                low = candidate;
            high = candidate;
        }
    direction = x * 2 < low + high ? 1 : -1;
    for (int step = 0; step <= 3; step++)
    {
        int candidate = x + direction * step;
        int index;

        if (candidate < 0 || candidate >= hull->width)
            break;
        index = row * hull->width + candidate;
        if (!hull->mask[index])
            break;
        if (hull->shade[index] != DIORAMA_TREE_SHADE_BLACK)
        {
            *sourceX = candidate;
            *sourceY = row;
            return;
        }
    }
    *sourceX = x;
    *sourceY = row;
}

static void TreeTopSource(const struct TreeHull *hull, int x, int y,
                          int *sourceX, int *sourceY)
{
    for (int row = y + 2; row <= y + 4 && row < hull->height; row++)
    {
        int index = row * hull->width + x;

        if (hull->mask[index] && hull->shade[index] != DIORAMA_TREE_SHADE_BLACK)
        {
            *sourceX = x;
            *sourceY = row;
            return;
        }
    }
    *sourceX = x;
    *sourceY = hull->sourceRow[y * hull->width + x];
}

static bool AppendTreeFace(const struct DioramaTerrainChunkInput *input,
                           struct DioramaTerrainVertex *vertices, uint32_t capacity,
                           uint32_t *vertexCount, struct DioramaTerrainMesh *mesh,
                           const struct TreeHull *hull, int ownerX, int ownerZ,
                           int axis, int sign, int plane, int uMin, int uMax,
                           int16_t vMin, int16_t vMax,
                           int sourceX0, int sourceX1, int sourceY, float shade)
{
    int ownerMinX = input->chunkX * DIORAMA_TERRAIN_CHUNK_SIZE * DIORAMA_VOXELS_PER_CELL;
    int ownerMinZ = input->chunkY * DIORAMA_TERRAIN_CHUNK_SIZE * DIORAMA_VOXELS_PER_CELL;
    struct DioramaShellFace face = {
        .plane = plane, .uMin = uMin, .uMax = uMax, .vMin = vMin, .vMax = vMax,
        .axis = axis, .sign = sign,
    };
    struct DioramaTerrainMaterial material;
    float positions[4][3];

    if (ownerX < ownerMinX
     || ownerX >= ownerMinX + DIORAMA_TERRAIN_CHUNK_SIZE * DIORAMA_VOXELS_PER_CELL
     || ownerZ < ownerMinZ
     || ownerZ >= ownerMinZ + DIORAMA_TERRAIN_CHUNK_SIZE * DIORAMA_VOXELS_PER_CELL)
        return true;
    material = TreePixelMaterial(hull, sourceX0, sourceX1, sourceY);
    if (material.layer == DIORAMA_MATERIAL_NONE)
        return true;
    FacePositions(&face, positions);
    if (!AppendQuad(vertices, capacity, vertexCount, positions, &material, shade, 0.0f))
        return false;
    mesh->shellFaceCount++;
    if (axis == 1 && sign > 0)
        mesh->topFaceCount++;
    else if (axis == 1)
        mesh->bottomFaceCount++;
    else
        mesh->sideFaceCount++;
    for (int corner = 0; corner < 4; corner++)
    {
        if (positions[corner][0] < mesh->bounds.minX) mesh->bounds.minX = positions[corner][0];
        if (positions[corner][0] > mesh->bounds.maxX) mesh->bounds.maxX = positions[corner][0];
        if (positions[corner][1] < mesh->bounds.minY) mesh->bounds.minY = positions[corner][1];
        if (positions[corner][1] > mesh->bounds.maxY) mesh->bounds.maxY = positions[corner][1];
        if (positions[corner][2] < mesh->bounds.minZ) mesh->bounds.minZ = positions[corner][2];
        if (positions[corner][2] > mesh->bounds.maxZ) mesh->bounds.maxZ = positions[corner][2];
    }
    return true;
}

static bool AppendTreeHull(const struct DioramaTerrainChunkInput *input,
                           struct DioramaTerrainVertex *vertices, uint32_t capacity,
                           uint32_t *vertexCount, struct DioramaTerrainMesh *mesh,
                           struct TreeHull *hull)
{
    if (!BuildTreeSilhouette(hull))
        return true;
    for (int y = 0; y < hull->height; y++)
    {
        int x = 0;

        while (x < hull->width)
        {
            int index = y * hull->width + x;
            int end = x + 1;
            int sourceY;
            int worldY;

            if (hull->zMin[index] < 0)
            {
                x++;
                continue;
            }
            sourceY = hull->sourceRow[index];
            while (end < hull->width
                && end / DIORAMA_VOXELS_PER_CELL == x / DIORAMA_VOXELS_PER_CELL
                && hull->zMin[y * hull->width + end] == hull->zMin[index]
                && hull->zMax[y * hull->width + end] == hull->zMax[index]
                && hull->sourceRow[y * hull->width + end] == sourceY)
                end++;
            worldY = hull->ground + hull->height - 1 - y;
            if (!AppendTreeFace(input, vertices, capacity, vertexCount, mesh, hull,
                    hull->originX + x, hull->originZ + hull->zMin[index],
                    2, -1, hull->originZ + hull->zMin[index],
                    hull->originX + x, hull->originX + end, worldY, worldY + 1,
                    x, end, sourceY, 0.68f)
             || !AppendTreeFace(input, vertices, capacity, vertexCount, mesh, hull,
                    hull->originX + x, hull->originZ + hull->zMax[index] - 1,
                    2, 1, hull->originZ + hull->zMax[index],
                    hull->originX + x, hull->originX + end, worldY, worldY + 1,
                    x, end, sourceY, 1.0f))
                return false;
            x = end;
        }
        for (int x = 0; x < hull->width; x++)
        {
            int index = y * hull->width + x;
            int sourceY;
            int worldX;
            int worldY;
            int sideX;
            int sideY;

            if (hull->zMin[index] < 0)
                continue;
            sourceY = hull->sourceRow[index];
            worldX = hull->originX + x;
            worldY = hull->ground + hull->height - 1 - y;
            TreeSideSource(hull, x, y, &sideX, &sideY);
            for (int direction = -1; direction <= 1; direction += 2)
            {
                int z = hull->zMin[index];

                while (z < hull->zMax[index])
                {
                    int start;
                    int chunk;

                    while (z < hull->zMax[index]
                        && TreeSolidAt(hull, x + direction, y, z))
                        z++;
                    start = z;
                    chunk = DioramaTerrain_FloorDiv(hull->originZ + start,
                        DIORAMA_TERRAIN_CHUNK_SIZE * DIORAMA_VOXELS_PER_CELL);
                    while (z < hull->zMax[index]
                        && !TreeSolidAt(hull, x + direction, y, z)
                        && DioramaTerrain_FloorDiv(hull->originZ + z,
                            DIORAMA_TERRAIN_CHUNK_SIZE * DIORAMA_VOXELS_PER_CELL) == chunk)
                        z++;
                    if (start < z
                     && !AppendTreeFace(input, vertices, capacity, vertexCount, mesh, hull,
                            worldX, hull->originZ + start, 0, direction,
                            worldX + (direction > 0), hull->originZ + start,
                            hull->originZ + z, worldY, worldY + 1,
                            sideX, sideX + 1, sideY, 0.78f))
                        return false;
                }
            }
            for (int direction = -1; direction <= 1; direction += 2)
            {
                int z = hull->zMin[index];
                bool wholeExposed = true;

                for (int testZ = z; wholeExposed && testZ < hull->zMax[index]; testZ++)
                    wholeExposed = !TreeSolidAt(hull, x, y + direction, testZ);
                while (z < hull->zMax[index])
                {
                    int start;
                    int sourceX = x;
                    int sourceRow = sourceY;
                    int chunk;
                    bool deepRun;

                    while (z < hull->zMax[index]
                        && TreeSolidAt(hull, x, y + direction, z))
                        z++;
                    start = z;
                    deepRun = direction < 0 && wholeExposed
                           && hull->zMax[index] - hull->zMin[index] >= 3
                           && z > hull->zMin[index] && z + 1 < hull->zMax[index];
                    if (deepRun)
                        TreeTopSource(hull, x, y, &sourceX, &sourceRow);
                    chunk = DioramaTerrain_FloorDiv(hull->originZ + start,
                        DIORAMA_TERRAIN_CHUNK_SIZE * DIORAMA_VOXELS_PER_CELL);
                    while (z < hull->zMax[index]
                        && !TreeSolidAt(hull, x, y + direction, z)
                        && DioramaTerrain_FloorDiv(hull->originZ + z,
                            DIORAMA_TERRAIN_CHUNK_SIZE * DIORAMA_VOXELS_PER_CELL) == chunk)
                    {
                        bool deep = direction < 0 && wholeExposed
                                 && hull->zMax[index] - hull->zMin[index] >= 3
                                 && z > hull->zMin[index] && z + 1 < hull->zMax[index];

                        if (deep != deepRun)
                            break;
                        z++;
                    }
                    if (start < z && (direction < 0 || y + 1 < hull->height)
                     && !AppendTreeFace(input, vertices, capacity, vertexCount, mesh, hull,
                            worldX, hull->originZ + start, 1, -direction,
                            worldY + (direction < 0), worldX, worldX + 1,
                            hull->originZ + start, hull->originZ + z,
                            sourceX, sourceX + 1, sourceRow,
                            direction < 0 ? 1.0f : 0.55f))
                        return false;
                }
            }
        }
    }
    return true;
}

static bool AppendPixelQuad(struct DioramaTerrainVertex *vertices, uint32_t capacity,
                            uint32_t *count, const float positions[4][3], float shade,
                            uint32_t color);

bool DioramaTerrain_BuildTreeModel(struct DioramaTerrainVertex *vertices,
                                   uint32_t capacity, uint32_t *vertexCount,
                                   struct DioramaTerrainBounds *bounds)
{
    const int originX = 0;
    const int originZ = (DIORAMA_VOXELS_PER_CELL * 2 - DIORAMA_TREE_MODEL_SIZE_Z) / 2;
    const int ground = -DIORAMA_TREE_MODEL_MIN_Y;

    if (vertices == NULL || vertexCount == NULL || bounds == NULL)
        return false;
    *vertexCount = 0;
    bounds->minX = bounds->minY = bounds->minZ = FLT_MAX;
    bounds->maxX = bounds->maxY = bounds->maxZ = -FLT_MAX;

    for (uint32_t index = 0; index < gDioramaTreeModelFaceCount; index++)
    {
        const struct DioramaTreeModelFace *source = &gDioramaTreeModelFaces[index];
        int minimum[3] = {0, 0, 0};
        int maximum[3] = {0, 0, 0};
        int uAxis = (source->axis + 1) % 3;
        int vAxis = (source->axis + 2) % 3;
        int xMin;
        int xMax;
        int zMin;
        int zMax;
        struct DioramaShellFace face = {0};
        float positions[4][3];
        float shade;

        minimum[source->axis] = maximum[source->axis] = source->plane;
        minimum[uAxis] = source->uMin;
        maximum[uAxis] = source->uMax;
        minimum[vAxis] = source->vMin;
        maximum[vAxis] = source->vMax;
        xMin = originX + minimum[0];
        xMax = originX + maximum[0];
        zMin = originZ + minimum[1];
        zMax = originZ + maximum[1];

        if (source->axis == 0)
        {
            face.axis = 0;
            face.plane = xMin;
            face.uMin = zMin;
            face.uMax = zMax;
            face.vMin = ground + minimum[2];
            face.vMax = ground + maximum[2];
            shade = 0.82f;
        }
        else if (source->axis == 1)
        {
            face.axis = 2;
            face.plane = zMin;
            face.uMin = xMin;
            face.uMax = xMax;
            face.vMin = ground + minimum[2];
            face.vMax = ground + maximum[2];
            shade = source->sign > 0 ? 0.72f : 1.0f;
        }
        else
        {
            face.axis = 1;
            face.sign = source->sign;
            face.plane = ground + minimum[2];
            face.uMin = xMin;
            face.uMax = xMax;
            face.vMin = zMin;
            face.vMax = zMax;
            shade = source->sign > 0 ? 1.0f : 0.58f;
        }
        FacePositions(&face, positions);
        if (!AppendPixelQuad(vertices, capacity, vertexCount, positions, shade, source->rgba))
            return false;
        for (int corner = 0; corner < 4; corner++)
        {
            if (positions[corner][0] < bounds->minX) bounds->minX = positions[corner][0];
            if (positions[corner][0] > bounds->maxX) bounds->maxX = positions[corner][0];
            if (positions[corner][1] < bounds->minY) bounds->minY = positions[corner][1];
            if (positions[corner][1] > bounds->maxY) bounds->maxY = positions[corner][1];
            if (positions[corner][2] < bounds->minZ) bounds->minZ = positions[corner][2];
            if (positions[corner][2] > bounds->maxZ) bounds->maxZ = positions[corner][2];
        }
    }
    return true;
}

static bool AppendVoxelTreeInstance(const struct DioramaTerrainChunkInput *input,
                                    const struct DioramaTerrainCell *anchor,
                                    struct DioramaTerrainMesh *mesh)
{
    int anchorY = anchor->mapY - (anchor->structureHeight == 1);
    int ownerChunkX = DioramaTerrain_FloorDiv(anchor->mapX, DIORAMA_TERRAIN_CHUNK_SIZE);
    int ownerChunkY = DioramaTerrain_FloorDiv(anchorY, DIORAMA_TERRAIN_CHUNK_SIZE);
    struct DioramaTerrainTreeInstance *instance;
    float minX;
    float maxX;
    float minY;
    float maxY;
    float minZ;
    float maxZ;

    if (ownerChunkX != input->chunkX || ownerChunkY != input->chunkY)
        return true;
    if (mesh->treeInstanceCount >= DIORAMA_TERRAIN_MAX_TREE_INSTANCES)
        return false;
    instance = &mesh->treeInstances[mesh->treeInstanceCount++];
    instance->x = anchor->mapX;
    instance->y = anchor->groundHeight;
    instance->z = -anchorY;
    minX = instance->x + (DIORAMA_TREE_MODEL_MIN_X - DIORAMA_VOXELS_PER_CELL / 2)
         / (float)DIORAMA_VOXELS_PER_CELL;
    maxX = instance->x + (DIORAMA_TREE_MODEL_MAX_X - DIORAMA_VOXELS_PER_CELL / 2)
         / (float)DIORAMA_VOXELS_PER_CELL;
    minY = instance->y;
    maxY = instance->y + (DIORAMA_TREE_MODEL_MAX_Y - DIORAMA_TREE_MODEL_MIN_Y)
         / (float)DIORAMA_VOXELS_PER_CELL;
    minZ = instance->z + (DIORAMA_VOXELS_PER_CELL / 4 - DIORAMA_TREE_MODEL_MAX_Z)
         / (float)DIORAMA_VOXELS_PER_CELL;
    maxZ = instance->z + (DIORAMA_VOXELS_PER_CELL / 4 - DIORAMA_TREE_MODEL_MIN_Z)
         / (float)DIORAMA_VOXELS_PER_CELL;
    if (minX < mesh->bounds.minX) mesh->bounds.minX = minX;
    if (maxX > mesh->bounds.maxX) mesh->bounds.maxX = maxX;
    if (minY < mesh->bounds.minY) mesh->bounds.minY = minY;
    if (maxY > mesh->bounds.maxY) mesh->bounds.maxY = maxY;
    if (minZ < mesh->bounds.minZ) mesh->bounds.minZ = minZ;
    if (maxZ > mesh->bounds.maxZ) mesh->bounds.maxZ = maxZ;
    return true;
}

static bool AssembleTreeHull(const struct DioramaTerrainChunkInput *input,
                             const struct DioramaTerrainCell *anchor,
                             struct TreeHull *hull)
{
    int cellsWide = 1;
    int cellsHigh = 1;
    int bodyStart = 0;

    memset(hull, 0, sizeof(*hull));
    if (anchor->archetype == DIORAMA_ARCHETYPE_GROUPED_HULL)
    {
        cellsWide = anchor->structureWidth;
        cellsHigh = anchor->structureHeight >= 2 ? 2 : 1;
        bodyStart = anchor->structureHeight - cellsHigh;
        if (cellsWide < 1 || cellsWide > 2 || cellsHigh > 2
         || anchor->structureLocalX != 0 || anchor->structureLocalY != bodyStart
         || anchor->structureId == 0)
            return false;
    }
    hull->width = cellsWide * DIORAMA_VOXELS_PER_CELL;
    hull->height = cellsHigh * DIORAMA_VOXELS_PER_CELL;
    hull->depth = hull->width;
    hull->originX = anchor->mapX * DIORAMA_VOXELS_PER_CELL;
    hull->originZ = anchor->mapY * DIORAMA_VOXELS_PER_CELL
                  + (hull->height - hull->depth) / 2;
    hull->ground = HeightToVoxel(anchor->groundHeight);
    hull->directMask = anchor->treeMaskDirect;
    for (int cellY = 0; cellY < cellsHigh; cellY++)
        for (int cellX = 0; cellX < cellsWide; cellX++)
        {
            const struct DioramaTerrainCell *source = FindInputCell(
                input, anchor->mapX + cellX, anchor->mapY + cellY);
            int sourceIndex = cellY * cellsWide + cellX;

            if (source == NULL || source->archetype != anchor->archetype
             || (anchor->archetype == DIORAMA_ARCHETYPE_GROUPED_HULL
              && source->structureId != anchor->structureId))
                return false;
            hull->sourceCells[sourceIndex] = source;
            for (int pixelY = 0; pixelY < DIORAMA_VOXELS_PER_CELL; pixelY++)
                for (int pixelX = 0; pixelX < DIORAMA_VOXELS_PER_CELL; pixelX++)
                    hull->shade[(cellY * DIORAMA_VOXELS_PER_CELL + pixelY) * hull->width
                              + cellX * DIORAMA_VOXELS_PER_CELL + pixelX]
                        = source->treeShade[pixelY * DIORAMA_VOXELS_PER_CELL + pixelX];
        }
    return true;
}

static bool AppendTreeHulls(const struct DioramaTerrainChunkInput *input,
                            struct DioramaTerrainVertex *vertices, uint32_t capacity,
                            uint32_t *vertexCount, struct DioramaTerrainMesh *mesh)
{
    struct TreeHull hull;

    for (int y = 0; y < DIORAMA_TERRAIN_INPUT_SIZE; y++)
        for (int x = 0; x < DIORAMA_TERRAIN_INPUT_SIZE; x++)
        {
            const struct DioramaTerrainCell *cell = GetCell(input, x, y);

            if (!cell->present
             || (cell->archetype != DIORAMA_ARCHETYPE_ROUND_HULL
              && cell->archetype != DIORAMA_ARCHETYPE_GROUPED_HULL)
             || !cell->treeHullReady)
                continue;
            if (cell->archetype == DIORAMA_ARCHETYPE_GROUPED_HULL)
            {
                int bodyRows = cell->structureHeight >= 2 ? 2 : 1;
                int bodyStart = cell->structureHeight - bodyRows;

                if (cell->structureLocalX != 0 || cell->structureLocalY != bodyStart)
                    continue;
            }
            {
                uint32_t vertexStart = *vertexCount;
                struct DioramaTerrainMesh meshBeforeHull = *mesh;
                bool appended;

                if (cell->archetype == DIORAMA_ARCHETYPE_GROUPED_HULL
                 && cell->structureWidth == 2)
                    appended = AppendVoxelTreeInstance(input, cell, mesh);
                else
                    appended = AssembleTreeHull(input, cell, &hull)
                            && AppendTreeHull(input, vertices, capacity, vertexCount, mesh, &hull);
                if (!appended)
                {
                    *vertexCount = vertexStart;
                    *mesh = meshBeforeHull;
                }
            }
        }
    return true;
}

static bool AppendPixelQuad(struct DioramaTerrainVertex *vertices, uint32_t capacity,
                            uint32_t *count, const float positions[4][3], float shade,
                            uint32_t color)
{
    static const uint8_t order[6] = {0, 1, 2, 0, 2, 3};
    int i;

    for (i = 0; i < 6; i++)
    {
        int corner = order[i];

        if (!AppendVertex(vertices, capacity, count,
                          positions[corner][0], positions[corner][1], positions[corner][2],
                          0.0f, 0.0f, shade, DIORAMA_TERRAIN_TEXTURE_VERTEX_COLOR,
                          0.0f, color))
            return false;
    }
    return true;
}

static bool AppendPixelPrimitives(const struct DioramaTerrainChunkInput *input,
                                  struct DioramaTerrainVertex *vertices,
                                  uint32_t capacity, uint32_t *count,
                                  struct DioramaTerrainMesh *mesh)
{
    uint32_t index;

    if (input->pixelCount > DIORAMA_TERRAIN_MAX_PIXEL_PRIMITIVES)
        return false;
    for (index = 0; index < input->pixelCount; index++)
    {
        const struct DioramaTerrainPixelPrimitive *pixel = &input->pixels[index];
        float left = (pixel->xQ32 - 16) / 32.0f;
        float right = (pixel->xQ32 + pixel->sizeXQ32 - 16) / 32.0f;
        float bottom = pixel->yQ32 / 32.0f;
        float top = (pixel->yQ32 + pixel->sizeYQ32) / 32.0f;
        float north = -(pixel->zQ32 - 16) / 32.0f;
        float south = -(pixel->zQ32 + pixel->sizeZQ32 - 16) / 32.0f;
        const float quads[6][4][3] = {
            {{left, top, north}, {right, top, north}, {right, top, south}, {left, top, south}},
            {{left, bottom, south}, {right, bottom, south}, {right, bottom, north}, {left, bottom, north}},
            {{left, top, north}, {right, top, north}, {right, bottom, north}, {left, bottom, north}},
            {{right, top, north}, {right, top, south}, {right, bottom, south}, {right, bottom, north}},
            {{right, top, south}, {left, top, south}, {left, bottom, south}, {right, bottom, south}},
            {{left, top, south}, {left, top, north}, {left, bottom, north}, {left, bottom, south}},
        };
        /* Both artwork faces preserve the source texel; only prism depth is shaded. */
        static const float shades[6] = {1.0f, 0.58f, 1.0f, 0.68f, 1.0f, 0.68f};
        int face;

        for (face = 0; face < 6; face++)
            if (!AppendPixelQuad(vertices, capacity, count, quads[face],
                                 shades[face], pixel->rgba))
                return false;
        if (left < mesh->bounds.minX) mesh->bounds.minX = left;
        if (right > mesh->bounds.maxX) mesh->bounds.maxX = right;
        if (bottom < mesh->bounds.minY) mesh->bounds.minY = bottom;
        if (top > mesh->bounds.maxY) mesh->bounds.maxY = top;
        if (south < mesh->bounds.minZ) mesh->bounds.minZ = south;
        if (north > mesh->bounds.maxZ) mesh->bounds.maxZ = north;
        mesh->featureFaceCount += 6;
    }
    return true;
}

static void FaceMaterial(const struct DioramaTerrainCell *cell, uint8_t surface,
                          const struct DioramaTerrainMaterial *source,
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
        if (surface == 0 && cell->archetype == DIORAMA_ARCHETYPE_GROUPED_HULL)
        {
            int bodyRows = cell->structureHeight >= 2 ? 2 : 1;
            int bodyStart = cell->structureHeight - bodyRows;
            int32_t groupPixelZ = (cell->mapY - (cell->structureLocalY - bodyStart))
                                * DIORAMA_VOXELS_PER_CELL;

            vMin = (face->vMin - groupPixelZ)
                 / (float)(bodyRows * DIORAMA_VOXELS_PER_CELL);
            vMax = (face->vMax - groupPixelZ)
                 / (float)(bodyRows * DIORAMA_VOXELS_PER_CELL);
        }
        else
        {
            vMin = (face->vMin - sourcePixelZ) / (float)DIORAMA_VOXELS_PER_CELL;
            vMax = (face->vMax - sourcePixelZ) / (float)DIORAMA_VOXELS_PER_CELL;
        }
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
        do
        {
            float positions[4][3];
            float shade;
            int corner;
            unsigned measuredBand = 0;

            if (face->axis != 1 && cell->measuredBandCount != 0)
            {
                int16_t boundary;

                measuredBand = band.vMin < 0 ? 0 : band.vMin / 8;
                if (measuredBand >= cell->measuredBandCount)
                    measuredBand = cell->measuredBandCount - 1;
                boundary = band.vMin < 0 ? 0 : (int16_t)((measuredBand + 1) * 8);
                band.vMax = boundary < bandEnd ? boundary : bandEnd;
                sourceMaterial = &cell->measuredBands[measuredBand];
            }
            else if (face->axis != 1 && band.vMin + DIORAMA_VOXELS_PER_CELL < bandEnd)
            {
                band.vMax = band.vMin + DIORAMA_VOXELS_PER_CELL;
                sourceMaterial = surface == 0 ? &cell->materials[materialFace]
                                              : &cell->underlayMaterials[materialFace];
            }
            else
            {
                band.vMax = bandEnd;
                sourceMaterial = surface == 0 ? &cell->materials[materialFace]
                                              : &cell->underlayMaterials[materialFace];
            }
            if (sourceMaterial->layer == DIORAMA_MATERIAL_NONE)
            {
                band.vMin = band.vMax;
                continue;
            }
            FacePositions(&band, positions);
            FaceMaterial(cell, surface, sourceMaterial, &band, &material);
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
            if (surface == 1 && face->axis == 1 && face->sign > 0
             && cell->archetype == DIORAMA_ARCHETYPE_GROUPED_HULL
             && cell->treeHullReady && cell->structureWidth == 2
             && cell->structureLocalY >= cell->structureHeight - 2)
                shade = 0.78f;
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

    if (!AppendTreeHulls(input, vertices, vertexCapacity, &vertexCount, mesh)
     || !AppendPixelPrimitives(input, vertices, vertexCapacity, &vertexCount, mesh))
        return false;

    mesh->vertexCount = vertexCount;
    mesh->faceCount = mesh->topFaceCount + mesh->bottomFaceCount
                    + mesh->sideFaceCount + mesh->featureFaceCount;
    if (mesh->faceCount == 0 && mesh->treeInstanceCount == 0)
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
        mesh->geometryHash = HashU32(mesh->geometryHash, vertices[i].color);
    }
    mesh->geometryHash = HashU16(mesh->geometryHash, mesh->treeInstanceCount);
    for (uint16_t i = 0; i < mesh->treeInstanceCount; i++)
    {
        mesh->geometryHash = HashFloat(mesh->geometryHash, mesh->treeInstances[i].x);
        mesh->geometryHash = HashFloat(mesh->geometryHash, mesh->treeInstances[i].y);
        mesh->geometryHash = HashFloat(mesh->geometryHash, mesh->treeInstances[i].z);
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
