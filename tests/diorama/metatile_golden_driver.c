#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "diorama/metatile_atlas.h"

#define GRAPHICS_SIZE (DIORAMA_TILE_COUNT * DIORAMA_TILE_BYTES)
#define PALETTE_ENTRIES 256
#define LAYER_PIXELS (DIORAMA_METATILE_SIZE * DIORAMA_METATILE_SIZE)

static void Fail(const char *message)
{
    fprintf(stderr, "metatile golden driver: %s\n", message);
    exit(EXIT_FAILURE);
}

static void ReadExact(FILE *file, void *data, size_t size)
{
    if (fread(data, 1, size, file) != size)
        Fail("truncated input");
}

static void WriteExact(FILE *file, const void *data, size_t size)
{
    if (fwrite(data, 1, size, file) != size)
        Fail("could not write output");
}

static void WriteProvenance(FILE *output, const uint8_t *graphics,
                            const uint16_t *entries, uint8_t layer)
{
    int y;
    int x;

    for (y = 0; y < DIORAMA_METATILE_SIZE; y++)
    {
        for (x = 0; x < DIORAMA_METATILE_SIZE; x++)
        {
            uint8_t subtile = (y / 8) * 2 + x / 8;
            uint16_t entry = entries[layer * 4 + subtile];
            uint16_t tile = entry & 0x03FF;
            uint8_t localX = x & 7;
            uint8_t localY = y & 7;
            uint8_t sourceX = entry & (1 << 10) ? 7 - localX : localX;
            uint8_t sourceY = entry & (1 << 11) ? 7 - localY : localY;
            uint8_t values[] = {
                subtile,
                (uint8_t)(tile & 0xFF), (uint8_t)(tile >> 8),
                (uint8_t)(entry >> 12),
                DioramaMetatile_DecodePixel(graphics, entry, localX, localY),
                sourceX, sourceY,
                (uint8_t)((entry >> 10) & 1), (uint8_t)((entry >> 11) & 1),
            };

            WriteExact(output, values, sizeof(values));
        }
    }
}

int main(int argc, char **argv)
{
    uint8_t *graphics;
    uint16_t palette[PALETTE_ENTRIES];
    uint16_t entries[DIORAMA_METATILE_ENTRY_COUNT];
    uint32_t pixels[LAYER_PIXELS];
    uint32_t groupCount;
    FILE *input;
    FILE *output;
    uint32_t group;

    if (argc != 3)
        Fail("expected input and output paths");
    input = fopen(argv[1], "rb");
    output = fopen(argv[2], "wb");
    if (input == NULL || output == NULL)
        Fail("could not open input or output");
    graphics = malloc(GRAPHICS_SIZE);
    if (graphics == NULL)
        Fail("out of memory");

    ReadExact(input, &groupCount, sizeof(groupCount));
    for (group = 0; group < groupCount; group++)
    {
        uint32_t metatileCount;
        uint32_t metatile;

        ReadExact(input, &metatileCount, sizeof(metatileCount));
        ReadExact(input, graphics, GRAPHICS_SIZE);
        ReadExact(input, palette, sizeof(palette));
        for (metatile = 0; metatile < metatileCount; metatile++)
        {
            uint16_t metatileId;
            uint8_t layer;

            ReadExact(input, &metatileId, sizeof(metatileId));
            ReadExact(input, entries, sizeof(entries));
            WriteExact(output, &metatileId, sizeof(metatileId));
            for (layer = 0; layer < 2; layer++)
            {
                DioramaMetatile_ComposeLayer(graphics, entries, palette, layer, pixels);
                WriteExact(output, pixels, sizeof(pixels));
                WriteProvenance(output, graphics, entries, layer);
            }
            DioramaMetatile_Compose(graphics, entries, palette, pixels);
            WriteExact(output, pixels, sizeof(pixels));
        }
    }
    if (fgetc(input) != EOF)
        Fail("trailing input");
    free(graphics);
    if (fclose(input) || fclose(output))
        Fail("could not close input or output");
    return EXIT_SUCCESS;
}
