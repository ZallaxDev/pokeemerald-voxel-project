#ifdef ENABLE_DIORAMA

#include <string.h>
#include <SDL2/SDL_atomic.h>

#include "diorama/scene_snapshot.h"

#define DIORAMA_NO_SLOT -1

static struct DioramaSceneSnapshot sSnapshots[DIORAMA_SNAPSHOT_COUNT];
static SDL_atomic_t sPublishedIndex;
static SDL_atomic_t sPinnedIndex;
static SDL_SpinLock sSlotLock;
static uint64_t sNextSequence;

#ifdef DIORAMA_TEST
static DioramaSnapshotTestPinnedHook sPinnedHook;
static void *sPinnedHookUserdata;
#endif

void DioramaSnapshotExchange_Init(void)
{
    memset(sSnapshots, 0, sizeof(sSnapshots));
    sNextSequence = 0;
    sSlotLock = 0;
    SDL_AtomicSet(&sPinnedIndex, DIORAMA_NO_SLOT);
    SDL_AtomicSet(&sPublishedIndex, DIORAMA_NO_SLOT);
#ifdef DIORAMA_TEST
    sPinnedHook = NULL;
    sPinnedHookUserdata = NULL;
#endif
}

void DioramaSnapshotExchange_Shutdown(void)
{
    SDL_AtomicSet(&sPublishedIndex, DIORAMA_NO_SLOT);
    SDL_AtomicSet(&sPinnedIndex, DIORAMA_NO_SLOT);
}

void DioramaSnapshotExchange_Publish(const struct DioramaSceneSnapshot *snapshot)
{
    int publishedIndex;
    int pinnedIndex;
    int writeIndex;

    SDL_AtomicLock(&sSlotLock);
    publishedIndex = SDL_AtomicGet(&sPublishedIndex);
    pinnedIndex = SDL_AtomicGet(&sPinnedIndex);
    for (writeIndex = 0; writeIndex < DIORAMA_SNAPSHOT_COUNT; writeIndex++)
    {
        if (writeIndex != publishedIndex && writeIndex != pinnedIndex)
            break;
    }

    memcpy(&sSnapshots[writeIndex], snapshot, sizeof(*snapshot));
    sSnapshots[writeIndex].sequence = ++sNextSequence;
    SDL_MemoryBarrierRelease();
    SDL_AtomicSet(&sPublishedIndex, writeIndex);
    SDL_AtomicUnlock(&sSlotLock);
}

bool DioramaSnapshotExchange_CopyLatest(struct DioramaSceneSnapshot *snapshot)
{
    int publishedIndex;

    for (;;)
    {
        SDL_AtomicLock(&sSlotLock);
        publishedIndex = SDL_AtomicGet(&sPublishedIndex);
        if (publishedIndex == DIORAMA_NO_SLOT)
        {
            SDL_AtomicUnlock(&sSlotLock);
            return false;
        }

        if (!SDL_AtomicCAS(&sPinnedIndex, DIORAMA_NO_SLOT, publishedIndex))
        {
            SDL_AtomicUnlock(&sSlotLock);
            continue;
        }
        SDL_AtomicUnlock(&sSlotLock);

        if (SDL_AtomicGet(&sPublishedIndex) != publishedIndex)
        {
            SDL_AtomicSet(&sPinnedIndex, DIORAMA_NO_SLOT);
            continue;
        }

        SDL_MemoryBarrierAcquire();
#ifdef DIORAMA_TEST
        if (sPinnedHook != NULL)
            sPinnedHook(sPinnedHookUserdata);
#endif
        memcpy(snapshot, &sSnapshots[publishedIndex], sizeof(*snapshot));
        SDL_AtomicSet(&sPinnedIndex, DIORAMA_NO_SLOT);
        return true;
    }
}

bool DioramaSnapshot_CanRenderGrid(const struct DioramaSceneSnapshot *snapshot)
{
    return snapshot != NULL
        && snapshot->valid
        && snapshot->visibleCellCount == DIORAMA_MAX_VISIBLE_CELLS
        && snapshot->objectCount <= DIORAMA_MAX_OBJECTS
        && snapshot->fallbackReasons == DIORAMA_FALLBACK_NONE
        && snapshot->sceneKind == DIORAMA_SCENE_OVERWORLD_FREE;
}

bool DioramaSnapshot_CanRenderFlatMap(const struct DioramaSceneSnapshot *snapshot)
{
    return DioramaSnapshot_CanRenderGrid(snapshot)
        && snapshot->tilesetResourcesValid;
}

#ifdef DIORAMA_TEST
void DioramaSnapshotExchange_TestReset(void)
{
    DioramaSnapshotExchange_Init();
}

void DioramaSnapshotExchange_TestSetPinnedHook(DioramaSnapshotTestPinnedHook hook, void *userdata)
{
    sPinnedHook = hook;
    sPinnedHookUserdata = userdata;
}
#endif

#endif
