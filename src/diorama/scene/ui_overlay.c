#ifdef ENABLE_DIORAMA

#include <string.h>

#include "diorama/ui_overlay.h"

bool DioramaUI_RectIntersects(const struct DioramaUiRect *rect,
                              int x, int y, int width, int height)
{
    return rect != NULL && width > 0 && height > 0
        && x < rect->x + rect->width && x + width > rect->x
        && y < rect->y + rect->height && y + height > rect->y;
}

static bool IsInsideUi(const struct DioramaUiRect *rects, unsigned rectCount,
                       int x, int y)
{
    unsigned i;

    for (i = 0; i < rectCount; i++)
        if (x >= rects[i].x && x < rects[i].x + rects[i].width
         && y >= rects[i].y && y < rects[i].y + rects[i].height)
            return true;
    return false;
}

static uint32_t ConvertColor(uint16_t color)
{
    uint32_t r = (color & 0x1F) * 255 / 31;
    uint32_t g = ((color >> 5) & 0x1F) * 255 / 31;
    uint32_t b = ((color >> 10) & 0x1F) * 255 / 31;

    return 0xFF000000u | (r << 16) | (g << 8) | b;
}

void DioramaUI_RenderBgOverlay(int frameWidth, int frameHeight,
                               const struct DioramaUiRect *rects, unsigned rectCount,
                               uint16_t control, uint16_t hOffset, uint16_t vOffset,
                               const uint8_t *tileGraphics, const uint16_t *tilemap,
                               const uint16_t *palette, uint32_t *overlay)
{
    int y;

    if (overlay == NULL || frameWidth <= 0 || frameHeight <= 0)
        return;
    memset(overlay, 0, frameWidth * frameHeight * sizeof(*overlay));
    if (rects == NULL || tileGraphics == NULL || tilemap == NULL || palette == NULL)
        return;
    if (control & (1 << 7))
        return;
    for (y = 0; y < frameHeight; y++)
    {
        int x;
        unsigned sourceY = (y + vOffset) & 0xFF;

        for (x = 0; x < frameWidth; x++)
        {
            unsigned sourceX;
            uint16_t entry;
            unsigned tileX;
            unsigned tileY;
            unsigned tileNum;
            unsigned paletteNum;
            unsigned pixel;

            if (!IsInsideUi(rects, rectCount, x, y))
                continue;
            sourceX = (x + hOffset) & 0xFF;
            entry = tilemap[(sourceY / 8) * 32 + sourceX / 8];
            tileNum = entry & 0x3FF;
            paletteNum = (entry >> 12) & 0xF;
            tileX = sourceX & 7;
            tileY = sourceY & 7;
            if (entry & (1 << 10))
                tileX = 7 - tileX;
            if (entry & (1 << 11))
                tileY = 7 - tileY;
            pixel = tileGraphics[tileNum * 32 + tileY * 4 + tileX / 2];
            pixel = (tileX & 1) ? pixel >> 4 : pixel & 0xF;
            if (pixel != 0)
                overlay[y * frameWidth + x]
                    = ConvertColor(palette[paletteNum * 16 + pixel]);
        }
    }
}

#endif
