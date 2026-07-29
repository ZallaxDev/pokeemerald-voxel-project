#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL2/SDL.h>

#include "diorama/scene_snapshot.h"

#define CHECK(condition) Check((condition), #condition, __LINE__)

static void Check(bool condition, const char *expression, int line)
{
    if (!condition)
    {
        fprintf(stderr, "test_snapshot.c:%d: check failed: %s\n", line, expression);
        exit(EXIT_FAILURE);
    }
}

static void FillSnapshot(struct DioramaSceneSnapshot *snapshot, uint8_t seed)
{
    unsigned i;

    memset(snapshot, seed, sizeof(*snapshot));
    snapshot->sequence = UINT64_C(0xfeedface);
    snapshot->mapGeneration = 1000 + seed;
    snapshot->mapEditGeneration = 1500 + seed;
    snapshot->paletteGeneration = 2000 + seed;
    snapshot->tilesetAnimationGeneration = 3000 + seed;
    snapshot->valid = 1;
    snapshot->tilesetResourcesValid = 1;
    snapshot->sceneKind = DIORAMA_SCENE_OVERWORLD_FREE;
    snapshot->fallbackReasons = DIORAMA_FALLBACK_NONE;
    snapshot->mapGroup = seed;
    snapshot->mapNum = seed + 1;
    snapshot->mapLayoutId = 4000 + seed;
    snapshot->mapWidth = 40 + seed;
    snapshot->mapHeight = 30 + seed;
    snapshot->mapType = seed + 2;
    snapshot->weather = seed + 3;
    snapshot->cameraMapX = -100 + seed;
    snapshot->cameraMapY = 100 - seed;
    snapshot->cameraPixelX = -7;
    snapshot->cameraPixelY = 9;
    snapshot->gridOriginX = snapshot->cameraMapX - 16;
    snapshot->gridOriginY = snapshot->cameraMapY - 16;
    snapshot->visibleCellCount = DIORAMA_MAX_VISIBLE_CELLS;
    snapshot->objectCount = DIORAMA_MAX_OBJECTS;

    for (i = 0; i < DIORAMA_MAX_VISIBLE_CELLS; i++)
    {
        snapshot->cells[i].mapX = snapshot->gridOriginX + i % DIORAMA_GRID_WIDTH;
        snapshot->cells[i].mapY = snapshot->gridOriginY + i / DIORAMA_GRID_WIDTH;
        snapshot->cells[i].sourceMapX = i % DIORAMA_GRID_WIDTH;
        snapshot->cells[i].sourceMapY = i / DIORAMA_GRID_WIDTH;
        snapshot->cells[i].sourceMapGroup = seed;
        snapshot->cells[i].sourceMapNum = seed + 1;
        snapshot->cells[i].sourceLayoutId = 4000 + seed;
        snapshot->cells[i].flags = DIORAMA_CELL_SOURCE_VALID;
        snapshot->cells[i].metatileId = i + seed;
        snapshot->cells[i].behavior = i ^ seed;
        snapshot->cells[i].elevation = (i + seed) & 15;
    }
    snapshot->dirtyCellCount = 2;
    snapshot->dirtyCells[0].mapX = -8;
    snapshot->dirtyCells[0].mapY = 7;
    snapshot->dirtyCells[1].mapX = 8;
    snapshot->dirtyCells[1].mapY = 15;

    for (i = 0; i < DIORAMA_MAX_OBJECTS; i++)
    {
        snapshot->objects[i].active = 1;
        snapshot->objects[i].localId = i;
        snapshot->objects[i].spriteId = i + seed;
        snapshot->objects[i].currentMapX = i * 3 - seed;
        snapshot->objects[i].oamAttr2 = (i * 37) | seed;
        snapshot->objects[i].animCmdIndex = seed + i;
    }

    for (i = 0; i < DIORAMA_FADED_PALETTE_ENTRIES; i++)
        snapshot->fadedPalette[i] = (i * 29 + seed) & 0x7fff;
}

static void TestInitialEmpty(void)
{
    struct DioramaSceneSnapshot output;

    DioramaSnapshotExchange_TestReset();
    memset(&output, 0xa5, sizeof(output));
    CHECK(!DioramaSnapshotExchange_CopyLatest(&output));
}

