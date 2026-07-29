#ifdef ENABLE_DIORAMA

#include <string.h>

#include "global.h"
#include "diorama/scene_snapshot.h"
#include "event_object_movement.h"
#include "field_camera.h"
#include "field_message_box.h"
#include "fieldmap.h"
#include "gba/defines.h"
#include "main.h"
#include "menu.h"
#include "overworld.h"
#include "palette.h"
#include "script.h"
#include "sprite.h"
#include "window.h"

#define CELL_RADIUS 16

enum
{
    OBJECT_FLAG_PLAYER     = 1 << 0,
    OBJECT_FLAG_INVISIBLE  = 1 << 1,
    OBJECT_FLAG_OFFSCREEN  = 1 << 2,
    OBJECT_FLAG_REFLECTION = 1 << 3,
    OBJECT_FLAG_SHADOW     = 1 << 4,
};

static struct DioramaSceneSnapshot sDraft;
static u16 sPreviousPalette[DIORAMA_FADED_PALETTE_ENTRIES];
static u8 sPreviousTileGraphics[DIORAMA_TILE_GRAPHICS_SIZE];
static u32 sMapGeneration;
static u32 sPaletteGeneration;
static u32 sTilesetAnimationGeneration;
static bool sHasPreviousPalette;
static bool sHasPreviousTileGraphics;
static bool sPublishedThisFrame;
static bool sMapChanged;
static s8 sPreviousMapGroup;
static s8 sPreviousMapNum;
static u16 sPreviousMapLayoutId;

static enum DioramaSceneKind GetOverworldSceneKind(u32 *fallbackReasons)
{
    if (gMain.callback2 != CB2_Overworld)
    {
        *fallbackReasons |= DIORAMA_FALLBACK_UNSUPPORTED_SCENE;
        return DIORAMA_SCENE_TRANSITION;
    }
    if (gPaletteFade.active)
    {
        *fallbackReasons |= DIORAMA_FALLBACK_PALETTE_FADE;
        return DIORAMA_SCENE_TRANSITION;
    }
    if (!IsFieldMessageBoxHidden())
    {
        *fallbackReasons |= DIORAMA_FALLBACK_MODAL_UI;
        return DIORAMA_SCENE_DIALOGUE;
    }
    if (GetStartMenuWindowId() != WINDOW_NONE)
    {
        *fallbackReasons |= DIORAMA_FALLBACK_MODAL_UI;
        return DIORAMA_SCENE_MENU;
    }
    if (ArePlayerFieldControlsLocked())
        return DIORAMA_SCENE_OVERWORLD_SCRIPTED;
    return DIORAMA_SCENE_OVERWORLD_FREE;
}

static bool CopyCells(struct DioramaSceneSnapshot *snapshot)
{
    int x;
    int y;
    int index = 0;

    snapshot->gridOriginX = snapshot->cameraMapX - CELL_RADIUS;
    snapshot->gridOriginY = snapshot->cameraMapY - CELL_RADIUS;
    for (y = 0; y < DIORAMA_GRID_HEIGHT; y++)
    {
        for (x = 0; x < DIORAMA_GRID_WIDTH; x++, index++)
        {
            struct DioramaCellSnapshot *cell = &snapshot->cells[index];
            int mapX = snapshot->gridOriginX + x;
            int mapY = snapshot->gridOriginY + y;

            cell->mapX = mapX;
            cell->mapY = mapY;
            cell->metatileId = MapGridGetMetatileIdAt(mapX, mapY);
            cell->behavior = MapGridGetMetatileBehaviorAt(mapX, mapY);
            cell->layerType = MapGridGetMetatileLayerTypeAt(mapX, mapY);
            cell->collision = MapGridGetCollisionAt(mapX, mapY);
            cell->elevation = MapGridGetElevationAt(mapX, mapY);
            cell->flags = 0;
            if (cell->metatileId < NUM_METATILES_IN_PRIMARY
             && gMapHeader.mapLayout->primaryTileset != NULL
             && gMapHeader.mapLayout->primaryTileset->metatiles != NULL)
            {
                memcpy(cell->tileEntries,
                       gMapHeader.mapLayout->primaryTileset->metatiles
                           + cell->metatileId * DIORAMA_METATILE_ENTRY_COUNT,
                       sizeof(cell->tileEntries));
            }
            else if (cell->metatileId < NUM_METATILES_TOTAL
                  && gMapHeader.mapLayout->secondaryTileset != NULL
                  && gMapHeader.mapLayout->secondaryTileset->metatiles != NULL)
            {
                memcpy(cell->tileEntries,
                       gMapHeader.mapLayout->secondaryTileset->metatiles
                           + (cell->metatileId - NUM_METATILES_IN_PRIMARY)
                           * DIORAMA_METATILE_ENTRY_COUNT,
                       sizeof(cell->tileEntries));
            }
            else
            {
                memset(cell->tileEntries, 0, sizeof(cell->tileEntries));
                return false;
            }
        }
    }
    snapshot->visibleCellCount = DIORAMA_MAX_VISIBLE_CELLS;
    return true;
}

