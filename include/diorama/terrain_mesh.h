#ifndef GUARD_DIORAMA_TERRAIN_MESH_H
#define GUARD_DIORAMA_TERRAIN_MESH_H

#include <stdbool.h>
#include <stdint.h>

#include "diorama/rules.h"

#define DIORAMA_TERRAIN_CHUNK_SIZE 8
#define DIORAMA_TERRAIN_HALO 1
#define DIORAMA_TERRAIN_INPUT_SIZE (DIORAMA_TERRAIN_CHUNK_SIZE + 2 * DIORAMA_TERRAIN_HALO)
#define DIORAMA_TERRAIN_MAX_FACES (DIORAMA_TERRAIN_CHUNK_SIZE * DIORAMA_TERRAIN_CHUNK_SIZE * 7)
#define DIORAMA_TERRAIN_MAX_VERTICES (DIORAMA_TERRAIN_MAX_FACES * 6)
#define DIORAMA_TERRAIN_LEDGE_HEIGHT 0.375f
#define DIORAMA_TERRAIN_WATER_HEIGHT -0.125f
#define DIORAMA_TERRAIN_TEXTURE_FULL 0.0f
#define DIORAMA_TERRAIN_TEXTURE_BASE 1.0f
#define DIORAMA_TERRAIN_TEXTURE_FOREGROUND 2.0f

enum DioramaTerrainFace
{
    DIORAMA_TERRAIN_FACE_TOP,
    DIORAMA_TERRAIN_FACE_NORTH,
    DIORAMA_TERRAIN_FACE_EAST,
    DIORAMA_TERRAIN_FACE_SOUTH,
    DIORAMA_TERRAIN_FACE_WEST,
};

struct DioramaTerrainMaterial
{
    uint16_t metatileId;
    uint8_t layer;
    float u0;
    float v0;
    float u1;
    float v1;
};

struct DioramaTerrainCell
{
    uint8_t present;
    int16_t mapX;
    int16_t mapY;
    uint16_t metatileId;
    uint8_t behavior;
    uint8_t layerType;
    uint8_t collision;
    uint8_t rawElevation;
    uint8_t shape;
    uint8_t profile;
    uint8_t planeAxis;
    uint16_t structureId;
    int16_t structureX;
    int16_t structureY;
    uint8_t structureWidth;
    uint8_t structureHeight;
    uint8_t structureRoofRows;
    uint8_t structureLocalX;
    uint8_t structureLocalY;
    float groundHeight;
    float visualHeight;
    float featureHeight;
    float structureBodyHeight;
    float structureRoofHeight;
    struct DioramaTerrainMaterial materials[DIORAMA_MATERIAL_FACE_COUNT];
};

struct DioramaTerrainHeightCell
{
    int16_t mapX;
    int16_t mapY;
    uint8_t behavior;
    uint8_t rawElevation;
};

struct DioramaTerrainChunkInput
{
    int16_t chunkX;
    int16_t chunkY;
    uint32_t mapGeneration;
    uint32_t rulesGeneration;
    struct DioramaTerrainCell cells[DIORAMA_TERRAIN_INPUT_SIZE * DIORAMA_TERRAIN_INPUT_SIZE];
};

struct DioramaTerrainVertex
{
    float x;
    float y;
    float z;
    float u;
    float v;
    float shade;
    float textureLayer;
};

struct DioramaTerrainBounds
{
    float minX;
    float minY;
    float minZ;
    float maxX;
    float maxY;
    float maxZ;
};

struct DioramaTerrainMesh
{
    uint32_t vertexCount;
    uint32_t faceCount;
    uint32_t topFaceCount;
    uint32_t sideFaceCount;
    uint32_t featureFaceCount;
    uint64_t geometryHash;
    struct DioramaTerrainBounds bounds;
};

int32_t DioramaTerrain_FloorDiv(int32_t value, int32_t divisor);
float DioramaTerrain_NormalizeElevation(uint8_t rawElevation, uint8_t behavior);
void DioramaTerrain_BuildHeightField(const struct DioramaTerrainHeightCell *cells,
                                     uint16_t width, uint16_t height,
                                     float *visualHeights);
uint64_t DioramaTerrain_ChunkSignature(const struct DioramaTerrainChunkInput *input);
bool DioramaTerrain_BuildChunk(const struct DioramaTerrainChunkInput *input,
                               struct DioramaTerrainVertex *vertices,
                               uint32_t vertexCapacity,
                               struct DioramaTerrainMesh *mesh);
bool DioramaTerrain_IsBoundsVisible(const struct DioramaTerrainBounds *bounds,
                                     float cameraX, float cameraZ,
                                     float cameraPitch, float focalLength);

#endif
