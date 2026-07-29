#ifdef ENABLE_DIORAMA

#include <string.h>

#include "diorama/metatile_atlas.h"

#define TILE_ID_MASK 0x03FF
#define TILE_HFLIP (1 << 10)
#define TILE_VFLIP (1 << 11)
#define TILE_PALETTE_SHIFT 12

uint8_t DioramaMetatile_DecodePixel(const uint8_t *tileGraphics, uint16_t tileEntry,
                                    uint8_t x, uint8_t y)
{
    uint16_t tileId = tileEntry & TILE_ID_MASK;
    uint8_t sourceX = tileEntry & TILE_HFLIP ? 7 - x : x;
    uint8_t sourceY = tileEntry & TILE_VFLIP ? 7 - y : y;
    uint8_t packed = tileGraphics[tileId * DIORAMA_TILE_BYTES + sourceY * 4 + sourceX / 2];

    return sourceX & 1 ? packed >> 4 : packed & 0x0F;
}

uint32_t DioramaMetatile_ConvertColor(uint16_t bgr555, bool transparent)
{
    uint8_t red = bgr555 & 0x1F;
    uint8_t green = (bgr555 >> 5) & 0x1F;
    uint8_t blue = (bgr555 >> 10) & 0x1F;

    red = (red << 3) | (red >> 2);
    green = (green << 3) | (green >> 2);
    blue = (blue << 3) | (blue >> 2);
    return (transparent ? 0u : 0xFF000000u)
         | ((uint32_t)red << 16) | ((uint32_t)green << 8) | blue;
}

void DioramaMetatile_Compose(const uint8_t *tileGraphics, const uint16_t *tileEntries,
                             const uint16_t *palette, uint32_t *pixels)
{
    int layer;
    int tile;

    memset(pixels, 0, DIORAMA_METATILE_SIZE * DIORAMA_METATILE_SIZE * sizeof(*pixels));
    for (layer = 0; layer < 2; layer++)
    {
        for (tile = 0; tile < 4; tile++)
        {
            uint16_t entry = tileEntries[layer * 4 + tile];
            uint8_t paletteId = entry >> TILE_PALETTE_SHIFT;
            int tileX = (tile & 1) * 8;
            int tileY = (tile >> 1) * 8;
            int y;
            int x;

            for (y = 0; y < 8; y++)
            {
                for (x = 0; x < 8; x++)
                {
                    uint8_t colorId = DioramaMetatile_DecodePixel(tileGraphics, entry, x, y);

                    if (colorId != 0)
                    {
                        pixels[(tileY + y) * DIORAMA_METATILE_SIZE + tileX + x]
                            = DioramaMetatile_ConvertColor(palette[paletteId * 16 + colorId], false);
                    }
                }
            }
        }
    }
}

void DioramaMetatile_ComposeLayer(const uint8_t *tileGraphics, const uint16_t *tileEntries,
                                  const uint16_t *palette, uint8_t layer,
                                  uint32_t *pixels)
{
    int tile;

    memset(pixels, 0, DIORAMA_METATILE_SIZE * DIORAMA_METATILE_SIZE * sizeof(*pixels));
    if (layer > 1)
        return;
    for (tile = 0; tile < 4; tile++)
    {
        uint16_t entry = tileEntries[layer * 4 + tile];
        uint8_t paletteId = entry >> TILE_PALETTE_SHIFT;
        int tileX = (tile & 1) * 8;
        int tileY = (tile >> 1) * 8;
        int y;
        int x;

        for (y = 0; y < 8; y++)
        {
            for (x = 0; x < 8; x++)
            {
                uint8_t colorId = DioramaMetatile_DecodePixel(tileGraphics, entry, x, y);

                if (colorId != 0)
                    pixels[(tileY + y) * DIORAMA_METATILE_SIZE + tileX + x]
                        = DioramaMetatile_ConvertColor(palette[paletteId * 16 + colorId], false);
            }
        }
    }
}

void DioramaAtlas_Clear(uint32_t *atlasPixels, uint8_t *presentMetatiles)
{
    memset(atlasPixels, 0, DIORAMA_ATLAS_PIXEL_COUNT * sizeof(*atlasPixels));
    memset(presentMetatiles, 0, DIORAMA_ATLAS_PRESENT_BYTES);
}

static bool IsPresent(const uint8_t *presentMetatiles, uint16_t metatileId)
{
    return (presentMetatiles[metatileId / 8] & (1 << (metatileId % 8))) != 0;
}

static void BlitWithGutter(uint32_t *atlasPixels, uint16_t metatileId, const uint32_t *pixels)
{
    int atlasX = (metatileId % DIORAMA_ATLAS_COLUMNS) * DIORAMA_ATLAS_STRIDE + DIORAMA_ATLAS_GUTTER;
    int atlasY = (metatileId / DIORAMA_ATLAS_COLUMNS) * DIORAMA_ATLAS_STRIDE + DIORAMA_ATLAS_GUTTER;
    int y;
    int x;

    for (y = -DIORAMA_ATLAS_GUTTER; y < DIORAMA_METATILE_SIZE + DIORAMA_ATLAS_GUTTER; y++)
    {
        int sourceY = y < 0 ? 0 : (y >= DIORAMA_METATILE_SIZE ? DIORAMA_METATILE_SIZE - 1 : y);

        for (x = -DIORAMA_ATLAS_GUTTER; x < DIORAMA_METATILE_SIZE + DIORAMA_ATLAS_GUTTER; x++)
        {
            int sourceX = x < 0 ? 0 : (x >= DIORAMA_METATILE_SIZE ? DIORAMA_METATILE_SIZE - 1 : x);

            atlasPixels[(atlasY + y) * DIORAMA_ATLAS_WIDTH + atlasX + x]
                = pixels[sourceY * DIORAMA_METATILE_SIZE + sourceX];
        }
    }
}

