#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "diorama/metatile_atlas.h"

#define CHECK(condition) Check((condition), #condition, __LINE__)

static void Check(bool condition, const char *expression, int line)
{
    if (!condition)
    {
        fprintf(stderr, "test_metatile_atlas.c:%d: check failed: %s\n", line, expression);
        exit(EXIT_FAILURE);
    }
}

static void SetPixel(uint8_t *graphics, uint16_t tile, uint8_t x, uint8_t y, uint8_t color)
{
    uint8_t *packed = &graphics[tile * DIORAMA_TILE_BYTES + y * 4 + x / 2];

    if (x & 1)
        *packed = (*packed & 0x0F) | (color << 4);
    else
        *packed = (*packed & 0xF0) | color;
}

static void TestDecodeAndFlips(void)
{
    static uint8_t graphics[DIORAMA_TILE_GRAPHICS_SIZE];

    memset(graphics, 0, sizeof(graphics));
    SetPixel(graphics, 7, 1, 2, 5);
    SetPixel(graphics, 7, 6, 5, 9);
    CHECK(DioramaMetatile_DecodePixel(graphics, 7, 1, 2) == 5);
    CHECK(DioramaMetatile_DecodePixel(graphics, 7 | (1 << 10), 6, 2) == 5);
    CHECK(DioramaMetatile_DecodePixel(graphics, 7 | (1 << 11), 1, 5) == 5);
    CHECK(DioramaMetatile_DecodePixel(graphics, 7 | (1 << 10) | (1 << 11), 1, 2) == 9);
}

static void TestColorAndComposition(void)
{
    static uint8_t graphics[DIORAMA_TILE_GRAPHICS_SIZE];
    uint16_t entries[DIORAMA_METATILE_ENTRY_COUNT] = {0};
    uint16_t palette[DIORAMA_FADED_PALETTE_ENTRIES] = {0};
    uint32_t pixels[DIORAMA_METATILE_SIZE * DIORAMA_METATILE_SIZE];
    uint32_t layerPixels[DIORAMA_METATILE_SIZE * DIORAMA_METATILE_SIZE];
    int x;
    int y;

    CHECK(DioramaMetatile_ConvertColor(0x001F, false) == 0xFFFF0000u);
    CHECK(DioramaMetatile_ConvertColor(0x03E0, false) == 0xFF00FF00u);
    CHECK(DioramaMetatile_ConvertColor(0x7C00, false) == 0xFF0000FFu);
    CHECK(DioramaMetatile_ConvertColor(0x7FFF, true) == 0x00FFFFFFu);

    memset(graphics, 0, sizeof(graphics));
    for (y = 0; y < 8; y++)
        for (x = 0; x < 8; x++)
            SetPixel(graphics, 1, x, y, 2);
    SetPixel(graphics, 2, 0, 0, 3);
    entries[0] = 1 | (1 << 12);
    entries[4] = 2 | (1 << 12);
    palette[16 + 2] = 0x001F;
    palette[16 + 3] = 0x03E0;

    DioramaMetatile_Compose(graphics, entries, palette, pixels);
    CHECK(pixels[0] == 0xFF00FF00u);
    CHECK(pixels[1] == 0xFFFF0000u);
    CHECK(pixels[7 * DIORAMA_METATILE_SIZE + 7] == 0xFFFF0000u);
    CHECK(pixels[8] == 0);
    DioramaMetatile_ComposeLayer(graphics, entries, palette, 0, layerPixels);
    CHECK(layerPixels[0] == 0xFFFF0000u);
    CHECK(layerPixels[1] == 0xFFFF0000u);
    DioramaMetatile_ComposeLayer(graphics, entries, palette, 1, layerPixels);
    CHECK(layerPixels[0] == 0xFF00FF00u);
    CHECK(layerPixels[1] == 0);
}

