#ifndef GUARD_DIORAMA_OCCUPANCY_H
#define GUARD_DIORAMA_OCCUPANCY_H

#include <stdbool.h>
#include <stdint.h>

#define DIORAMA_VOXELS_PER_CELL 16
#define DIORAMA_OCCUPANCY_FACE_COUNT 6

enum DioramaOccupancyFace
{
    DIORAMA_OCCUPANCY_FACE_TOP,
    DIORAMA_OCCUPANCY_FACE_BOTTOM,
    DIORAMA_OCCUPANCY_FACE_NORTH,
    DIORAMA_OCCUPANCY_FACE_EAST,
    DIORAMA_OCCUPANCY_FACE_SOUTH,
    DIORAMA_OCCUPANCY_FACE_WEST,
};

struct DioramaOccupancySpan
{
    int32_t x;
    int32_t z;
    int16_t yMin;
    int16_t yMax;
    int16_t sourceCellX;
    int16_t sourceCellY;
    uint16_t material[DIORAMA_OCCUPANCY_FACE_COUNT];
    uint16_t flags;
};

struct DioramaShellFace
{
    int32_t plane;
    int32_t uMin;
    int32_t uMax;
    int16_t vMin;
    int16_t vMax;
    int16_t sourceCellX;
    int16_t sourceCellY;
    uint16_t material;
    uint16_t flags;
    uint8_t axis;
    int8_t sign;
};

bool DioramaOccupancy_Canonicalize(struct DioramaOccupancySpan *spans,
                                   uint32_t *spanCount);
bool DioramaOccupancy_BuildShell(const struct DioramaOccupancySpan *spans,
                                 uint32_t spanCount,
                                 int32_t ownerMinX, int32_t ownerMinZ,
                                 int32_t ownerMaxX, int32_t ownerMaxZ,
                                 struct DioramaShellFace *faces,
                                 uint32_t faceCapacity, uint32_t *faceCount);
uint32_t DioramaOccupancy_MergeFaces(struct DioramaShellFace *faces,
                                     uint32_t faceCount);

#endif
