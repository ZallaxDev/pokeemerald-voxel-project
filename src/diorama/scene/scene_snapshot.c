#ifdef ENABLE_DIORAMA

#include <string.h>

#include "global.h"
#include "diorama/map_connection.h"
#include "diorama/scene_snapshot.h"
#include "diorama/sprite_frame.h"
#include "diorama/weather.h"
#include "event_object_movement.h"
#include "field_camera.h"
#include "field_effect_helpers.h"
#include "field_message_box.h"
#include "field_player_avatar.h"
#include "fieldmap.h"
#include "field_weather.h"
#include "gba/defines.h"
#include "main.h"
#include "menu.h"
#include "metatile_behavior.h"
#include "overworld.h"
#include "palette.h"
#include "script.h"
#include "sprite.h"
#include "window.h"

#define CELL_RADIUS 16

static struct DioramaSceneSnapshot sDraft;
static u16 sPreviousPalette[DIORAMA_FADED_PALETTE_ENTRIES];
static u8 sPreviousTileGraphics[DIORAMA_TILE_GRAPHICS_SIZE];
static u32 sMapGeneration;
static u32 sMapEditGeneration;
static u32 sPaletteGeneration;
static u32 sObjPaletteGeneration;
static u32 sTilesetAnimationGeneration;
static bool sHasPreviousPalette;
static bool sHasPreviousTileGraphics;
static bool sPublishedThisFrame;
static bool sMapChanged;
static s8 sPreviousMapGroup;
static s8 sPreviousMapNum;
static u16 sPreviousMapLayoutId;
static struct DioramaDirtyCell sDirtyCells[DIORAMA_MAX_DIRTY_CELLS];
static u8 sDirtyCellCount;
static bool sDirtyOverflow;

static bool HasFieldUiWindow(void)
{
    u8 mapPopupWindowId = GetMapNamePopUpWindowId();
    unsigned i;

    for (i = 1; i < WINDOWS_MAX; i++)
        if (i != mapPopupWindowId && gWindows[i].window.bg != 0xFF)
            return true;
    return false;
}

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
        return DIORAMA_SCENE_DIALOGUE;
    if (GetStartMenuWindowId() != WINDOW_NONE)
        return DIORAMA_SCENE_MENU;
    if (ArePlayerFieldControlsLocked() && HasFieldUiWindow())
        return DIORAMA_SCENE_MENU;
    if (ArePlayerFieldControlsLocked())
        return DIORAMA_SCENE_OVERWORLD_SCRIPTED;
    return DIORAMA_SCENE_OVERWORLD_FREE;
}

static void AddUiRect(struct DioramaSceneSnapshot *snapshot,
                      int x, int y, int width, int height)
{
    struct DioramaUiRect *rect;

    if (snapshot->uiRectCount >= DIORAMA_MAX_UI_RECTS || width <= 0 || height <= 0)
        return;
    rect = &snapshot->uiRects[snapshot->uiRectCount++];
    rect->x = x;
    rect->y = y;
    rect->width = width;
    rect->height = height;
}

static void AddStandardWindowRect(struct DioramaSceneSnapshot *snapshot, u8 windowId)
{
    int left;
    int top;
    int width;
    int height;

    if (windowId == WINDOW_NONE || windowId >= WINDOWS_MAX
     || gWindows[windowId].window.bg == 0xFF)
        return;
    left = GetWindowAttribute(windowId, WINDOW_TILEMAP_LEFT);
    top = GetWindowAttribute(windowId, WINDOW_TILEMAP_TOP);
    width = GetWindowAttribute(windowId, WINDOW_WIDTH);
    height = GetWindowAttribute(windowId, WINDOW_HEIGHT);
    AddUiRect(snapshot, (left - 1) * 8, (top - 1) * 8,
              (width + 2) * 8, (height + 2) * 8);
}

static void MarkOam(u32 *mask, u8 oamOrder)
{
    if (oamOrder < 128)
        mask[oamOrder / 32] |= 1u << (oamOrder % 32);
}

