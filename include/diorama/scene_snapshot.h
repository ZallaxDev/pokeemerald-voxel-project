#ifndef GUARD_DIORAMA_SCENE_SNAPSHOT_H
#define GUARD_DIORAMA_SCENE_SNAPSHOT_H

#include <stdbool.h>
#include <stdint.h>

#include "diorama/ui_overlay.h"

#define DIORAMA_SNAPSHOT_COUNT 3
#define DIORAMA_GRID_WIDTH 33
#define DIORAMA_GRID_HEIGHT 33
#define DIORAMA_MAX_VISIBLE_CELLS (DIORAMA_GRID_WIDTH * DIORAMA_GRID_HEIGHT)
#define DIORAMA_MAX_OBJECTS 16
#define DIORAMA_FADED_PALETTE_ENTRIES 512
#define DIORAMA_TILE_COUNT 1024
#define DIORAMA_TILE_BYTES 32
#define DIORAMA_TILE_GRAPHICS_SIZE (DIORAMA_TILE_COUNT * DIORAMA_TILE_BYTES)
#define DIORAMA_OBJ_FRAME_MAX_BYTES 2048
#define DIORAMA_METATILE_ENTRY_COUNT 8
#define DIORAMA_MAX_DIRTY_CELLS 64

enum DioramaCellFlags
{
    DIORAMA_CELL_SOURCE_VALID = 1 << 0,
    DIORAMA_CELL_CONNECTED = 1 << 1,
};

enum DioramaObjectFlags
{
    DIORAMA_OBJECT_PLAYER     = 1 << 0,
    DIORAMA_OBJECT_INVISIBLE  = 1 << 1,
    DIORAMA_OBJECT_OFFSCREEN  = 1 << 2,
    DIORAMA_OBJECT_REFLECTION = 1 << 3,
    DIORAMA_OBJECT_SHADOW     = 1 << 4,
    DIORAMA_OBJECT_SUBSPRITES = 1 << 5,
    DIORAMA_OBJECT_FRAME_INVALID = 1 << 6,
};

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
    DIORAMA_FALLBACK_TILESET_UNAVAILABLE = 1u << 11,
    DIORAMA_FALLBACK_UNSUPPORTED_WEATHER = 1u << 12
};

struct DioramaCellSnapshot
{
    int16_t mapX;
    int16_t mapY;
    int16_t sourceMapX;
    int16_t sourceMapY;
    uint16_t metatileId;
    uint16_t sourceLayoutId;
    uint8_t behavior;
    uint8_t layerType;
    uint8_t collision;
    uint8_t elevation;
    uint8_t sourceMapGroup;
    uint8_t sourceMapNum;
    uint16_t flags;
    uint16_t tileEntries[DIORAMA_METATILE_ENTRY_COUNT];
};

struct DioramaDirtyCell
{
    int16_t mapX;
    int16_t mapY;
};

struct DioramaObjectSnapshot
{
    uint8_t active;
    uint8_t localId;
    uint8_t graphicsId;
    uint8_t spriteId;
    uint8_t elevation;
    uint8_t previousElevation;
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
    int16_t mapPixelOffsetX;
    int16_t mapPixelOffsetY;
    int8_t centerToCornerX;
    int8_t centerToCornerY;
    uint8_t width;
    uint8_t height;
    uint8_t shadowSize;
    uint8_t reflectionHidden;
    uint8_t reflectionPaletteNum;
    int16_t reflectionOffsetY;
    uint8_t affineMode;
    uint8_t objMode;
    uint8_t bpp;
    uint16_t frameSize;
    uint64_t frameHash;
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
    uint8_t oamOrder;
    uint8_t animNum;
    uint8_t animCmdIndex;
    uint8_t frameGraphics[DIORAMA_OBJ_FRAME_MAX_BYTES];
};

struct DioramaSceneSnapshot
{
    uint64_t sequence;
    uint32_t mapGeneration;
    uint32_t mapEditGeneration;
    uint32_t paletteGeneration;
    uint32_t objPaletteGeneration;
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
    uint8_t nextWeather;
    uint8_t weatherTransitionComplete;
    uint8_t rainVisibleCount;
    uint16_t fogScrollOffset;
    uint16_t weatherBlendEVA;
    uint16_t weatherBlendEVB;
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
    uint8_t dirtyCellCount;
    uint8_t dirtyOverflow;
    struct DioramaDirtyCell dirtyCells[DIORAMA_MAX_DIRTY_CELLS];
    uint8_t objectCount;
    struct DioramaObjectSnapshot objects[DIORAMA_MAX_OBJECTS];
    uint8_t uiFlags;
    uint8_t uiRectCount;
    struct DioramaUiRect uiRects[DIORAMA_MAX_UI_RECTS];
    uint32_t fieldOamMask[DIORAMA_OAM_MASK_WORDS];
    uint32_t interfaceOamMask[DIORAMA_OAM_MASK_WORDS];
    uint16_t uiBgControl;
    uint16_t uiBgHOffset;
    uint16_t uiBgVOffset;
    uint8_t uiBgTileGraphics[DIORAMA_UI_BG_TILE_BYTES];
    uint16_t uiBgTilemap[DIORAMA_UI_BG_MAP_BYTES / sizeof(uint16_t)];
    uint8_t playerAvatarFlags;
    uint8_t surfBlobValid;
    int16_t surfBlobOffsetX;
    int16_t surfBlobOffsetY;
    struct DioramaObjectSnapshot surfBlob;
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
void DioramaScene_MarkCellDirty(int16_t mapX, int16_t mapY);

#ifdef DIORAMA_TEST
typedef void (*DioramaSnapshotTestPinnedHook)(void *userdata);
void DioramaSnapshotExchange_TestReset(void);
void DioramaSnapshotExchange_TestSetPinnedHook(DioramaSnapshotTestPinnedHook hook, void *userdata);
#endif

#endif
