#ifndef GUARD_DIORAMA_TREE_MODEL_GENERATED_H
#define GUARD_DIORAMA_TREE_MODEL_GENERATED_H

#include <stdint.h>

#define DIORAMA_TREE_MODEL_SIZE_X 32
#define DIORAMA_TREE_MODEL_SIZE_Z 24
#define DIORAMA_TREE_MODEL_SIZE_Y 48
#define DIORAMA_TREE_MODEL_MIN_X 0
#define DIORAMA_TREE_MODEL_MIN_Z 1
#define DIORAMA_TREE_MODEL_MIN_Y 2
#define DIORAMA_TREE_MODEL_MAX_X 32
#define DIORAMA_TREE_MODEL_MAX_Z 24
#define DIORAMA_TREE_MODEL_MAX_Y 36

struct DioramaTreeModelFace
{
    uint8_t axis;
    int8_t sign;
    uint8_t plane;
    uint8_t uMin;
    uint8_t uMax;
    uint8_t vMin;
    uint8_t vMax;
    uint32_t rgba;
};

extern const struct DioramaTreeModelFace gDioramaTreeModelFaces[];
extern const uint32_t gDioramaTreeModelFaceCount;
extern const uint32_t gDioramaTreeModelVoxelCount;

#endif