static void GetOamDimensions(const struct OamData *oam, int *width, int *height)
{
    static const u8 dimensions[3][4][2] = {
        {{8, 8}, {16, 16}, {32, 32}, {64, 64}},
        {{16, 8}, {32, 8}, {32, 16}, {64, 32}},
        {{8, 16}, {8, 32}, {16, 32}, {32, 64}},
    };

    if (oam->shape >= 3)
    {
        *width = 0;
        *height = 0;
        return;
    }
    *width = dimensions[oam->shape][oam->size][0];
    *height = dimensions[oam->shape][oam->size][1];
    if (oam->affineMode == ST_OAM_AFFINE_DOUBLE)
    {
        *width *= 2;
        *height *= 2;
    }
}

static void CopyUiProfile(struct DioramaSceneSnapshot *snapshot)
{
    u8 startMenuWindowId = GetStartMenuWindowId();
    u8 mapPopupWindowId = GetMapNamePopUpWindowId();
    bool hasFieldWindow = HasFieldUiWindow();
    unsigned i;

    if (!IsFieldMessageBoxHidden())
    {
        snapshot->uiFlags |= DIORAMA_UI_DIALOGUE;
        AddUiRect(snapshot, 0, 14 * 8, DISPLAY_WIDTH, DISPLAY_HEIGHT - 14 * 8);
    }
    if (startMenuWindowId != WINDOW_NONE)
        snapshot->uiFlags |= DIORAMA_UI_START_MENU;
    if (hasFieldWindow)
        snapshot->uiFlags |= DIORAMA_UI_FIELD_WINDOW;
    if (snapshot->uiFlags
        & (DIORAMA_UI_DIALOGUE | DIORAMA_UI_START_MENU | DIORAMA_UI_FIELD_WINDOW))
    {
        for (i = 1; i < WINDOWS_MAX; i++)
            if (i != mapPopupWindowId)
                AddStandardWindowRect(snapshot, i);
    }
    if (mapPopupWindowId != WINDOW_NONE)
    {
        snapshot->uiFlags |= DIORAMA_UI_MAP_POPUP;
        AddUiRect(snapshot, 0, 0, 12 * 8, 5 * 8);
    }

    if (snapshot->uiRectCount > 0)
    {
        unsigned charBase;

        snapshot->uiBgControl = REG_BG0CNT;
        snapshot->uiBgHOffset = REG_BG0HOFS;
        snapshot->uiBgVOffset = REG_BG0VOFS;
        charBase = (snapshot->uiBgControl >> 2) & 3;
        if (charBase > 2 || (snapshot->uiBgControl & (1 << 7))
         || (snapshot->uiBgControl >> 14) != 0)
        {
            snapshot->fallbackReasons |= DIORAMA_FALLBACK_INTERNAL_ERROR;
        }
        else
        {
            memcpy(snapshot->uiBgTileGraphics,
                   (const void *)BG_CHAR_ADDR(charBase),
                   sizeof(snapshot->uiBgTileGraphics));
            memcpy(snapshot->uiBgTilemap,
                   (const void *)BG_SCREEN_ADDR((snapshot->uiBgControl >> 8) & 0x1F),
                   sizeof(snapshot->uiBgTilemap));
        }
    }

    for (i = 0; i < snapshot->objectCount; i++)
        MarkOam(snapshot->fieldOamMask, snapshot->objects[i].oamOrder);
    if (snapshot->surfBlobValid)
        MarkOam(snapshot->fieldOamMask, snapshot->surfBlob.oamOrder);

    for (i = 0; i < 128; i++)
    {
        const struct OamData *oam = &gMain.oamBuffer[i];
        int x;
        int y;
        int width;
        int height;
        unsigned rectIndex;

        if (oam->affineMode == ST_OAM_AFFINE_ERASE)
            continue;
        if (snapshot->fieldOamMask[i / 32] & (1u << (i % 32)))
            continue;
        x = oam->x >= DISPLAY_WIDTH ? oam->x - 512 : oam->x;
        y = oam->y >= DISPLAY_HEIGHT ? oam->y - 256 : oam->y;
        GetOamDimensions(oam, &width, &height);
        for (rectIndex = 0; rectIndex < snapshot->uiRectCount; rectIndex++)
        {
            if (DioramaUI_RectIntersects(&snapshot->uiRects[rectIndex],
                                         x, y, width, height))
            {
                MarkOam(snapshot->interfaceOamMask, i);
                break;
            }
        }
    }
}