static void CopyTileGraphics(struct DioramaSceneSnapshot *snapshot)
{
    const u8 *tileGraphics = (const u8 *)BG_VRAM;

    if (!sHasPreviousTileGraphics
     || memcmp(sPreviousTileGraphics, tileGraphics, sizeof(sPreviousTileGraphics)) != 0)
    {
        memcpy(sPreviousTileGraphics, tileGraphics, sizeof(sPreviousTileGraphics));
        sTilesetAnimationGeneration++;
        sHasPreviousTileGraphics = true;
    }
    memcpy(snapshot->tileGraphics, tileGraphics, sizeof(snapshot->tileGraphics));
    snapshot->tilesetAnimationGeneration = sTilesetAnimationGeneration;
}

static void CopyObjects(struct DioramaSceneSnapshot *snapshot)
{
    int i;

    for (i = 0; i < OBJECT_EVENTS_COUNT; i++)
    {
        const struct ObjectEvent *object = &gObjectEvents[i];
        struct DioramaObjectSnapshot *copy;
        const struct Sprite *sprite;

        if (!object->active || object->spriteId >= MAX_SPRITES)
            continue;
        sprite = &gSprites[object->spriteId];
        if (!sprite->inUse || snapshot->objectCount >= DIORAMA_MAX_OBJECTS)
            continue;

        copy = &snapshot->objects[snapshot->objectCount++];
        copy->active = TRUE;
        copy->localId = object->localId;
        copy->graphicsId = object->graphicsId;
        copy->spriteId = object->spriteId;
        copy->elevation = object->currentElevation;
        copy->previousElevation = object->previousElevation;
        copy->facingDirection = object->facingDirection;
        copy->movementDirection = object->movementDirection;
        copy->flags = (object->isPlayer ? OBJECT_FLAG_PLAYER : 0)
                    | (object->invisible ? OBJECT_FLAG_INVISIBLE : 0)
                    | (object->offScreen ? OBJECT_FLAG_OFFSCREEN : 0)
                    | (object->hasReflection ? OBJECT_FLAG_REFLECTION : 0)
                    | (object->hasShadow ? OBJECT_FLAG_SHADOW : 0);
        copy->currentMapX = object->currentCoords.x;
        copy->currentMapY = object->currentCoords.y;
        copy->previousMapX = object->previousCoords.x;
        copy->previousMapY = object->previousCoords.y;
        copy->screenX = sprite->x + sprite->x2;
        copy->screenY = sprite->y + sprite->y2;
        copy->spriteX2 = sprite->x2;
        copy->spriteY2 = sprite->y2;
        copy->oamAttr0 = sprite->oam.y
                       | (sprite->oam.affineMode << 8)
                       | (sprite->oam.objMode << 10)
                       | (sprite->oam.mosaic << 12)
                       | (sprite->oam.bpp << 13)
                       | (sprite->oam.shape << 14);
        copy->oamAttr1 = sprite->oam.x | (sprite->oam.matrixNum << 9) | (sprite->oam.size << 14);
        copy->oamAttr2 = sprite->oam.tileNum | (sprite->oam.priority << 10) | (sprite->oam.paletteNum << 12);
        copy->tileNum = sprite->oam.tileNum;
        copy->paletteNum = sprite->oam.paletteNum;
        copy->oamShape = sprite->oam.shape;
        copy->oamSize = sprite->oam.size;
        copy->hFlip = sprite->oam.affineMode == ST_OAM_AFFINE_OFF
                   && (sprite->oam.matrixNum & ST_OAM_HFLIP) != 0;
        copy->vFlip = sprite->oam.affineMode == ST_OAM_AFFINE_OFF
                   && (sprite->oam.matrixNum & ST_OAM_VFLIP) != 0;
        copy->priority = sprite->oam.priority;
        copy->subpriority = sprite->subpriority;
        copy->animNum = sprite->animNum;
        copy->animCmdIndex = sprite->animCmdIndex;
    }
}

void DioramaScene_Init(void)
{
    memset(&sDraft, 0, sizeof(sDraft));
    memset(sPreviousPalette, 0, sizeof(sPreviousPalette));
    memset(sPreviousTileGraphics, 0, sizeof(sPreviousTileGraphics));
    sMapGeneration = 0;
    sPaletteGeneration = 0;
    sTilesetAnimationGeneration = 0;
    sHasPreviousPalette = false;
    sHasPreviousTileGraphics = false;
    sPublishedThisFrame = false;
    sMapChanged = true;
    sPreviousMapGroup = -1;
    sPreviousMapNum = -1;
    sPreviousMapLayoutId = 0;
}