static void TestAtlasGuttersAndUv(void)
{
    static struct DioramaSceneSnapshot snapshot;
    static uint32_t atlas[DIORAMA_ATLAS_PIXEL_COUNT];
    static uint32_t baseAtlas[DIORAMA_ATLAS_PIXEL_COUNT];
    static uint32_t foregroundAtlas[DIORAMA_ATLAS_PIXEL_COUNT];
    uint8_t present[DIORAMA_ATLAS_PRESENT_BYTES];
    struct DioramaAtlasUv uv;
    int originX;
    int originY;

    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.visibleCellCount = 2;
    snapshot.cells[0].metatileId = 33;
    snapshot.cells[1].metatileId = 33;
    snapshot.cells[0].tileEntries[0] = 1;
    snapshot.cells[0].tileEntries[1] = 1;
    snapshot.cells[0].tileEntries[2] = 1;
    snapshot.cells[0].tileEntries[3] = 1;
    memset(snapshot.tileGraphics + DIORAMA_TILE_BYTES, 0x11, DIORAMA_TILE_BYTES);
    snapshot.fadedPalette[1] = 0x7FFF;

    DioramaAtlas_Clear(atlas, present);
    memset(baseAtlas, 0, sizeof(baseAtlas));
    memset(foregroundAtlas, 0, sizeof(foregroundAtlas));
    CHECK(DioramaAtlas_Update(&snapshot, NULL, atlas, baseAtlas, foregroundAtlas, present));
    CHECK(!DioramaAtlas_Update(&snapshot, NULL, atlas, baseAtlas, foregroundAtlas, present));
    CHECK((present[33 / 8] & (1 << (33 % 8))) != 0);

    originX = (33 % DIORAMA_ATLAS_COLUMNS) * DIORAMA_ATLAS_STRIDE + DIORAMA_ATLAS_GUTTER;
    originY = (33 / DIORAMA_ATLAS_COLUMNS) * DIORAMA_ATLAS_STRIDE + DIORAMA_ATLAS_GUTTER;
    CHECK(atlas[originY * DIORAMA_ATLAS_WIDTH + originX] == 0xFFFFFFFFu);
    CHECK(atlas[(originY - 1) * DIORAMA_ATLAS_WIDTH + originX - 1] == 0xFFFFFFFFu);
    CHECK(atlas[(originY + 16) * DIORAMA_ATLAS_WIDTH + originX + 16] == 0xFFFFFFFFu);
    CHECK(baseAtlas[originY * DIORAMA_ATLAS_WIDTH + originX] == 0xFFFFFFFFu);
    CHECK(foregroundAtlas[originY * DIORAMA_ATLAS_WIDTH + originX] == 0);

    uv = DioramaAtlas_GetUv(33);
    CHECK(uv.u0 > (float)(originX - 1) / DIORAMA_ATLAS_WIDTH);
    CHECK(uv.v0 > (float)(originY - 1) / DIORAMA_ATLAS_HEIGHT);
    CHECK(uv.u1 < (float)(originX + 16) / DIORAMA_ATLAS_WIDTH);
    CHECK(uv.v1 < (float)(originY + 16) / DIORAMA_ATLAS_HEIGHT);
}

static void TestCutoutDifference(void)
{
    static struct DioramaSceneSnapshot snapshot;
    static uint32_t atlas[DIORAMA_ATLAS_PIXEL_COUNT];
    static uint32_t baseAtlas[DIORAMA_ATLAS_PIXEL_COUNT];
    static uint32_t foregroundAtlas[DIORAMA_ATLAS_PIXEL_COUNT];
    uint16_t cutoutBases[DIORAMA_TILE_COUNT];
    uint8_t present[DIORAMA_ATLAS_PRESENT_BYTES];
    int originX;
    int originY;
    int tile;

    memset(&snapshot, 0, sizeof(snapshot));
    memset(cutoutBases, 0xFF, sizeof(cutoutBases));
    cutoutBases[33] = 1;
    snapshot.visibleCellCount = 2;
    snapshot.cells[0].metatileId = 33;
    snapshot.cells[1].metatileId = 1;
    for (tile = 0; tile < 4; tile++)
    {
        snapshot.cells[0].tileEntries[tile] = 1;
        snapshot.cells[1].tileEntries[tile] = 2;
    }
    memset(snapshot.tileGraphics + DIORAMA_TILE_BYTES, 0x11, DIORAMA_TILE_BYTES);
    memset(snapshot.tileGraphics + 2 * DIORAMA_TILE_BYTES, 0x11, DIORAMA_TILE_BYTES);
    SetPixel(snapshot.tileGraphics, 1, 0, 0, 2);
    snapshot.fadedPalette[1] = 0x7FFF;
    snapshot.fadedPalette[2] = 0x001F;

    DioramaAtlas_Clear(atlas, present);
    memset(baseAtlas, 0, sizeof(baseAtlas));
    memset(foregroundAtlas, 0, sizeof(foregroundAtlas));
    CHECK(DioramaAtlas_Update(&snapshot, cutoutBases, atlas, baseAtlas,
                              foregroundAtlas, present));
    originX = (33 % DIORAMA_ATLAS_COLUMNS) * DIORAMA_ATLAS_STRIDE + DIORAMA_ATLAS_GUTTER;
    originY = (33 / DIORAMA_ATLAS_COLUMNS) * DIORAMA_ATLAS_STRIDE + DIORAMA_ATLAS_GUTTER;
    CHECK(baseAtlas[originY * DIORAMA_ATLAS_WIDTH + originX] == 0xFFFFFFFFu);
    CHECK(foregroundAtlas[originY * DIORAMA_ATLAS_WIDTH + originX] == 0xFFFF0000u);
    CHECK(foregroundAtlas[originY * DIORAMA_ATLAS_WIDTH + originX + 1] == 0);
}