static bool CopyCellVisual(const struct MapLayout *layout, u16 metatileId,
                           u8 collision, u8 elevation,
                           struct DioramaCellSnapshot *cell)
{
    const u16 *attributes;

    cell->metatileId = metatileId;
    cell->collision = collision;
    cell->elevation = elevation;
    if (metatileId < NUM_METATILES_IN_PRIMARY
     && layout->primaryTileset != NULL
     && layout->primaryTileset->metatiles != NULL
     && layout->primaryTileset->metatileAttributes != NULL)
    {
        attributes = &layout->primaryTileset->metatileAttributes[metatileId];
        memcpy(cell->tileEntries,
               layout->primaryTileset->metatiles
                   + metatileId * DIORAMA_METATILE_ENTRY_COUNT,
               sizeof(cell->tileEntries));
    }
    else if (metatileId < NUM_METATILES_TOTAL
          && layout->secondaryTileset != NULL
          && layout->secondaryTileset->metatiles != NULL
          && layout->secondaryTileset->metatileAttributes != NULL)
    {
        u16 secondaryId = metatileId - NUM_METATILES_IN_PRIMARY;

        attributes = &layout->secondaryTileset->metatileAttributes[secondaryId];
        memcpy(cell->tileEntries,
               layout->secondaryTileset->metatiles
                   + secondaryId * DIORAMA_METATILE_ENTRY_COUNT,
               sizeof(cell->tileEntries));
    }
    else
    {
        memset(cell->tileEntries, 0, sizeof(cell->tileEntries));
        return false;
    }
    cell->behavior = UNPACK_BEHAVIOR(*attributes);
    cell->layerType = UNPACK_LAYER_TYPE(*attributes);
    return true;
}

static bool TryCopyConnectedCell(struct DioramaSceneSnapshot *snapshot,
                                 int mapX, int mapY,
                                 struct DioramaCellSnapshot *cell)
{
    const struct MapConnection *connection = GetMapConnectionAtPos(mapX, mapY);
    const struct MapHeader *connectedHeader;
    const struct MapLayout *connectedLayout;
    int16_t connectedX;
    int16_t connectedY;
    u16 entry;

    if (connection == NULL)
        return false;
    connectedHeader = GetMapHeaderFromConnection(connection);
    if (connectedHeader == NULL || connectedHeader->mapLayout == NULL)
        return false;
    connectedLayout = connectedHeader->mapLayout;
    if (connectedLayout->map == NULL
     || connectedLayout->primaryTileset != gMapHeader.mapLayout->primaryTileset
     || connectedLayout->secondaryTileset != gMapHeader.mapLayout->secondaryTileset
     || !DioramaConnection_MapCoordinates(connection->direction, connection->offset,
                                          mapX - MAP_OFFSET, mapY - MAP_OFFSET,
                                          snapshot->mapWidth, snapshot->mapHeight,
                                          connectedLayout->width, connectedLayout->height,
                                          &connectedX, &connectedY))
        return false;