bool DioramaAtlas_Update(const struct DioramaSceneSnapshot *snapshot,
                         const uint16_t *cutoutBaseMetatileIds,
                         uint32_t *atlasPixels, uint32_t *baseAtlasPixels,
                         uint32_t *foregroundAtlasPixels, uint8_t *presentMetatiles)
{
    uint32_t metatilePixels[DIORAMA_METATILE_SIZE * DIORAMA_METATILE_SIZE];
    uint32_t layerPixels[DIORAMA_METATILE_SIZE * DIORAMA_METATILE_SIZE];
    bool changed = false;
    unsigned i;

    unsigned cellCount = snapshot->visibleCellCount;

    if (cellCount > DIORAMA_MAX_VISIBLE_CELLS)
        cellCount = DIORAMA_MAX_VISIBLE_CELLS;
    for (i = 0; i < cellCount; i++)
    {
        const struct DioramaCellSnapshot *cell = &snapshot->cells[i];

        if (cell->metatileId >= DIORAMA_TILE_COUNT || IsPresent(presentMetatiles, cell->metatileId))
            continue;
        DioramaMetatile_Compose(snapshot->tileGraphics, cell->tileEntries,
                                snapshot->fadedPalette, metatilePixels);
        BlitWithGutter(atlasPixels, cell->metatileId, metatilePixels);
        if (cutoutBaseMetatileIds != NULL
         && cutoutBaseMetatileIds[cell->metatileId] < DIORAMA_TILE_COUNT)
        {
            const struct DioramaCellSnapshot *baseCell = NULL;
            unsigned baseIndex;

            for (baseIndex = 0; baseIndex < cellCount; baseIndex++)
            {
                if (snapshot->cells[baseIndex].metatileId == cutoutBaseMetatileIds[cell->metatileId])
                {
                    baseCell = &snapshot->cells[baseIndex];
                    break;
                }
            }
            if (baseCell != NULL)
            {
                unsigned pixel;

                DioramaMetatile_Compose(snapshot->tileGraphics, baseCell->tileEntries,
                                        snapshot->fadedPalette, layerPixels);
                BlitWithGutter(baseAtlasPixels, cell->metatileId, layerPixels);
                for (pixel = 0; pixel < DIORAMA_METATILE_SIZE * DIORAMA_METATILE_SIZE; pixel++)
                    if (metatilePixels[pixel] == layerPixels[pixel])
                        metatilePixels[pixel] = 0;
                BlitWithGutter(foregroundAtlasPixels, cell->metatileId, metatilePixels);
            }
            else
            {
                DioramaMetatile_ComposeLayer(snapshot->tileGraphics, cell->tileEntries,
                                             snapshot->fadedPalette, 0, layerPixels);
                BlitWithGutter(baseAtlasPixels, cell->metatileId, layerPixels);
                DioramaMetatile_ComposeLayer(snapshot->tileGraphics, cell->tileEntries,
                                             snapshot->fadedPalette, 1, layerPixels);
                BlitWithGutter(foregroundAtlasPixels, cell->metatileId, layerPixels);
            }
        }
        else
        {
            DioramaMetatile_ComposeLayer(snapshot->tileGraphics, cell->tileEntries,
                                         snapshot->fadedPalette, 0, layerPixels);
            BlitWithGutter(baseAtlasPixels, cell->metatileId, layerPixels);
            DioramaMetatile_ComposeLayer(snapshot->tileGraphics, cell->tileEntries,
                                         snapshot->fadedPalette, 1, layerPixels);
            BlitWithGutter(foregroundAtlasPixels, cell->metatileId, layerPixels);
        }
        presentMetatiles[cell->metatileId / 8] |= 1 << (cell->metatileId % 8);
        changed = true;
    }
    return changed;
}

struct DioramaAtlasUv DioramaAtlas_GetUv(uint16_t metatileId)
{
    float x = (metatileId % DIORAMA_ATLAS_COLUMNS) * DIORAMA_ATLAS_STRIDE
            + DIORAMA_ATLAS_GUTTER + 0.5f;
    float y = (metatileId / DIORAMA_ATLAS_COLUMNS) * DIORAMA_ATLAS_STRIDE
            + DIORAMA_ATLAS_GUTTER + 0.5f;
    struct DioramaAtlasUv uv = {
        x / DIORAMA_ATLAS_WIDTH,
        y / DIORAMA_ATLAS_HEIGHT,
        (x + DIORAMA_METATILE_SIZE - 1) / DIORAMA_ATLAS_WIDTH,
        (y + DIORAMA_METATILE_SIZE - 1) / DIORAMA_ATLAS_HEIGHT,
    };

    return uv;
}

#endif
