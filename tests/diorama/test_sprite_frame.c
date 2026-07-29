#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "constants/metatile_behaviors.h"
#include "diorama/sprite_frame.h"
#include "diorama/terrain_mesh.h"

#define CHECK(condition) Check((condition), #condition, __LINE__)
#define CHECK_CLOSE(actual, expected) CheckClose((actual), (expected), #actual, __LINE__)

static struct DioramaSceneSnapshot sSnapshot;
static uint32_t sPixels[DIORAMA_SPRITE_MAX_PIXELS];

static void Check(bool condition, const char *expression, int line)
{
    if (!condition)
    {
        fprintf(stderr, "test_sprite_frame.c:%d: check failed: %s\n", line, expression);
        exit(EXIT_FAILURE);
    }
}

static void CheckClose(float actual, float expected, const char *expression, int line)
{
    if (fabsf(actual - expected) > 0.0001f)
    {
        fprintf(stderr, "test_sprite_frame.c:%d: %s was %.6f, expected %.6f\n",
                line, expression, actual, expected);
        exit(EXIT_FAILURE);
    }
}

static void FillTile(struct DioramaObjectSnapshot *object, uint16_t tile, uint8_t color)
{
    memset(&object->frameGraphics[tile * DIORAMA_TILE_BYTES],
           color | (color << 4), DIORAMA_TILE_BYTES);
}

static void InitSnapshot(void)
{
    int x;
    int y;

    memset(&sSnapshot, 0, sizeof(sSnapshot));
    sSnapshot.sequence = 7;
    sSnapshot.mapGeneration = 3;
    sSnapshot.objPaletteGeneration = 13;
    sSnapshot.visibleCellCount = DIORAMA_MAX_VISIBLE_CELLS;
    for (y = 0; y < DIORAMA_GRID_HEIGHT; y++)
    {
        for (x = 0; x < DIORAMA_GRID_WIDTH; x++)
        {
            int index = y * DIORAMA_GRID_WIDTH + x;
            sSnapshot.cells[index].mapX = x;
            sSnapshot.cells[index].mapY = y;
        }
    }
    sSnapshot.fadedPalette[256 + 1] = 0x001F;
    sSnapshot.fadedPalette[256 + 2] = 0x03E0;
    sSnapshot.fadedPalette[256 + 3] = 0x7C00;
    sSnapshot.fadedPalette[256 + 4] = 0x7FFF;
}

static struct DioramaObjectSnapshot MakeObject(void)
{
    struct DioramaObjectSnapshot object;

    memset(&object, 0, sizeof(object));
    object.active = 1;
    object.localId = 1;
    object.tileNum = 10;
    object.oamShape = 0;
    object.oamSize = 1;
    object.frameSize = 16 * 16 / 2;
    object.frameHash = 17;
    return object;
}

static void TestDimensions(void)
{
    static const uint8_t expectedWidths[3][4] = {
        {8, 16, 32, 64}, {16, 32, 32, 64}, {8, 8, 16, 32}
    };
    static const uint8_t expectedHeights[3][4] = {
        {8, 16, 32, 64}, {8, 8, 16, 32}, {16, 32, 32, 64}
    };
    uint8_t shape;
    uint8_t size;

    for (shape = 0; shape < 3; shape++)
    {
        for (size = 0; size < 4; size++)
        {
            uint8_t width;
            uint8_t height;

            CHECK(DioramaSprite_GetDimensions(shape, size, &width, &height));
            CHECK(width == expectedWidths[shape][size]);
            CHECK(height == expectedHeights[shape][size]);
        }
    }
    CHECK(!DioramaSprite_GetDimensions(3, 0, &shape, &size));
}

static void TestDecodeAndFlips(void)
{
    struct DioramaObjectSnapshot object = MakeObject();
    uint8_t width;
    uint8_t height;

    FillTile(&object, 0, 1);
    FillTile(&object, 1, 2);
    FillTile(&object, 2, 3);
    FillTile(&object, 3, 4);
    CHECK(DioramaSprite_DecodeFrame(&sSnapshot, &object, sPixels,
                                    DIORAMA_SPRITE_MAX_PIXELS, &width, &height));
    CHECK(width == 16 && height == 16);
    CHECK(sPixels[0] == 0xFFFF0000u);
    CHECK(sPixels[8] == 0xFF00FF00u);
    CHECK(sPixels[8 * width] == 0xFF0000FFu);
    CHECK(sPixels[8 * width + 8] == 0xFFFFFFFFu);

    object.hFlip = 1;
    CHECK(DioramaSprite_DecodeFrame(&sSnapshot, &object, sPixels,
                                    DIORAMA_SPRITE_MAX_PIXELS, &width, &height));
    CHECK(sPixels[0] == 0xFF00FF00u);
    CHECK(sPixels[15] == 0xFFFF0000u);
    object.hFlip = 0;
    object.vFlip = 1;
    CHECK(DioramaSprite_DecodeFrame(&sSnapshot, &object, sPixels,
                                    DIORAMA_SPRITE_MAX_PIXELS, &width, &height));
    CHECK(sPixels[0] == 0xFF0000FFu);

    FillTile(&object, 0, 0);
    object.vFlip = 0;
    CHECK(DioramaSprite_DecodeFrame(&sSnapshot, &object, sPixels,
                                    DIORAMA_SPRITE_MAX_PIXELS, &width, &height));
    CHECK(sPixels[0] == 0);
}