static void TestCompleteImmutableCopy(void)
{
    struct DioramaSceneSnapshot input;
    struct DioramaSceneSnapshot expected;
    struct DioramaSceneSnapshot output;

    DioramaSnapshotExchange_TestReset();
    FillSnapshot(&input, 11);
    memcpy(&expected, &input, sizeof(expected));
    expected.sequence = 1;
    DioramaSnapshotExchange_Publish(&input);
    memset(&input, 0xcc, sizeof(input));

    CHECK(DioramaSnapshotExchange_CopyLatest(&output));
    CHECK(memcmp(&output, &expected, sizeof(output)) == 0);
}

static void TestMonotonicLatestPublication(void)
{
    struct DioramaSceneSnapshot input;
    struct DioramaSceneSnapshot output;
    uint64_t previousSequence = 0;
    unsigned i;

    DioramaSnapshotExchange_TestReset();
    for (i = 0; i < 12; i++)
    {
        FillSnapshot(&input, i);
        DioramaSnapshotExchange_Publish(&input);
        CHECK(DioramaSnapshotExchange_CopyLatest(&output));
        CHECK(output.sequence > previousSequence);
        CHECK(output.sequence == i + 1);
        CHECK(output.mapGroup == i);
        previousSequence = output.sequence;
    }

    FillSnapshot(&input, 90);
    DioramaSnapshotExchange_Publish(&input);
    FillSnapshot(&input, 91);
    DioramaSnapshotExchange_Publish(&input);
    CHECK(DioramaSnapshotExchange_CopyLatest(&output));
    CHECK(output.sequence == 14);
    CHECK(output.mapGroup == 91);
}

struct PinnedPublishContext
{
    struct DioramaSceneSnapshot second;
    struct DioramaSceneSnapshot third;
};

static void PublishWhileReaderPinned(void *userdata)
{
    struct PinnedPublishContext *context = userdata;

    DioramaSnapshotExchange_Publish(&context->second);
    DioramaSnapshotExchange_Publish(&context->third);
    memset(&context->second, 0xdd, sizeof(context->second));
    memset(&context->third, 0xee, sizeof(context->third));
    DioramaSnapshotExchange_TestSetPinnedHook(NULL, NULL);
}

static void TestPinnedReaderSafety(void)
{
    struct DioramaSceneSnapshot first;
    struct DioramaSceneSnapshot expectedFirst;
    struct DioramaSceneSnapshot expectedThird;
    struct DioramaSceneSnapshot output;
    struct PinnedPublishContext context;

    DioramaSnapshotExchange_TestReset();
    FillSnapshot(&first, 21);
    FillSnapshot(&context.second, 22);
    FillSnapshot(&context.third, 23);
    memcpy(&expectedFirst, &first, sizeof(expectedFirst));
    memcpy(&expectedThird, &context.third, sizeof(expectedThird));
    expectedFirst.sequence = 1;
    expectedThird.sequence = 3;

    DioramaSnapshotExchange_Publish(&first);
    DioramaSnapshotExchange_TestSetPinnedHook(PublishWhileReaderPinned, &context);
    CHECK(DioramaSnapshotExchange_CopyLatest(&output));
    CHECK(memcmp(&output, &expectedFirst, sizeof(output)) == 0);
    CHECK(DioramaSnapshotExchange_CopyLatest(&output));
    CHECK(memcmp(&output, &expectedThird, sizeof(output)) == 0);
}

static void TestInvalidFallbackIntegrity(void)
{
    struct DioramaSceneSnapshot input;
    struct DioramaSceneSnapshot expected;
    struct DioramaSceneSnapshot output;

    DioramaSnapshotExchange_TestReset();
    FillSnapshot(&input, 31);
    input.valid = 0;
    input.sceneKind = DIORAMA_SCENE_SPECIAL;
    input.fallbackReasons = DIORAMA_FALLBACK_INVALID_SNAPSHOT
                          | DIORAMA_FALLBACK_UNSUPPORTED_SCENE
                          | DIORAMA_FALLBACK_GRID_INCOMPLETE;
    input.visibleCellCount = 17;
    input.objectCount = 0;
    memcpy(&expected, &input, sizeof(expected));
    expected.sequence = 1;

    DioramaSnapshotExchange_Publish(&input);
    CHECK(DioramaSnapshotExchange_CopyLatest(&output));
    CHECK(memcmp(&output, &expected, sizeof(output)) == 0);
    CHECK(!output.valid);
    CHECK(output.fallbackReasons == input.fallbackReasons);
    CHECK(!DioramaSnapshot_CanRenderGrid(&output));
}

