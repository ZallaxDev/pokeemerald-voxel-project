#ifndef GUARD_DIORAMA_SCENE_SNAPSHOT_H
#define GUARD_DIORAMA_SCENE_SNAPSHOT_H

#include <stdbool.h>
#include <stdint.h>

#define DIORAMA_SNAPSHOT_COUNT 3
#define DIORAMA_GRID_WIDTH 33
#define DIORAMA_GRID_HEIGHT 33
#define DIORAMA_MAX_VISIBLE_CELLS (DIORAMA_GRID_WIDTH * DIORAMA_GRID_HEIGHT)
#define DIORAMA_MAX_OBJECTS 16
#define DIORAMA_FADED_PALETTE_ENTRIES 512
#define DIORAMA_TILE_COUNT 1024
#define DIORAMA_TILE_BYTES 32
#define DIORAMA_TILE_GRAPHICS_SIZE (DIORAMA_TILE_COUNT * DIORAMA_TILE_BYTES)
#define DIORAMA_METATILE_ENTRY_COUNT 8

enum DioramaRenderMode
{
    DIORAMA_RENDER_CLASSIC_2D,
    DIORAMA_RENDER_AUTO,
    DIORAMA_RENDER_FORCE_3D,
    DIORAMA_RENDER_DEBUG
};

enum DioramaSceneKind
{
    DIORAMA_SCENE_UNAVAILABLE,
    DIORAMA_SCENE_OVERWORLD_FREE,
    DIORAMA_SCENE_OVERWORLD_SCRIPTED,
    DIORAMA_SCENE_DIALOGUE,
    DIORAMA_SCENE_MENU,
    DIORAMA_SCENE_BATTLE,
    DIORAMA_SCENE_TRANSITION,
    DIORAMA_SCENE_SPECIAL
};

enum DioramaFallbackReason
{
    DIORAMA_FALLBACK_NONE = 0,
    DIORAMA_FALLBACK_INVALID_SNAPSHOT = 1u << 0,
    DIORAMA_FALLBACK_UNSUPPORTED_SCENE = 1u << 1,
    DIORAMA_FALLBACK_MAP_UNAVAILABLE = 1u << 2,
    DIORAMA_FALLBACK_GRID_INCOMPLETE = 1u << 3,
    DIORAMA_FALLBACK_OBJECT_OVERFLOW = 1u << 4,
    DIORAMA_FALLBACK_PALETTE_UNAVAILABLE = 1u << 5,
    DIORAMA_FALLBACK_RENDERER_UNAVAILABLE = 1u << 6,
    DIORAMA_FALLBACK_INTERNAL_ERROR = 1u << 7,
    DIORAMA_FALLBACK_PALETTE_FADE = 1u << 8,
    DIORAMA_FALLBACK_MODAL_UI = 1u << 9,
    DIORAMA_FALLBACK_NON_OVERWORLD = 1u << 10,
    DIORAMA_FALLBACK_TILESET_UNAVAILABLE = 1u << 11
};

struct DioramaCellSnapshot
{
    int16_t mapX;
    int16_t mapY;
    uint16_t metatileId;
    uint8_t behavior;
    uint8_t layerType;
    uint8_t collision;
    uint8_t elevation;
    uint16_t flags;
    uint16_t tileEntries[DIORAMA_METATILE_ENTRY_COUNT];
};

struct DioramaObjectSnapshot
{
    uint8_t active;
    uint8_t localId;
    uint8_t graphicsId;
    uint8_t spriteId;
    uint8_t elevation;
    uint8_t facingDirection;
    uint8_t movementDirection;
    uint8_t flags;
    int16_t currentMapX;
    int16_t currentMapY;
    int16_t previousMapX;
    int16_t previousMapY;
    int16_t screenX;
    int16_t screenY;
    int16_t spriteX2;
    int16_t spriteY2;
    uint16_t oamAttr0;
    uint16_t oamAttr1;
    uint16_t oamAttr2;
    uint16_t tileNum;
    uint8_t paletteNum;
    uint8_t oamShape;
    uint8_t oamSize;
    uint8_t hFlip;
    uint8_t vFlip;
    uint8_t priority;
    uint8_t subpriority;
    uint8_t animNum;
    uint8_t animCmdIndex;
};

struct DioramaSceneSnapshot
{
    uint64_t sequence;
    uint32_t mapGeneration;
    uint32_t paletteGeneration;
    uint32_t tilesetAnimationGeneration;
    uint8_t valid;
    uint8_t tilesetResourcesValid;
    enum DioramaSceneKind sceneKind;
    uint32_t fallbackReasons;
    uint8_t mapGroup;
    uint8_t mapNum;
    uint16_t mapLayoutId;
    uint16_t mapWidth;
    uint16_t mapHeight;
    uint8_t mapCoordinateOffset;
    uint8_t mapType;
    uint8_t weather;
    int16_t cameraMapX;
    int16_t cameraMapY;
    int16_t cameraPixelX;
    int16_t cameraPixelY;
    int8_t cameraSubpixelX;
    int8_t cameraSubpixelY;
    int16_t cameraPanX;
    int16_t cameraPanY;
    int16_t gridOriginX;
    int16_t gridOriginY;
    uint16_t visibleCellCount;
    struct DioramaCellSnapshot cells[DIORAMA_MAX_VISIBLE_CELLS];
    uint8_t objectCount;
    struct DioramaObjectSnapshot objects[DIORAMA_MAX_OBJECTS];
    uint16_t fadedPalette[DIORAMA_FADED_PALETTE_ENTRIES];
    uint8_t tileGraphics[DIORAMA_TILE_GRAPHICS_SIZE];
};

void DioramaSnapshotExchange_Init(void);
void DioramaSnapshotExchange_Shutdown(void);
void DioramaSnapshotExchange_Publish(const struct DioramaSceneSnapshot *snapshot);
bool DioramaSnapshotExchange_CopyLatest(struct DioramaSceneSnapshot *snapshot);
bool DioramaSnapshot_CanRenderGrid(const struct DioramaSceneSnapshot *snapshot);
bool DioramaSnapshot_CanRenderFlatMap(const struct DioramaSceneSnapshot *snapshot);

void DioramaScene_Init(void);
void DioramaScene_BeginFrame(void);
void DioramaScene_PublishOverworld(void);
void DioramaScene_EndFrame(bool inBattle);
void DioramaScene_MarkMapChanged(void);

#ifdef DIORAMA_TEST
typedef void (*DioramaSnapshotTestPinnedHook)(void *userdata);
void DioramaSnapshotExchange_TestReset(void);
void DioramaSnapshotExchange_TestSetPinnedHook(DioramaSnapshotTestPinnedHook hook, void *userdata);
#endif

#endif
