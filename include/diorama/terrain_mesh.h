#ifndef GUARD_DIORAMA_TERRAIN_MESH_H
#define GUARD_DIORAMA_TERRAIN_MESH_H

#include <stdbool.h>
#include <stdint.h>

#include "diorama/rules.h"
#include "diorama/occupancy.h"

#define DIORAMA_TERRAIN_CHUNK_SIZE 8
#define DIORAMA_TERRAIN_HALO 1
#define DIORAMA_TERRAIN_INPUT_SIZE (DIORAMA_TERRAIN_CHUNK_SIZE + 2 * DIORAMA_TERRAIN_HALO)
#define DIORAMA_TERRAIN_MAX_SURFACES DIORAMA_MAX_VISUAL_SURFACES
#define DIORAMA_TERRAIN_PIXEL_COLUMNS (DIORAMA_TERRAIN_CHUNK_SIZE * DIORAMA_VOXELS_PER_CELL * DIORAMA_TERRAIN_CHUNK_SIZE * DIORAMA_VOXELS_PER_CELL)
#define DIORAMA_TERRAIN_MAX_FACES (DIORAMA_TERRAIN_PIXEL_COLUMNS * DIORAMA_OCCUPANCY_FACE_COUNT * DIORAMA_TERRAIN_MAX_SURFACES)
#define DIORAMA_TERRAIN_MAX_PIXEL_PRIMITIVES 4096
#define DIORAMA_TERRAIN_MAX_TREE_INSTANCES 32
#define DIORAMA_TERRAIN_MAX_VERTICES ((DIORAMA_TERRAIN_MAX_FACES + DIORAMA_TERRAIN_MAX_PIXEL_PRIMITIVES * 6) * 6)
#define DIORAMA_TERRAIN_LEDGE_HEIGHT 0.375f
#define DIORAMA_TERRAIN_WATER_HEIGHT -0.125f
#define DIORAMA_TERRAIN_TEXTURE_FULL 0.0f
#define DIORAMA_TERRAIN_TEXTURE_BASE 1.0f
#define DIORAMA_TERRAIN_TEXTURE_FOREGROUND 2.0f
#define DIORAMA_TERRAIN_TEXTURE_VERTEX_COLOR 3.0f

enum DioramaTerrainFace
{
    DIORAMA_TERRAIN_FACE_TOP,
    DIORAMA_TERRAIN_FACE_NORTH,
    DIORAMA_TERRAIN_FACE_EAST,
    DIORAMA_TERRAIN_FACE_SOUTH,
    DIORAMA_TERRAIN_FACE_WEST,
};

enum DioramaElevationSemantics
{
    DIORAMA_ELEVATION_WILDCARD,
    DIORAMA_ELEVATION_CONCRETE,
    DIORAMA_ELEVATION_RETAIN
};

enum DioramaTreeShade
{
    DIORAMA_TREE_SHADE_OFF,
    DIORAMA_TREE_SHADE_BLACK,
    DIORAMA_TREE_SHADE_DARK,
    DIORAMA_TREE_SHADE_LIGHT,
    DIORAMA_TREE_SHADE_WHITE
};

struct DioramaTerrainMaterial
{
    uint16_t metatileId;
    uint8_t layer;
    uint8_t rotation;
    uint8_t flags;
    float u0;
    float v0;
    float u1;
    float v1;
};

struct DioramaTerrainSurface
{
    float bottomHeight;
    float topHeight;
    uint8_t gameplayElevation;
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
    uint8_t reflective;
    uint8_t shape;
    uint8_t archetype;
    uint8_t terrainClass;
    uint8_t semanticProfile;
    uint8_t profile;
    uint8_t planeAxis;
    uint8_t effectiveElevation;
    uint8_t surfaceCount;
    uint16_t claimOwner;
    uint16_t regionId;
    uint16_t structureId;
    int16_t rulePriority;
    uint16_t structureTemplateId;
    int16_t structureX;
    int16_t structureY;
    uint8_t structureWidth;
    uint8_t structureHeight;
    uint8_t structureRoofRows;
    uint8_t structureLocalX;
    uint8_t structureLocalY;
    uint8_t structureOwnerKind;
    uint8_t doorFold;
    uint8_t voidKind;
    uint8_t treeHullReady;
    uint8_t treeMaskDirect;
    uint8_t southFacadeCount;
    uint8_t measuredAxis;
    uint16_t measuredExtentBands;
    uint8_t measuredPeriodBands;
    uint8_t measuredRoofBands;
    uint16_t measuredRunLocal;
    uint16_t measuredRunLength;
    uint8_t measuredFlags;
    uint8_t measuredBandCount;
    uint8_t cliffEdgeMask;
    uint8_t cliffBaseMask;
    uint8_t cliffTransitionMask;
    uint8_t cliffCornerMask;
    uint8_t terraceProfile;
    float groundHeight;
    float visualHeight;
    float featureHeight;
    float structureBodyHeight;
    float structureRoofHeight;
    float southFacadeUnitHeight;
    float measuredConfidence;
    struct DioramaTerrainSurface surfaces[DIORAMA_TERRAIN_MAX_SURFACES];
    struct DioramaTerrainMaterial materials[DIORAMA_MATERIAL_FACE_COUNT];
    struct DioramaTerrainMaterial underlayMaterials[DIORAMA_MATERIAL_FACE_COUNT];
    struct DioramaTerrainMaterial treeMaterial;
    uint16_t foregroundAlpha[DIORAMA_VOXELS_PER_CELL];
    uint8_t mountainHeight[DIORAMA_VOXELS_PER_CELL * DIORAMA_VOXELS_PER_CELL];
    uint8_t treeShade[DIORAMA_VOXELS_PER_CELL * DIORAMA_VOXELS_PER_CELL];
    struct DioramaTerrainMaterial southFacadeMaterials[DIORAMA_BUILDING_MAX_FACADE_ROWS];
    struct DioramaTerrainMaterial measuredBands[DIORAMA_MAX_MEASURED_BANDS];
};