    if (mapX >= 0 && mapY >= 0
     && mapX < gBackupMapLayout.width && mapY < gBackupMapLayout.height
     && gBackupMapLayout.map[mapY * gBackupMapLayout.width + mapX] != MAPGRID_UNDEFINED)
        entry = gBackupMapLayout.map[mapY * gBackupMapLayout.width + mapX];
    else
        entry = connectedLayout->map[connectedY * connectedLayout->width + connectedX];
    cell->sourceMapX = connectedX;
    cell->sourceMapY = connectedY;
    cell->sourceMapGroup = connection->mapGroup;
    cell->sourceMapNum = connection->mapNum;
    cell->sourceLayoutId = connectedHeader->mapLayoutId;
    cell->flags = DIORAMA_CELL_SOURCE_VALID | DIORAMA_CELL_CONNECTED;
    return CopyCellVisual(connectedLayout, UNPACK_METATILE(entry),
                          UNPACK_COLLISION(entry), UNPACK_ELEVATION(entry), cell);
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
            cell->flags = 0;
            if (mapX >= MAP_OFFSET && mapY >= MAP_OFFSET
             && mapX < MAP_OFFSET + snapshot->mapWidth
             && mapY < MAP_OFFSET + snapshot->mapHeight)
            {
                cell->sourceMapX = mapX - MAP_OFFSET;
                cell->sourceMapY = mapY - MAP_OFFSET;
                cell->sourceMapGroup = snapshot->mapGroup;
                cell->sourceMapNum = snapshot->mapNum;
                cell->sourceLayoutId = snapshot->mapLayoutId;
                cell->flags = DIORAMA_CELL_SOURCE_VALID;
            }
            else if (TryCopyConnectedCell(snapshot, mapX, mapY, cell))
                continue;
            if (!CopyCellVisual(gMapHeader.mapLayout, MapGridGetMetatileIdAt(mapX, mapY),
                                MapGridGetCollisionAt(mapX, mapY),
                                MapGridGetElevationAt(mapX, mapY), cell))
                return false;
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

static u64 HashFrame(const u8 *data, u16 size)
{
    u64 hash = UINT64_C(1469598103934665603);
    u16 i;

    for (i = 0; i < size; i++)
        hash = (hash ^ data[i]) * UINT64_C(1099511628211);
    return hash;
}

static bool CopyObjectFrame(struct DioramaObjectSnapshot *copy, const struct Sprite *sprite)
{
    const u8 *source;
    u8 width;
    u8 height;
    u16 expectedSize;

    if (!DioramaSprite_GetDimensions(copy->oamShape, copy->oamSize, &width, &height)
     || copy->bpp != ST_OAM_4BPP)
        return false;
    expectedSize = width * height / 2;
    if (expectedSize > sizeof(copy->frameGraphics))
        return false;
    if (!sprite->usingSheet && sprite->images != NULL && sprite->anims != NULL)
    {
        const union AnimCmd *command = &sprite->anims[sprite->animNum][sprite->animCmdIndex];
        const struct SpriteFrameImage *image;

        if (command->type < 0)
            return false;
        image = &sprite->images[command->frame.imageValue];
        if (image->data == NULL || image->size < expectedSize)
            return false;
        source = image->data;
    }
    else
    {
        u32 byteOffset = sprite->oam.tileNum * DIORAMA_TILE_BYTES;

        if (byteOffset + expectedSize > DIORAMA_TILE_GRAPHICS_SIZE)
            return false;
        source = (const u8 *)OBJ_VRAM0 + byteOffset;
    }
    memcpy(copy->frameGraphics, source, expectedSize);
    copy->frameSize = expectedSize;
    copy->frameHash = HashFrame(copy->frameGraphics, expectedSize);
    return true;
}

static bool HasUnsupportedSubsprites(const struct Sprite *sprite,
                                     const struct ObjectEventGraphicsInfo *graphicsInfo,
                                     u8 *resolvedPriority)
{
    const struct SubspriteTable *table;
    const struct Subsprite *subsprite;
    u8 width;
    u8 height;

    if (sprite->subspriteTables == NULL || sprite->subspriteMode == SUBSPRITES_OFF)
        return false;
    table = &sprite->subspriteTables[sprite->subspriteTableNum];
    if (table->subspriteCount == 0 || table->subsprites == NULL)
        return false;
    if (table->subspriteCount != 1 || graphicsInfo == NULL)
        return true;
    subsprite = &table->subsprites[0];
    if (!DioramaSprite_GetDimensions(subsprite->shape, subsprite->size, &width, &height)
     || width != graphicsInfo->width || height != graphicsInfo->height
     || subsprite->tileOffset != 0
     || subsprite->x != -(s8)(width / 2) || subsprite->y != -(s8)(height / 2))
        return true;
    if (sprite->subspriteMode != SUBSPRITES_IGNORE_PRIORITY)
        *resolvedPriority = subsprite->priority;
    return false;
}

static u8 FindOamOrder(const struct Sprite *sprite, u8 priority)
{
    u8 i;

    for (i = 0; i < gOamLimit; i++)
    {
        const struct OamData *oam = &gMain.oamBuffer[i];

        if (oam->x == sprite->oam.x && oam->y == sprite->oam.y
         && oam->tileNum == sprite->oam.tileNum
         && oam->paletteNum == sprite->oam.paletteNum
         && oam->shape == sprite->oam.shape && oam->size == sprite->oam.size
         && oam->priority == priority)
            return i;
    }
    return 0xFF;
}

static void CopySurfBlob(struct DioramaSceneSnapshot *snapshot)
{
    const struct ObjectEvent *player;
    const struct Sprite *playerSprite;
    const struct Sprite *sprite;
    struct DioramaObjectSnapshot *copy = &snapshot->surfBlob;
    u8 spriteId;

    snapshot->playerAvatarFlags = gPlayerAvatar.flags;
    if (gPlayerAvatar.objectEventId >= OBJECT_EVENTS_COUNT)
        return;
    player = &gObjectEvents[gPlayerAvatar.objectEventId];
    if (!player->active || player->spriteId >= MAX_SPRITES)
        return;
    spriteId = player->fieldEffectSpriteId;
    if (spriteId >= MAX_SPRITES)
        return;
    sprite = &gSprites[spriteId];
    if (!sprite->inUse || sprite->callback != UpdateSurfBlobFieldEffect)
        return;
    playerSprite = &gSprites[player->spriteId];
    memset(copy, 0, sizeof(*copy));
    copy->active = TRUE;
    copy->spriteId = spriteId;
    copy->flags = sprite->invisible ? DIORAMA_OBJECT_INVISIBLE : 0;
    copy->screenX = sprite->x + sprite->x2;
    copy->screenY = sprite->y + sprite->y2;
    copy->affineMode = sprite->oam.affineMode;
    copy->objMode = sprite->oam.objMode;
    copy->bpp = sprite->oam.bpp;
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
    copy->oamOrder = FindOamOrder(sprite, sprite->oam.priority);
    copy->animNum = sprite->animNum;
    copy->animCmdIndex = sprite->animCmdIndex;
    if (!CopyObjectFrame(copy, sprite))
    {
        copy->flags |= DIORAMA_OBJECT_FRAME_INVALID;
        return;
    }
    snapshot->surfBlobOffsetX = copy->screenX - (playerSprite->x + playerSprite->x2);
    snapshot->surfBlobOffsetY = copy->screenY - (playerSprite->y + playerSprite->y2);
    snapshot->surfBlobValid = TRUE;
}

static s16 GetReflectionOffsetY(const struct ObjectEvent *object)
{
    static const s16 bridgeOffsets[] = {12, 28, 44};
    u8 bridgeType = MetatileBehavior_GetBridgeType(object->previousMetatileBehavior);

    if (bridgeType == 0)
        bridgeType = MetatileBehavior_GetBridgeType(object->currentMetatileBehavior);
    if (bridgeType > 0 && bridgeType <= ARRAY_COUNT(bridgeOffsets))
        return bridgeOffsets[bridgeType - 1] - 2;
    return -2;
}

static void CopyObjects(struct DioramaSceneSnapshot *snapshot)
{
    int i;

    for (i = 0; i < OBJECT_EVENTS_COUNT; i++)
    {
        const struct ObjectEvent *object = &gObjectEvents[i];
        struct DioramaObjectSnapshot *copy;
        const struct Sprite *sprite;
        const struct ObjectEventGraphicsInfo *graphicsInfo;
        u8 resolvedPriority;
        s16 baseX;
        s16 baseY;

        if (!object->active || object->spriteId >= MAX_SPRITES)
            continue;
        sprite = &gSprites[object->spriteId];
        if (!sprite->inUse || snapshot->objectCount >= DIORAMA_MAX_OBJECTS)
            continue;
        graphicsInfo = GetObjectEventGraphicsInfo(object->graphicsId);
        resolvedPriority = sprite->oam.priority;

        copy = &snapshot->objects[snapshot->objectCount++];
        copy->active = TRUE;
        copy->localId = object->localId;
        copy->graphicsId = object->graphicsId;
        copy->spriteId = object->spriteId;
        copy->elevation = object->currentElevation;
        copy->previousElevation = object->previousElevation;
        copy->facingDirection = object->facingDirection;
        copy->movementDirection = object->movementDirection;
        copy->flags = (object->isPlayer ? DIORAMA_OBJECT_PLAYER : 0)
                    | ((object->invisible || sprite->invisible) ? DIORAMA_OBJECT_INVISIBLE : 0)
                    | (object->offScreen ? DIORAMA_OBJECT_OFFSCREEN : 0)
                    | (object->hasReflection ? DIORAMA_OBJECT_REFLECTION : 0)
                    | (object->hasShadow ? DIORAMA_OBJECT_SHADOW : 0)
                    | (HasUnsupportedSubsprites(sprite, graphicsInfo, &resolvedPriority)
                        ? DIORAMA_OBJECT_SUBSPRITES : 0);
        copy->currentMapX = object->currentCoords.x;
        copy->currentMapY = object->currentCoords.y;
        copy->previousMapX = object->previousCoords.x;
        copy->previousMapY = object->previousCoords.y;
        copy->screenX = sprite->x + sprite->x2;
        copy->screenY = sprite->y + sprite->y2;
        copy->spriteX2 = sprite->x2;
        copy->spriteY2 = sprite->y2;
        copy->width = graphicsInfo != NULL ? graphicsInfo->width : 0;
        copy->height = graphicsInfo != NULL ? graphicsInfo->height : 0;
        SetSpritePosToMapCoords(object->currentCoords.x, object->currentCoords.y, &baseX, &baseY);
        copy->mapPixelOffsetX = sprite->x - (baseX + 8);
        copy->mapPixelOffsetY = sprite->y + copy->height / 2 - (baseY + 16);
        copy->centerToCornerX = sprite->centerToCornerVecX;
        copy->centerToCornerY = sprite->centerToCornerVecY;
        copy->shadowSize = graphicsInfo != NULL ? graphicsInfo->shadowSize : 0;
        copy->reflectionHidden = object->hideReflection;
        copy->reflectionPaletteNum = gReflectionEffectPaletteMap[sprite->oam.paletteNum];
        copy->reflectionOffsetY = GetReflectionOffsetY(object);
        copy->affineMode = sprite->oam.affineMode;
        copy->objMode = sprite->oam.objMode;
        copy->bpp = sprite->oam.bpp;
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
        copy->priority = resolvedPriority;
        copy->subpriority = sprite->subpriority;
        copy->oamOrder = FindOamOrder(sprite, resolvedPriority);
        copy->animNum = sprite->animNum;
        copy->animCmdIndex = sprite->animCmdIndex;
        if (!CopyObjectFrame(copy, sprite))
            copy->flags |= DIORAMA_OBJECT_FRAME_INVALID;
    }
    CopySurfBlob(snapshot);
}

void DioramaScene_Init(void)
{
    memset(&sDraft, 0, sizeof(sDraft));
    memset(sPreviousPalette, 0, sizeof(sPreviousPalette));
    memset(sPreviousTileGraphics, 0, sizeof(sPreviousTileGraphics));
    sMapGeneration = 0;
    sMapEditGeneration = 0;
    sPaletteGeneration = 0;
    sObjPaletteGeneration = 0;
    sTilesetAnimationGeneration = 0;
    sHasPreviousPalette = false;
    sHasPreviousTileGraphics = false;
    sPublishedThisFrame = false;
    sMapChanged = true;
    sPreviousMapGroup = -1;
    sPreviousMapNum = -1;
    sPreviousMapLayoutId = 0;
    sDirtyCellCount = 0;
    sDirtyOverflow = false;
}

void DioramaScene_BeginFrame(void)
{
    sPublishedThisFrame = false;
}

void DioramaScene_MarkMapChanged(void)
{
    sMapChanged = true;
    sDirtyCellCount = 0;
    sDirtyOverflow = false;
}

void DioramaScene_MarkCellDirty(int16_t mapX, int16_t mapY)
{
    u8 i;

    sMapEditGeneration++;
    for (i = 0; i < sDirtyCellCount; i++)
        if (sDirtyCells[i].mapX == mapX && sDirtyCells[i].mapY == mapY)
            return;
    if (sDirtyCellCount >= DIORAMA_MAX_DIRTY_CELLS)
    {
        sDirtyOverflow = true;
        return;
    }
    sDirtyCells[sDirtyCellCount].mapX = mapX;
    sDirtyCells[sDirtyCellCount].mapY = mapY;
    sDirtyCellCount++;
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
    sDraft.weather = GetCurrentWeather();
    sDraft.nextWeather = gWeatherPtr->nextWeather;
    sDraft.weatherTransitionComplete = gWeatherPtr->weatherChangeComplete;
    sDraft.rainVisibleCount = gWeatherPtr->curRainSpriteIndex;
    sDraft.fogScrollOffset = gWeatherPtr->fogHScrollOffset;
    sDraft.weatherBlendEVA = gWeatherPtr->currBlendEVA;
    sDraft.weatherBlendEVB = gWeatherPtr->currBlendEVB;
    if (!DioramaWeather_CanRender(sDraft.weather, sDraft.nextWeather))
        sDraft.fallbackReasons |= DIORAMA_FALLBACK_UNSUPPORTED_WEATHER;
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
    sDraft.mapEditGeneration = sMapEditGeneration;
    sDraft.dirtyCellCount = sDirtyCellCount;
    sDraft.dirtyOverflow = sDirtyOverflow;
    memcpy(sDraft.dirtyCells, sDirtyCells,
           sDirtyCellCount * sizeof(*sDirtyCells));
    if (!sHasPreviousPalette
     || memcmp(sPreviousPalette, gPlttBufferFaded, 256 * sizeof(*sPreviousPalette)) != 0)
        sPaletteGeneration++;
    if (!sHasPreviousPalette
     || memcmp(sPreviousPalette + 256, gPlttBufferFaded + 256,
               256 * sizeof(*sPreviousPalette)) != 0)
        sObjPaletteGeneration++;
    memcpy(sPreviousPalette, gPlttBufferFaded, sizeof(sPreviousPalette));
    sHasPreviousPalette = true;
    memcpy(sDraft.fadedPalette, gPlttBufferFaded, sizeof(sDraft.fadedPalette));
    sDraft.paletteGeneration = sPaletteGeneration;
    sDraft.objPaletteGeneration = sObjPaletteGeneration;

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
        if (gPlayerAvatar.objectEventId < OBJECT_EVENTS_COUNT
         && gObjectEvents[gPlayerAvatar.objectEventId].active)
        {
            const struct ObjectEvent *player = &gObjectEvents[gPlayerAvatar.objectEventId];

            sDraft.playerMapX = player->currentCoords.x - MAP_OFFSET;
            sDraft.playerMapY = player->currentCoords.y - MAP_OFFSET;
            sDraft.playerFacingDirection = player->facingDirection;
        }
        CopyUiProfile(&sDraft);
    }
    DioramaSnapshotExchange_Publish(&sDraft);
    sPublishedThisFrame = true;
    sDirtyCellCount = 0;
    sDirtyOverflow = false;
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