void DioramaScene_BeginFrame(void)
{
    sPublishedThisFrame = false;
}

void DioramaScene_MarkMapChanged(void)
{
    sMapChanged = true;
}

void DioramaScene_PublishOverworld(void)
{
    u16 cameraX;
    u16 cameraY;
    s8 mapGroup = gSaveBlock1Ptr->location.mapGroup;
    s8 mapNum = gSaveBlock1Ptr->location.mapNum;

    memset(&sDraft, 0, sizeof(sDraft));
    sDraft.sceneKind = GetOverworldSceneKind(&sDraft.fallbackReasons);
    sDraft.valid = gMapHeader.mapLayout != NULL;
    if (!sDraft.valid)
        sDraft.fallbackReasons |= DIORAMA_FALLBACK_MAP_UNAVAILABLE;

    sDraft.mapGroup = mapGroup;
    sDraft.mapNum = mapNum;
    sDraft.mapLayoutId = gMapHeader.mapLayoutId;
    sDraft.mapCoordinateOffset = MAP_OFFSET;
    sDraft.mapType = gMapHeader.mapType;
    sDraft.weather = gMapHeader.weather;
    if (sDraft.valid)
    {
        sDraft.mapWidth = gMapHeader.mapLayout->width;
        sDraft.mapHeight = gMapHeader.mapLayout->height;
    }

    if (sMapChanged || mapGroup != sPreviousMapGroup || mapNum != sPreviousMapNum
     || sDraft.mapLayoutId != sPreviousMapLayoutId)
    {
        sMapGeneration++;
        sMapChanged = false;
        sPreviousMapGroup = mapGroup;
        sPreviousMapNum = mapNum;
        sPreviousMapLayoutId = sDraft.mapLayoutId;
    }
    sDraft.mapGeneration = sMapGeneration;
    if (memcmp(sPreviousPalette, gPlttBufferFaded, 256 * sizeof(*sPreviousPalette)) != 0
     || !sHasPreviousPalette)
    {
        memcpy(sPreviousPalette, gPlttBufferFaded, sizeof(sPreviousPalette));
        sPaletteGeneration++;
        sHasPreviousPalette = true;
    }
    memcpy(sDraft.fadedPalette, gPlttBufferFaded, sizeof(sDraft.fadedPalette));
    sDraft.paletteGeneration = sPaletteGeneration;

    if (sDraft.valid)
    {
        CopyTileGraphics(&sDraft);
        GetCameraFocusCoords(&cameraX, &cameraY);
        sDraft.cameraMapX = cameraX;
        sDraft.cameraMapY = cameraY;
        GetCameraOffsetWithPan(&sDraft.cameraPixelX, &sDraft.cameraPixelY);
        sDraft.cameraSubpixelX = gFieldCamera.x;
        sDraft.cameraSubpixelY = gFieldCamera.y;
        if (gFieldCamera.movementSpeedX > 0 && sDraft.cameraSubpixelX > 0)
            sDraft.cameraSubpixelX -= 16;
        else if (gFieldCamera.movementSpeedX < 0 && sDraft.cameraSubpixelX < 0)
            sDraft.cameraSubpixelX += 16;
        if (gFieldCamera.movementSpeedY > 0 && sDraft.cameraSubpixelY > 0)
            sDraft.cameraSubpixelY -= 16;
        else if (gFieldCamera.movementSpeedY < 0 && sDraft.cameraSubpixelY < 0)
            sDraft.cameraSubpixelY += 16;
        GetCameraPan(&sDraft.cameraPanX, &sDraft.cameraPanY);
        sDraft.tilesetResourcesValid = CopyCells(&sDraft);
        if (!sDraft.tilesetResourcesValid)
            sDraft.fallbackReasons |= DIORAMA_FALLBACK_TILESET_UNAVAILABLE;
        CopyObjects(&sDraft);
    }
    DioramaSnapshotExchange_Publish(&sDraft);
    sPublishedThisFrame = true;
}

void DioramaScene_EndFrame(bool inBattle)
{
    if (!sPublishedThisFrame)
    {
        memset(&sDraft, 0, sizeof(sDraft));
        sDraft.sceneKind = inBattle ? DIORAMA_SCENE_BATTLE : DIORAMA_SCENE_UNAVAILABLE;
        sDraft.fallbackReasons = DIORAMA_FALLBACK_INVALID_SNAPSHOT
                               | DIORAMA_FALLBACK_NON_OVERWORLD;
        DioramaSnapshotExchange_Publish(&sDraft);
    }
}

#endif