struct DioramaTerrainHeightCell
{
    int16_t mapX;
    int16_t mapY;
    uint8_t behavior;
    uint8_t rawElevation;
};

struct DioramaTerrainPixelPrimitive
{
    int32_t xQ32;
    int32_t yQ32;
    int32_t zQ32;
    uint32_t rgba;
    uint32_t sourceCellOffset;
    uint16_t objectId;
    uint16_t structureId;
    uint16_t expectedMetatile;
    uint16_t expectedTileEntry;
    uint8_t sizeXQ32;
    uint8_t sizeYQ32;
    uint8_t sizeZQ32;
    uint8_t sourceLayer;
    uint8_t sourceX;
    uint8_t sourceY;
    uint8_t kind;
};

struct DioramaTerrainChunkInput
{
    int16_t chunkX;
    int16_t chunkY;
    uint32_t mapGeneration;
    uint32_t rulesGeneration;
    uint32_t pixelCount;
    struct DioramaTerrainCell cells[DIORAMA_TERRAIN_INPUT_SIZE * DIORAMA_TERRAIN_INPUT_SIZE];
    struct DioramaTerrainPixelPrimitive pixels[DIORAMA_TERRAIN_MAX_PIXEL_PRIMITIVES];
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
    float reflectionMask;
    uint32_t color;
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

struct DioramaTerrainTreeInstance
{
    float x;
    float y;
    float z;
};

struct DioramaTerrainMesh
{
    uint32_t vertexCount;
    uint32_t faceCount;
    uint32_t occupancySpanCount;
    uint32_t shellFaceCount;
    uint32_t topFaceCount;
    uint32_t bottomFaceCount;
    uint32_t sideFaceCount;
    uint32_t featureFaceCount;
    uint32_t cliffBaseFaceCount;
    uint32_t cliffCornerCount;
    uint32_t cliffTransitionFaceCount;
    uint16_t treeInstanceCount;
    uint8_t usedCompressedOccupancy;
    uint64_t geometryHash;
    struct DioramaTerrainBounds bounds;
    struct DioramaTerrainTreeInstance treeInstances[DIORAMA_TERRAIN_MAX_TREE_INSTANCES];
};

int32_t DioramaTerrain_FloorDiv(int32_t value, int32_t divisor);
int16_t DioramaTerrain_VisibleStructureOrigin(int16_t mapCoordinate,
                                              uint8_t structureLocalCoordinate);
bool DioramaTerrain_DirtyCellAffectsChunk(int16_t mapX, int16_t mapY,
                                         int16_t chunkX, int16_t chunkY);
bool DioramaTerrain_ShouldInvalidateAll(uint64_t previousSequence,
                                       uint64_t currentSequence,
                                       uint32_t previousEditGeneration,
                                       uint32_t currentEditGeneration,
                                       uint8_t dirtyCellCount,
                                       bool dirtyOverflow);
float DioramaTerrain_NormalizeElevation(uint8_t rawElevation, uint8_t behavior);
enum DioramaElevationSemantics DioramaTerrain_GetElevationSemantics(uint8_t rawElevation);
void DioramaTerrain_BuildHeightField(const struct DioramaTerrainHeightCell *cells,
                                     uint16_t width, uint16_t height,
                                     float *visualHeights);
void DioramaTerrain_BuildTerraceHeightmap(uint8_t profile,
                                          const uint16_t artworkRows[DIORAMA_VOXELS_PER_CELL],
                                          bool useArtwork,
                                          uint16_t rows[DIORAMA_VOXELS_PER_CELL],
                                          uint8_t heights[DIORAMA_VOXELS_PER_CELL
                                                          * DIORAMA_VOXELS_PER_CELL]);
uint64_t DioramaTerrain_ChunkSignature(const struct DioramaTerrainChunkInput *input);
bool DioramaTerrain_BuildChunk(const struct DioramaTerrainChunkInput *input,
                               struct DioramaTerrainVertex *vertices,
                               uint32_t vertexCapacity,
                               struct DioramaTerrainMesh *mesh);
bool DioramaTerrain_BuildTreeModel(struct DioramaTerrainVertex *vertices,
                                   uint32_t vertexCapacity, uint32_t *vertexCount,
                                   struct DioramaTerrainBounds *bounds);
bool DioramaTerrain_IsBoundsVisible(const struct DioramaTerrainBounds *bounds,
                                     float cameraX, float cameraZ,
                                     float cameraPitch, float focalLength);

#endif