static void TestSupportAndFrameKeys(void)
{
    struct DioramaObjectSnapshot object = MakeObject();
    struct DioramaSpriteFrameKey left;
    struct DioramaSpriteFrameKey right;

    CHECK(DioramaSprite_IsSupported(&object));
    DioramaSprite_MakeFrameKey(&sSnapshot, &object, &left);
    DioramaSprite_MakeFrameKey(&sSnapshot, &object, &right);
    CHECK(DioramaSprite_FrameKeyEqual(&left, &right));
    right.paletteHash++;
    CHECK(!DioramaSprite_FrameKeyEqual(&left, &right));
    object.bpp = 1;
    CHECK(!DioramaSprite_IsSupported(&object));
    object.bpp = 0;
    object.affineMode = 1;
    CHECK(!DioramaSprite_IsSupported(&object));
    object.affineMode = 0;
    object.flags = DIORAMA_OBJECT_SUBSPRITES;
    CHECK(!DioramaSprite_IsSupported(&object));
}

static void TestPoseAndInterpolation(void)
{
    struct DioramaSceneSnapshot previousSnapshot = sSnapshot;
    struct DioramaObjectSnapshot previous = MakeObject();
    struct DioramaObjectSnapshot current = MakeObject();
    struct DioramaSpritePose pose;
    struct DioramaSpritePose interpolated;

    sSnapshot.cells[1 * DIORAMA_GRID_WIDTH + 2].behavior = MB_JUMP_EAST;
    sSnapshot.sequence = previousSnapshot.sequence + 1;
    current.previousMapX = 1;
    current.previousMapY = 1;
    current.currentMapX = 2;
    current.currentMapY = 1;
    current.mapPixelOffsetX = -8;
    CHECK(DioramaSprite_BuildPose(&sSnapshot, &current, &pose));
    CHECK_CLOSE(pose.x, 1.5f);
    CHECK_CLOSE(pose.z, -1.0f);
    CHECK_CLOSE(pose.y, DIORAMA_TERRAIN_LEDGE_HEIGHT * 0.5f);

    previous.previousMapX = 1;
    previous.previousMapY = 1;
    previous.currentMapX = 1;
    previous.currentMapY = 1;
    current.mapPixelOffsetX = -15;
    CHECK(DioramaSprite_CanInterpolate(&previousSnapshot, &previous, &sSnapshot, &current));
    interpolated = DioramaSprite_InterpolatePose((struct DioramaSpritePose){1, 2, 3, 4},
                                                  (struct DioramaSpritePose){3, 4, 5, 6}, 0.25f);
    CHECK_CLOSE(interpolated.x, 1.5f);
    CHECK_CLOSE(interpolated.y, 2.5f);
    CHECK_CLOSE(interpolated.z, 3.5f);
    CHECK_CLOSE(interpolated.groundY, 4.5f);

    sSnapshot.mapGeneration++;
    CHECK(!DioramaSprite_CanInterpolate(&previousSnapshot, &previous, &sSnapshot, &current));
    sSnapshot.mapGeneration--;
    current.currentMapX = 4;
    CHECK(!DioramaSprite_CanInterpolate(&previousSnapshot, &previous, &sSnapshot, &current));
}

static void TestJumpLift(void)
{
    struct DioramaObjectSnapshot object = MakeObject();
    struct DioramaSpritePose pose;

    object.previousMapX = object.currentMapX = 2;
    object.previousMapY = object.currentMapY = 2;
    object.spriteY2 = -8;
    CHECK(DioramaSprite_BuildPose(&sSnapshot, &object, &pose));
    CHECK_CLOSE(pose.z, -2.0f);
    CHECK_CLOSE(pose.groundY, 0.0f);
    CHECK_CLOSE(pose.y, 0.5f);
}

