#ifndef GUARD_DIORAMA_UI_OVERLAY_H
#define GUARD_DIORAMA_UI_OVERLAY_H

#include <stdbool.h>
#include <stdint.h>

#define DIORAMA_MAX_UI_RECTS 8
#define DIORAMA_OAM_MASK_WORDS 4
#define DIORAMA_UI_BG_TILE_BYTES 0x8000
#define DIORAMA_UI_BG_MAP_BYTES 0x800

enum DioramaUiFlags
{
    DIORAMA_UI_DIALOGUE = 1 << 0,
    DIORAMA_UI_START_MENU = 1 << 1,
    DIORAMA_UI_MAP_POPUP = 1 << 2,
    DIORAMA_UI_FIELD_WINDOW = 1 << 3,
};

struct DioramaUiRect
{
    int16_t x;
    int16_t y;
    uint16_t width;
    uint16_t height;
};

bool DioramaUI_RectIntersects(const struct DioramaUiRect *rect,
                              int x, int y, int width, int height);
void DioramaUI_RenderBgOverlay(int frameWidth, int frameHeight,
                               const struct DioramaUiRect *rects, unsigned rectCount,
                               uint16_t control, uint16_t hOffset, uint16_t vOffset,
                               const uint8_t *tileGraphics, const uint16_t *tilemap,
                               const uint16_t *palette, uint32_t *overlay);

#endif
