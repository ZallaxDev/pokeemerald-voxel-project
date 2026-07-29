#ifndef GUARD_DIORAMA_METATILE_ATLAS_H
#define GUARD_DIORAMA_METATILE_ATLAS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "diorama/scene_snapshot.h"

#define DIORAMA_METATILE_SIZE 16
#define DIORAMA_ATLAS_COLUMNS 32
#define DIORAMA_ATLAS_ROWS 32
#define DIORAMA_ATLAS_GUTTER 1
#define DIORAMA_ATLAS_STRIDE (DIORAMA_METATILE_SIZE + DIORAMA_ATLAS_GUTTER * 2)
#define DIORAMA_ATLAS_WIDTH (DIORAMA_ATLAS_COLUMNS * DIORAMA_ATLAS_STRIDE)
#define DIORAMA_ATLAS_HEIGHT (DIORAMA_ATLAS_ROWS * DIORAMA_ATLAS_STRIDE)
#define DIORAMA_ATLAS_PIXEL_COUNT (DIORAMA_ATLAS_WIDTH * DIORAMA_ATLAS_HEIGHT)
#define DIORAMA_ATLAS_PRESENT_BYTES (DIORAMA_TILE_COUNT / 8)

struct DioramaAtlasUv
{
    float u0;
    float v0;
    float u1;
    float v1;
};

uint8_t DioramaMetatile_DecodePixel(const uint8_t *tileGraphics, uint16_t tileEntry,
                                    uint8_t x, uint8_t y);
uint32_t DioramaMetatile_ConvertColor(uint16_t bgr555, bool transparent);
void DioramaMetatile_Compose(const uint8_t *tileGraphics, const uint16_t *tileEntries,
                             const uint16_t *palette, uint32_t *pixels);
void DioramaMetatile_ComposeLayer(const uint8_t *tileGraphics, const uint16_t *tileEntries,
                                  const uint16_t *palette, uint8_t layer,
                                  uint32_t *pixels);
void DioramaAtlas_Clear(uint32_t *atlasPixels, uint8_t *presentMetatiles);
bool DioramaAtlas_Update(const struct DioramaSceneSnapshot *snapshot,
                         const uint16_t *cutoutBaseMetatileIds,
                         uint32_t *atlasPixels, uint32_t *baseAtlasPixels,
                         uint32_t *foregroundAtlasPixels, uint8_t *presentMetatiles);
struct DioramaAtlasUv DioramaAtlas_GetUv(uint16_t metatileId);

#endif