static void TestCardinalMovementAnchors(void)
{
    struct DioramaObjectSnapshot object = MakeObject();
    struct DioramaSpritePose pose;

    object.previousMapX = 2;
    object.previousMapY = 2;
    object.currentMapX = 2;
    object.currentMapY = 2;
    CHECK(DioramaSprite_BuildPose(&sSnapshot, &object, &pose));
    CHECK_CLOSE(pose.x, 2.0f);
    CHECK_CLOSE(pose.z, -2.0f);

    object.currentMapX = 3;
    object.mapPixelOffsetX = -16;
    CHECK(DioramaSprite_BuildPose(&sSnapshot, &object, &pose));
    CHECK_CLOSE(pose.x, 2.0f);
    object.currentMapX = 1;
    object.mapPixelOffsetX = 16;
    CHECK(DioramaSprite_BuildPose(&sSnapshot, &object, &pose));
    CHECK_CLOSE(pose.x, 2.0f);

    object.currentMapX = 2;
    object.mapPixelOffsetX = 0;
    object.currentMapY = 3;
    object.mapPixelOffsetY = -16;
    CHECK(DioramaSprite_BuildPose(&sSnapshot, &object, &pose));
    CHECK_CLOSE(pose.z, -2.0f);
    object.currentMapY = 1;
    object.mapPixelOffsetY = 16;
    CHECK(DioramaSprite_BuildPose(&sSnapshot, &object, &pose));
    CHECK_CLOSE(pose.z, -2.0f);
}

static void TestInterpolationDiscontinuities(void)
{
    struct DioramaSceneSnapshot previousSnapshot = sSnapshot;
    struct DioramaSceneSnapshot currentSnapshot = sSnapshot;
    struct DioramaObjectSnapshot previous = MakeObject();
    struct DioramaObjectSnapshot current = MakeObject();

    previous.previousMapX = previous.currentMapX = 1;
    previous.previousMapY = previous.currentMapY = 1;
    current.previousMapX = current.currentMapX = 2;
    current.previousMapY = current.currentMapY = 1;
    currentSnapshot.sequence = previousSnapshot.sequence + 1;
    CHECK(!DioramaSprite_CanInterpolate(&previousSnapshot, &previous,
                                         &currentSnapshot, &current));
    current.previousMapX = 1;
    current.mapPixelOffsetX = 0;
    current.currentMapX = 2;
    currentSnapshot.sequence = previousSnapshot.sequence + 1;
    CHECK(!DioramaSprite_CanInterpolate(&previousSnapshot, &previous,
                                         &currentSnapshot, &current));
    current.mapPixelOffsetX = -15;
    CHECK(DioramaSprite_CanInterpolate(&previousSnapshot, &previous,
                                        &currentSnapshot, &current));
    currentSnapshot.sequence = previousSnapshot.sequence + 2;
    CHECK(!DioramaSprite_CanInterpolate(&previousSnapshot, &previous,
                                         &currentSnapshot, &current));
}

static void TestDepthOrder(void)
{
    struct DioramaSpritePose farPose = {0, 0, 5, 0};
    struct DioramaSpritePose nearPose = {0, 0, 2, 0};

    CHECK(DioramaSprite_CompareDepth(&farPose, 1, 10, 1,
                                     &nearPose, 1, 10, 2, 0, 0.70758444f) < 0);
    CHECK(DioramaSprite_CompareDepth(&nearPose, 2, 10, 1,
                                     &nearPose, 1, 10, 2, 0, 0.70758444f) < 0);
    CHECK(DioramaSprite_CompareDepth(&nearPose, 1, 10, 1,
                                     &nearPose, 1, 10, 2, 0, 0.70758444f) > 0);
    farPose.y = 2.0f;
    farPose.z = 1.0f;
    nearPose.y = 0.0f;
    nearPose.z = 0.0f;
    CHECK(DioramaSprite_CompareDepth(&farPose, 1, 10, 1,
                                     &nearPose, 1, 10, 2, 0, 0.34906585f) < 0);
    CHECK(DioramaSprite_CompareDepth(&farPose, 1, 10, 1,
                                     &nearPose, 1, 10, 2, 0, 1.22173048f) > 0);
}

static void TestAttachedScreenOffset(void)
{
    struct DioramaSpritePose pose = {2.0f, 3.0f, 4.0f, 1.0f};
    const float pitch = 0.70758444f;

    pose = DioramaSprite_ApplyScreenOffset(pose, 8, 16, pitch);
    CHECK_CLOSE(pose.x, 2.5f);
    CHECK_CLOSE(pose.y, 3.0f - cosf(pitch));
    CHECK_CLOSE(pose.z, 4.0f - sinf(pitch));
    CHECK_CLOSE(pose.groundY, 1.0f);
}

int main(void)
{
    InitSnapshot();
    TestDimensions();
    TestDecodeAndFlips();
    TestSupportAndFrameKeys();
    TestPoseAndInterpolation();
    TestJumpLift();
    TestCardinalMovementAnchors();
    TestInterpolationDiscontinuities();
    TestDepthOrder();
    TestAttachedScreenOffset();
    puts("sprite frame tests passed");
    return EXIT_SUCCESS;
}
