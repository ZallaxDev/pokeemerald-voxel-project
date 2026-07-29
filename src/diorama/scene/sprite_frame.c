#ifdef ENABLE_DIORAMA

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "diorama/sprite_frame.h"
#include "diorama/rules.h"
#include "diorama/terrain_mesh.h"

static uint32_t ConvertColor(uint16_t bgr555)
{
    uint8_t red = bgr555 & 0x1F;
    uint8_t green = (bgr555 >> 5) & 0x1F;
    uint8_t blue = (bgr555 >> 10) & 0x1F;

    red = (red << 3) | (red >> 2);
    green = (green << 3) | (green >> 2);
    blue = (blue << 3) | (blue >> 2);
    return 0xFF000000u | ((uint32_t)red << 16) | ((uint32_t)green << 8) | blue;
}

static uint64_t HashPalette(const uint16_t *palette)
{
    uint64_t hash = UINT64_C(1469598103934665603);
    int i;

    for (i = 0; i < 16; i++)
    {
        hash = (hash ^ (palette[i] & 0xFF)) * UINT64_C(1099511628211);
        hash = (hash ^ (palette[i] >> 8)) * UINT64_C(1099511628211);
    }
    return hash;
}

static const struct DioramaCellSnapshot *FindCell(const struct DioramaSceneSnapshot *snapshot,
                                                   int mapX, int mapY)
{
    int gridX = mapX - snapshot->gridOriginX;
    int gridY = mapY - snapshot->gridOriginY;
    int index;

    if (gridX < 0 || gridY < 0 || gridX >= DIORAMA_GRID_WIDTH || gridY >= DIORAMA_GRID_HEIGHT)
        return NULL;
    index = gridY * DIORAMA_GRID_WIDTH + gridX;
    if (index >= snapshot->visibleCellCount
     || snapshot->cells[index].mapX != mapX || snapshot->cells[index].mapY != mapY)
        return NULL;
    return &snapshot->cells[index];
}

bool DioramaSprite_GetDimensions(uint8_t shape, uint8_t size,
                                 uint8_t *width, uint8_t *height)
{
    static const uint8_t widths[3][4] = {
        {8, 16, 32, 64}, {16, 32, 32, 64}, {8, 8, 16, 32}
    };
    static const uint8_t heights[3][4] = {
        {8, 16, 32, 64}, {8, 8, 16, 32}, {16, 32, 32, 64}
    };

    if (shape >= 3 || size >= 4 || width == NULL || height == NULL)
        return false;
    *width = widths[shape][size];
    *height = heights[shape][size];
    return true;
}

bool DioramaSprite_IsSupported(const struct DioramaObjectSnapshot *object)
{
    uint8_t width;
    uint8_t height;

    return object != NULL
        && object->active
        && object->affineMode == 0
        && object->objMode == 0
        && object->bpp == 0
        && !(object->flags & (DIORAMA_OBJECT_SUBSPRITES | DIORAMA_OBJECT_FRAME_INVALID))
        && DioramaSprite_GetDimensions(object->oamShape, object->oamSize, &width, &height)
        && object->frameSize == (uint16_t)(width * height / 2);
}

void DioramaSprite_MakeFrameKey(const struct DioramaSceneSnapshot *snapshot,
                                const struct DioramaObjectSnapshot *object,
                                struct DioramaSpriteFrameKey *key)
{
    memset(key, 0, sizeof(*key));
    key->shape = object->oamShape;
    key->size = object->oamSize;
    key->hFlip = object->hFlip;
    key->vFlip = object->vFlip;
    key->frameHash = object->frameHash;
    key->paletteHash = HashPalette(snapshot->fadedPalette + 256 + object->paletteNum * 16);
}

bool DioramaSprite_FrameKeyEqual(const struct DioramaSpriteFrameKey *left,
                                 const struct DioramaSpriteFrameKey *right)
{
    return memcmp(left, right, sizeof(*left)) == 0;
}