static void TestGridEligibility(void)
{
    struct DioramaSceneSnapshot snapshot;

    FillSnapshot(&snapshot, 41);
    CHECK(DioramaSnapshot_CanRenderGrid(&snapshot));
    snapshot.sceneKind = DIORAMA_SCENE_OVERWORLD_SCRIPTED;
    CHECK(!DioramaSnapshot_CanRenderGrid(&snapshot));
    snapshot.sceneKind = DIORAMA_SCENE_MENU;
    CHECK(DioramaSnapshot_CanRenderGrid(&snapshot));
    snapshot.sceneKind = DIORAMA_SCENE_DIALOGUE;
    CHECK(DioramaSnapshot_CanRenderGrid(&snapshot));
    snapshot.sceneKind = DIORAMA_SCENE_OVERWORLD_FREE;
    snapshot.fallbackReasons = DIORAMA_FALLBACK_MODAL_UI;
    CHECK(!DioramaSnapshot_CanRenderGrid(&snapshot));
    snapshot.fallbackReasons = DIORAMA_FALLBACK_NONE;
    snapshot.visibleCellCount--;
    CHECK(!DioramaSnapshot_CanRenderGrid(&snapshot));
    snapshot.visibleCellCount = DIORAMA_MAX_VISIBLE_CELLS;
    snapshot.objectCount = DIORAMA_MAX_OBJECTS + 1;
    CHECK(!DioramaSnapshot_CanRenderGrid(&snapshot));
    snapshot.objectCount = DIORAMA_MAX_OBJECTS;
    snapshot.tilesetResourcesValid = 0;
    CHECK(DioramaSnapshot_CanRenderGrid(&snapshot));
    CHECK(!DioramaSnapshot_CanRenderFlatMap(&snapshot));
}

struct StressContext
{
    SDL_atomic_t running;
    SDL_atomic_t failed;
    SDL_atomic_t reads;
};

static int StressWriter(void *userdata)
{
    struct StressContext *context = userdata;
    struct DioramaSceneSnapshot snapshot;
    unsigned i;

    for (i = 0; i < 20000; i++)
    {
        FillSnapshot(&snapshot, i % 100);
        DioramaSnapshotExchange_Publish(&snapshot);
    }
    for (i = 0; i < 10000 && SDL_AtomicGet(&context->reads) == 0; i++)
        SDL_Delay(0);
    if (SDL_AtomicGet(&context->reads) == 0)
        SDL_AtomicSet(&context->failed, 1);
    SDL_AtomicSet(&context->running, 0);
    return 0;
}

static int StressReader(void *userdata)
{
    struct StressContext *context = userdata;
    struct DioramaSceneSnapshot snapshot;
    uint64_t previousSequence = 0;

    while (SDL_AtomicGet(&context->running))
    {
        if (!DioramaSnapshotExchange_CopyLatest(&snapshot))
            continue;
        if (snapshot.sequence < previousSequence
         || snapshot.visibleCellCount != DIORAMA_MAX_VISIBLE_CELLS
         || snapshot.cells[0].metatileId != snapshot.mapGroup
         || snapshot.cells[1088].metatileId != (uint16_t)(1088 + snapshot.mapGroup)
         || snapshot.fadedPalette[511] != (uint16_t)((511 * 29 + snapshot.mapGroup) & 0x7fff))
        {
            SDL_AtomicSet(&context->failed, 1);
            break;
        }
        previousSequence = snapshot.sequence;
        SDL_AtomicIncRef(&context->reads);
    }
    return 0;
}

static void TestConcurrentStress(void)
{
    struct StressContext context;
    SDL_Thread *writer;
    SDL_Thread *reader;

    DioramaSnapshotExchange_TestReset();
    SDL_AtomicSet(&context.running, 1);
    SDL_AtomicSet(&context.failed, 0);
    SDL_AtomicSet(&context.reads, 0);
    reader = SDL_CreateThread(StressReader, "snapshot-reader", &context);
    CHECK(reader != NULL);
    writer = SDL_CreateThread(StressWriter, "snapshot-writer", &context);
    CHECK(writer != NULL);
    SDL_WaitThread(writer, NULL);
    SDL_WaitThread(reader, NULL);
    CHECK(!SDL_AtomicGet(&context.failed));
    CHECK(SDL_AtomicGet(&context.reads) > 0);
}

int main(void)
{
    TestInitialEmpty();
    TestCompleteImmutableCopy();
    TestMonotonicLatestPublication();
    TestPinnedReaderSafety();
    TestInvalidFallbackIntegrity();
    TestGridEligibility();
    TestConcurrentStress();
    DioramaSnapshotExchange_Shutdown();
    puts("snapshot exchange tests passed");
    return EXIT_SUCCESS;
}
