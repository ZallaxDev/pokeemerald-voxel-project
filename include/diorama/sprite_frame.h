#ifndef GUARD_DIORAMA_SPRITE_FRAME_H
#define GUARD_DIORAMA_SPRITE_FRAME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "diorama/scene_snapshot.h"

#define DIORAMA_SPRITE_MAX_DIMENSION 64
#define DIORAMA_SPRITE_MAX_PIXELS (DIORAMA_SPRITE_MAX_DIMENSION * DIORAMA_SPRITE_MAX_DIMENSION)

struct DioramaSpriteFrameKey
{
    uint8_t shape;
    uint8_t size;
    uint8_t hFlip;
    uint8_t vFlip;
    uint64_t frameHash;
    uint64_t paletteHash;
};

struct DioramaSpritePose
{
    float x;
    float y;
    float z;
    float groundY;
};

bool DioramaSprite_GetDimensions(uint8_t shape, uint8_t size,
                                 uint8_t *width, uint8_t *height);
bool DioramaSprite_IsSupported(const struct DioramaObjectSnapshot *object);
void DioramaSprite_MakeFrameKey(const struct DioramaSceneSnapshot *snapshot,
                                const struct DioramaObjectSnapshot *object,
                                struct DioramaSpriteFrameKey *key);
bool DioramaSprite_FrameKeyEqual(const struct DioramaSpriteFrameKey *left,
                                 const struct DioramaSpriteFrameKey *right);
bool DioramaSprite_DecodeFrame(const struct DioramaSceneSnapshot *snapshot,
                               const struct DioramaObjectSnapshot *object,
                               uint32_t *pixels, size_t pixelCapacity,
                               uint8_t *width, uint8_t *height);
bool DioramaSprite_BuildPose(const struct DioramaSceneSnapshot *snapshot,
                             const struct DioramaObjectSnapshot *object,
                             struct DioramaSpritePose *pose);
bool DioramaSprite_CanInterpolate(const struct DioramaSceneSnapshot *previousSnapshot,
                                  const struct DioramaObjectSnapshot *previousObject,
                                  const struct DioramaSceneSnapshot *currentSnapshot,
                                  const struct DioramaObjectSnapshot *currentObject);
struct DioramaSpritePose DioramaSprite_InterpolatePose(struct DioramaSpritePose previous,
                                                         struct DioramaSpritePose current,
                                                         float alpha);
struct DioramaSpritePose DioramaSprite_ApplyScreenOffset(struct DioramaSpritePose pose,
                                                          float offsetX, float offsetY,
                                                          float cameraPitch);
int DioramaSprite_CompareDepth(const struct DioramaSpritePose *left, uint8_t leftPriority,
                               uint8_t leftSubpriority, uint8_t leftOamOrder,
                                const struct DioramaSpritePose *right, uint8_t rightPriority,
                                uint8_t rightSubpriority, uint8_t rightOamOrder,
                                float cameraZ, float cameraPitch);

#endif