bool DioramaSprite_DecodeFrame(const struct DioramaSceneSnapshot *snapshot,
                               const struct DioramaObjectSnapshot *object,
                               uint32_t *pixels, size_t pixelCapacity,
                               uint8_t *width, uint8_t *height)
{
    int outputX;
    int outputY;
    int widthInTiles;

    if (snapshot == NULL || pixels == NULL || !DioramaSprite_IsSupported(object)
     || !DioramaSprite_GetDimensions(object->oamShape, object->oamSize, width, height)
     || pixelCapacity < (size_t)*width * *height)
        return false;
    widthInTiles = *width / 8;
    for (outputY = 0; outputY < *height; outputY++)
    {
        for (outputX = 0; outputX < *width; outputX++)
        {
            int sourceX = object->hFlip ? *width - 1 - outputX : outputX;
            int sourceY = object->vFlip ? *height - 1 - outputY : outputY;
            int tile = (sourceY / 8) * widthInTiles + sourceX / 8;
            int tileX = sourceX & 7;
            int tileY = sourceY & 7;
            uint8_t packed = object->frameGraphics[tile * DIORAMA_TILE_BYTES
                                                 + tileY * 4 + tileX / 2];
            uint8_t colorId = tileX & 1 ? packed >> 4 : packed & 0x0F;

            pixels[outputY * *width + outputX] = colorId == 0 ? 0
                : ConvertColor(snapshot->fadedPalette[256 + object->paletteNum * 16 + colorId]);
        }
    }
    return true;
}

bool DioramaSprite_BuildPose(const struct DioramaSceneSnapshot *snapshot,
                             const struct DioramaObjectSnapshot *object,
                             struct DioramaSpritePose *pose)
{
    const struct DioramaCellSnapshot *currentCell;
    const struct DioramaCellSnapshot *previousCell;
    struct DioramaResolvedCell currentRule;
    struct DioramaResolvedCell previousRule;
    float currentHeight;
    float previousHeight;
    float moveProgress = 1.0f;
    int deltaX;
    int deltaY;

    if (snapshot == NULL || object == NULL || pose == NULL)
        return false;
    currentCell = FindCell(snapshot, object->currentMapX, object->currentMapY);
    previousCell = FindCell(snapshot, object->previousMapX, object->previousMapY);
    if (currentCell == NULL)
        return false;
    if (!DioramaRules_ResolveCell(snapshot, currentCell, &currentRule))
        return false;
    currentHeight = currentRule.groundHeight;
    previousHeight = currentHeight;
    if (previousCell != NULL
     && DioramaRules_ResolveCell(snapshot, previousCell, &previousRule))
        previousHeight = previousRule.groundHeight;
    deltaX = object->currentMapX - object->previousMapX;
    deltaY = object->currentMapY - object->previousMapY;
    if (deltaX != 0)
        moveProgress = 1.0f + object->mapPixelOffsetX / (16.0f * deltaX);
    else if (deltaY != 0)
        moveProgress = 1.0f + object->mapPixelOffsetY / (16.0f * deltaY);
    if (moveProgress < 0.0f) moveProgress = 0.0f;
    if (moveProgress > 1.0f) moveProgress = 1.0f;

    pose->x = object->currentMapX
            + (object->mapPixelOffsetX + object->spriteX2) / 16.0f;
    pose->z = -(object->currentMapY + object->mapPixelOffsetY / 16.0f);
    pose->groundY = previousHeight + (currentHeight - previousHeight) * moveProgress;
    pose->y = pose->groundY - object->spriteY2 / 16.0f;
    return true;
}