static void TestDirtyTileUpdates(void)
{
    static struct DioramaSceneSnapshot snapshot;
    static uint32_t atlas[DIORAMA_ATLAS_PIXEL_COUNT];
    static uint32_t baseAtlas[DIORAMA_ATLAS_PIXEL_COUNT];
    static uint32_t foregroundAtlas[DIORAMA_ATLAS_PIXEL_COUNT];
    uint16_t cutoutBases[DIORAMA_TILE_COUNT];
    uint8_t present[DIORAMA_ATLAS_PRESENT_BYTES];
    uint8_t dirty[DIORAMA_ATLAS_DIRTY_TILE_BYTES];
    uint8_t forced[DIORAMA_ATLAS_PRESENT_BYTES];
    uint8_t updated[DIORAMA_ATLAS_PRESENT_BYTES];

    memset(&snapshot, 0, sizeof(snapshot));
    memset(cutoutBases, 0xFF, sizeof(cutoutBases));
    memset(dirty, 0, sizeof(dirty));
    memset(forced, 0, sizeof(forced));
    memset(updated, 0, sizeof(updated));
    snapshot.visibleCellCount = 3;
    snapshot.cells[0].metatileId = 33;
    snapshot.cells[0].tileEntries[0] = 1 | (1 << 10) | (2 << 12);
    snapshot.cells[1].metatileId = 34;
    snapshot.cells[1].tileEntries[0] = 2;
    snapshot.cells[2].metatileId = 35;
    snapshot.cells[2].tileEntries[0] = 3;
    cutoutBases[35] = 34;
    snapshot.fadedPalette[1] = 0x001F;
    snapshot.fadedPalette[2 * 16 + 1] = 0x03E0;
    memset(snapshot.tileGraphics + DIORAMA_TILE_BYTES, 0x11, DIORAMA_TILE_BYTES);
    memset(snapshot.tileGraphics + 2 * DIORAMA_TILE_BYTES, 0x11, DIORAMA_TILE_BYTES);
    memset(snapshot.tileGraphics + 3 * DIORAMA_TILE_BYTES, 0x11, DIORAMA_TILE_BYTES);
    DioramaAtlas_Clear(atlas, present);
    memset(baseAtlas, 0, sizeof(baseAtlas));
    memset(foregroundAtlas, 0, sizeof(foregroundAtlas));
    CHECK(DioramaAtlas_Update(&snapshot, cutoutBases, atlas, baseAtlas,
                               foregroundAtlas, present));

    dirty[1 / 8] |= 1 << (1 % 8);
    CHECK(DioramaAtlas_UpdateDirty(&snapshot, cutoutBases, dirty, NULL,
                                   atlas, baseAtlas, foregroundAtlas, present, updated));
    CHECK((updated[33 / 8] & (1 << (33 % 8))) != 0);
    CHECK((updated[34 / 8] & (1 << (34 % 8))) == 0);
    CHECK((updated[35 / 8] & (1 << (35 % 8))) == 0);

    memset(dirty, 0, sizeof(dirty));
    memset(updated, 0, sizeof(updated));
    dirty[2 / 8] |= 1 << (2 % 8);
    CHECK(DioramaAtlas_UpdateDirty(&snapshot, cutoutBases, dirty, NULL,
                                   atlas, baseAtlas, foregroundAtlas, present, updated));
    CHECK((updated[34 / 8] & (1 << (34 % 8))) != 0);
    CHECK((updated[35 / 8] & (1 << (35 % 8))) != 0);

    memset(dirty, 0, sizeof(dirty));
    memset(updated, 0, sizeof(updated));
    forced[33 / 8] |= 1 << (33 % 8);
    CHECK(DioramaAtlas_UpdateDirty(&snapshot, cutoutBases, dirty, forced,
                                   atlas, baseAtlas, foregroundAtlas, present, updated));
    CHECK((updated[33 / 8] & (1 << (33 % 8))) != 0);
}

int main(void)
{
    TestDecodeAndFlips();
    TestColorAndComposition();
    TestAtlasGuttersAndUv();
    TestCutoutDifference();
    TestDirtyTileUpdates();
    puts("metatile atlas tests passed");
    return EXIT_SUCCESS;
}
