#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "diorama/ui_overlay.h"

int main(void)
{
    static uint8_t tileGraphics[DIORAMA_UI_BG_TILE_BYTES];
    static uint16_t tilemap[DIORAMA_UI_BG_MAP_BYTES / sizeof(uint16_t)];
    static uint16_t palette[256];
    uint32_t overlay[16 * 16];
    const struct DioramaUiRect rects[] = {
        {4, 4, 8, 8},
    };
    const struct DioramaUiRect fullRect = {0, 0, 8, 8};

    assert(DioramaUI_RectIntersects(&rects[0], 5, 5, 1, 1));
    assert(!DioramaUI_RectIntersects(&rects[0], 12, 4, 1, 1));
    tilemap[0] = 0x21D;
    tileGraphics[0x21D * 32 + 4 * 4 + 4 / 2] = 0x01;
    palette[1] = 0x001F;
    DioramaUI_RenderBgOverlay(16, 16, rects, 1, 0, 0, 0,
                              tileGraphics, tilemap, palette, overlay);
    assert(overlay[0] == 0);
    assert(overlay[4 * 16 + 4] == 0xFFFF0000);
    assert(overlay[4 * 16 + 5] == 0);
    assert(overlay[12 * 16 + 4] == 0);

    tilemap[33] = 0x220 | (1 << 10) | (1 << 11) | (3 << 12);
    tileGraphics[0x220 * 32 + 7 * 4 + 7 / 2] = 0x20;
    palette[3 * 16 + 2] = 0x03E0;
    DioramaUI_RenderBgOverlay(16, 16, &fullRect, 1, 0, 8, 8,
                              tileGraphics, tilemap, palette, overlay);
    assert(overlay[0] == 0xFF00FF00);
    assert(overlay[1] == 0);
    puts("diorama UI overlay tests passed");
    return 0;
}