bool DioramaSprite_CanInterpolate(const struct DioramaSceneSnapshot *previousSnapshot,
                                  const struct DioramaObjectSnapshot *previousObject,
                                  const struct DioramaSceneSnapshot *currentSnapshot,
                                  const struct DioramaObjectSnapshot *currentObject)
{
    struct DioramaSpritePose previousPose;
    struct DioramaSpritePose currentPose;
    float dx;
    float dy;
    float dz;
    bool sameMovement;
    bool continuedMovement;

    if (previousObject != NULL && currentObject != NULL)
    {
        sameMovement = previousObject->previousMapX == currentObject->previousMapX
                    && previousObject->previousMapY == currentObject->previousMapY
                    && previousObject->currentMapX == currentObject->currentMapX
                    && previousObject->currentMapY == currentObject->currentMapY;
        continuedMovement = previousObject->currentMapX == currentObject->previousMapX
                         && previousObject->currentMapY == currentObject->previousMapY;
    }
    else
    {
        sameMovement = false;
        continuedMovement = false;
    }

    if (previousSnapshot == NULL || previousObject == NULL
     || currentSnapshot == NULL || currentObject == NULL
     || previousSnapshot->mapGeneration != currentSnapshot->mapGeneration
     || currentSnapshot->sequence != previousSnapshot->sequence + 1
     || previousObject->localId != currentObject->localId
     || ((previousObject->flags ^ currentObject->flags) & DIORAMA_OBJECT_PLAYER)
     || (!sameMovement && !continuedMovement)
     || abs(currentObject->currentMapX - currentObject->previousMapX) > 1
     || abs(currentObject->currentMapY - currentObject->previousMapY) > 1
     || !DioramaSprite_BuildPose(previousSnapshot, previousObject, &previousPose)
     || !DioramaSprite_BuildPose(currentSnapshot, currentObject, &currentPose))
        return false;
    dx = currentPose.x - previousPose.x;
    dy = currentPose.y - previousPose.y;
    dz = currentPose.z - previousPose.z;
    if (continuedMovement && !sameMovement && dx * dx + dy * dy + dz * dz > 0.25f)
        return false;
    return dx * dx + dy * dy + dz * dz <= 2.25f;
}

struct DioramaSpritePose DioramaSprite_InterpolatePose(struct DioramaSpritePose previous,
                                                        struct DioramaSpritePose current,
                                                        float alpha)
{
    struct DioramaSpritePose result;

    if (alpha < 0.0f) alpha = 0.0f;
    if (alpha > 1.0f) alpha = 1.0f;
    result.x = previous.x + (current.x - previous.x) * alpha;
    result.y = previous.y + (current.y - previous.y) * alpha;
    result.z = previous.z + (current.z - previous.z) * alpha;
    result.groundY = previous.groundY + (current.groundY - previous.groundY) * alpha;
    return result;
}

struct DioramaSpritePose DioramaSprite_ApplyScreenOffset(struct DioramaSpritePose pose,
                                                          float offsetX, float offsetY,
                                                          float cameraPitch)
{
    pose.x += offsetX / 16.0f;
    pose.y -= offsetY * cosf(cameraPitch) / 16.0f;
    pose.z -= offsetY * sinf(cameraPitch) / 16.0f;
    return pose;
}

int DioramaSprite_CompareDepth(const struct DioramaSpritePose *left, uint8_t leftPriority,
                               uint8_t leftSubpriority, uint8_t leftOamOrder,
                               const struct DioramaSpritePose *right, uint8_t rightPriority,
                               uint8_t rightSubpriority, uint8_t rightOamOrder,
                               float cameraZ, float cameraPitch)
{
    float pitchSin = sinf(cameraPitch);
    float pitchCos = cosf(cameraPitch);
    float leftDepth = (16.0f - left->y) * pitchSin
                    + (left->z - cameraZ) * pitchCos;
    float rightDepth = (16.0f - right->y) * pitchSin
                     + (right->z - cameraZ) * pitchCos;

    if (leftDepth < rightDepth) return 1;
    if (leftDepth > rightDepth) return -1;
    if (leftPriority != rightPriority) return leftPriority < rightPriority ? 1 : -1;
    if (leftSubpriority != rightSubpriority) return leftSubpriority < rightSubpriority ? 1 : -1;
    return leftOamOrder < rightOamOrder ? 1 : -(leftOamOrder != rightOamOrder);
}

#endif
