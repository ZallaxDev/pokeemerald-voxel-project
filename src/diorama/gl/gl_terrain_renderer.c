#ifdef ENABLE_DIORAMA

#include <stddef.h>
#include <string.h>
#include <SDL2/SDL.h>

#include "global.h"
#include "diorama/gl_loader.h"
#include "diorama/gl_terrain_renderer.h"
#include "diorama/metatile_atlas.h"
#include "diorama/rules.generated.h"
#include "diorama/rules.h"
#include "diorama/terrain_mesh.h"
#include "metatile_behavior.h"

#define TERRAIN_CHUNK_CACHE_SIZE 36
#define TERRAIN_DEBUG_MAX_VERTICES ((TERRAIN_CHUNK_CACHE_SIZE + DIORAMA_MAX_VISIBLE_CELLS) * 8)
#define TERRAIN_MAX_VISIBLE_TREE_INSTANCES \
    (TERRAIN_CHUNK_CACHE_SIZE * DIORAMA_TERRAIN_MAX_TREE_INSTANCES)

struct GLTerrainChunk
{
    bool occupied;
    bool active;
    int16_t chunkX;
    int16_t chunkY;
    uint32_t mapGeneration;
    uint32_t rulesGeneration;
    uint64_t signature;
    uint64_t lastSeenSequence;
    GLuint vertexArray;
    GLuint vertexBuffer;
    struct DioramaTerrainMesh mesh;
};

static struct GLTerrainChunk sChunks[TERRAIN_CHUNK_CACHE_SIZE];
static struct DioramaTerrainVertex sScratchVertices[DIORAMA_TERRAIN_MAX_VERTICES];
static struct DioramaTerrainVertex sDebugVertices[TERRAIN_DEBUG_MAX_VERTICES];
static struct DioramaTerrainTreeInstance sVisibleTreeInstances[TERRAIN_MAX_VISIBLE_TREE_INSTANCES];
static struct DioramaTerrainMetrics sMetrics;
static GLuint sProgram;
static GLuint sDebugVertexArray;
static GLuint sDebugVertexBuffer;
static GLuint sTreeVertexArray;
static GLuint sTreeVertexBuffer;
static GLuint sTreeInstanceBuffer;
static uint32_t sTreeModelVertexCount;
static GLint sCameraLocation;
static GLint sCameraPitchLocation;
static GLint sFocalLengthLocation;
static GLint sAspectCorrectionLocation;
static GLint sImageLocation;
static GLint sBaseImageLocation;
static GLint sForegroundImageLocation;
static GLint sDebugColorLocation;
static GLint sRenderPassLocation;
static uint32_t sMapGeneration;
static uint32_t sMapEditGeneration;
static uint32_t sRulesGeneration;
static uint64_t sLastSyncSequence;
static uint32_t sClaimDebugVertexCount;

static const char sTerrainVertexShader[] =
    "#version 330 core\n"
    "layout(location = 0) in vec3 position;\n"
    "layout(location = 1) in vec2 texCoord;\n"
    "layout(location = 2) in float vertexShade;\n"
    "layout(location = 3) in float vertexTextureLayer;\n"
    "layout(location = 4) in float vertexReflectionMask;\n"
    "layout(location = 5) in vec4 vertexColor;\n"
    "layout(location = 6) in vec3 instanceOffset;\n"
    "uniform vec2 cameraPosition;\n"
    "uniform float cameraPitch;\n"
    "uniform float focalLength;\n"
    "uniform float aspectCorrection;\n"
    "out vec2 uv;\n"
    "out float shade;\n"
    "flat out int textureLayer;\n"
    "out float reflectionMask;\n"
    "out vec4 pixelColor;\n"
    "void main() {\n"
    "  vec3 worldPosition = position + instanceOffset;\n"
    "  const float cameraHeight = 16.0;\n"
    "  const float cameraTargetVertical = -0.458944;\n"
    "  const float nearDepth = 0.1;\n"
    "  const float farDepth = 80.0;\n"
    "  float pitchSin = sin(cameraPitch);\n"
    "  float pitchCos = cos(cameraPitch);\n"
    "  float cameraDistance = (cameraHeight * pitchCos + cameraTargetVertical) / pitchSin;\n"
    "  float relativeX = worldPosition.x - cameraPosition.x;\n"
    "  float relativeZ = worldPosition.z - cameraPosition.y;\n"
    "  float depth = (cameraHeight - worldPosition.y) * pitchSin + (relativeZ + cameraDistance) * pitchCos;\n"
    "  float vertical = -(cameraHeight - worldPosition.y) * pitchCos + (relativeZ + cameraDistance) * pitchSin;\n"
    "  float clipX = relativeX * focalLength * 2.0 / 240.0 * aspectCorrection;\n"
    "  float clipY = -0.04 * depth + vertical * focalLength * 2.0 / 160.0;\n"
    "  float clipZ = ((farDepth + nearDepth) / (farDepth - nearDepth)) * depth"
    "              - (2.0 * farDepth * nearDepth) / (farDepth - nearDepth);\n"
    "  gl_Position = vec4(clipX, clipY, clipZ, depth);\n"
    "  uv = texCoord;\n"
    "  shade = vertexShade;\n"
    "  textureLayer = int(vertexTextureLayer);\n"
    "  reflectionMask = vertexReflectionMask;\n"
    "  pixelColor = vertexColor;\n"
    "}\n";

static const char sTerrainFragmentShader[] =
    "#version 330 core\n"
    "in vec2 uv;\n"
    "in float shade;\n"
    "flat in int textureLayer;\n"
    "in float reflectionMask;\n"
    "in vec4 pixelColor;\n"
    "out vec4 color;\n"
    "uniform sampler2D image;\n"
    "uniform sampler2D baseImage;\n"
    "uniform sampler2D foregroundImage;\n"
    "uniform vec4 debugColor;\n"
    "uniform int renderPass;\n"
    "void main() {\n"
    "  if (renderPass == 1) {\n"
    "    if (reflectionMask < 0.5 || texture(foregroundImage, uv).a >= 0.5) discard;\n"
    "    color = vec4(1.0); return;\n"
    "  }\n"
    "  if (renderPass == 2) {\n"
    "    if (textureLayer != 3) discard;\n"
    "    color = vec4(1.0); return;\n"
    "  }\n"
    "  if (debugColor.a >= 0.0) { color = debugColor; return; }\n"
    "  vec4 texel = textureLayer == 1 ? texture(baseImage, uv)\n"
    "             : (textureLayer == 2 ? texture(foregroundImage, uv)\n"
    "             : (textureLayer == 3 ? pixelColor : texture(image, uv)));\n"
    "  if (texel.a < 0.5) discard;\n"
    "  color = vec4(texel.rgb * shade, texel.a);\n"
    "}\n";

static GLuint CompileShader(GLenum type, const char *source)
{
    GLuint shader = dglCreateShader(type);
    GLint compiled;

    dglShaderSource(shader, 1, &source, NULL);
    dglCompileShader(shader);
    dglGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled)
    {
        char log[1024];
        dglGetShaderInfoLog(shader, sizeof(log), NULL, log);
        SDL_Log("Diorama terrain shader compile failed: %s", log);
        dglDeleteShader(shader);
        return 0;
    }
    return shader;
}

static bool CreateProgram(void)
{
    GLuint vertexShader = CompileShader(GL_VERTEX_SHADER, sTerrainVertexShader);
    GLuint fragmentShader = CompileShader(GL_FRAGMENT_SHADER, sTerrainFragmentShader);
    GLint linked;

    if (vertexShader == 0 || fragmentShader == 0)
    {
        if (vertexShader != 0) dglDeleteShader(vertexShader);
        if (fragmentShader != 0) dglDeleteShader(fragmentShader);
        return false;
    }
    sProgram = dglCreateProgram();
    dglAttachShader(sProgram, vertexShader);
    dglAttachShader(sProgram, fragmentShader);
    dglLinkProgram(sProgram);
    dglDeleteShader(vertexShader);
    dglDeleteShader(fragmentShader);
    dglGetProgramiv(sProgram, GL_LINK_STATUS, &linked);
    if (!linked)
    {
        char log[1024];
        dglGetProgramInfoLog(sProgram, sizeof(log), NULL, log);
        SDL_Log("Diorama terrain shader link failed: %s", log);
        return false;
    }
    sCameraLocation = dglGetUniformLocation(sProgram, "cameraPosition");
    sCameraPitchLocation = dglGetUniformLocation(sProgram, "cameraPitch");
    sFocalLengthLocation = dglGetUniformLocation(sProgram, "focalLength");
    sAspectCorrectionLocation = dglGetUniformLocation(sProgram, "aspectCorrection");
    sImageLocation = dglGetUniformLocation(sProgram, "image");
    sBaseImageLocation = dglGetUniformLocation(sProgram, "baseImage");
    sForegroundImageLocation = dglGetUniformLocation(sProgram, "foregroundImage");
    sDebugColorLocation = dglGetUniformLocation(sProgram, "debugColor");
    sRenderPassLocation = dglGetUniformLocation(sProgram, "renderPass");
    return sCameraLocation >= 0 && sCameraPitchLocation >= 0
        && sFocalLengthLocation >= 0 && sAspectCorrectionLocation >= 0
        && sImageLocation >= 0
        && sBaseImageLocation >= 0 && sForegroundImageLocation >= 0
        && sDebugColorLocation >= 0 && sRenderPassLocation >= 0;
}

static void ConfigureVertexArray(GLuint vertexArray, GLuint vertexBuffer)
{
    dglBindVertexArray(vertexArray);
    dglBindBuffer(GL_ARRAY_BUFFER, vertexBuffer);
    dglEnableVertexAttribArray(0);
    dglVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE,
                           sizeof(struct DioramaTerrainVertex), NULL);
    dglEnableVertexAttribArray(1);
    dglVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE,
                           sizeof(struct DioramaTerrainVertex),
                           (void *)offsetof(struct DioramaTerrainVertex, u));
    dglEnableVertexAttribArray(2);
    dglVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE,
                           sizeof(struct DioramaTerrainVertex),
                           (void *)offsetof(struct DioramaTerrainVertex, shade));
    dglEnableVertexAttribArray(3);
    dglVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE,
                           sizeof(struct DioramaTerrainVertex),
                           (void *)offsetof(struct DioramaTerrainVertex, textureLayer));
    dglEnableVertexAttribArray(4);
    dglVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE,
                           sizeof(struct DioramaTerrainVertex),
                           (void *)offsetof(struct DioramaTerrainVertex, reflectionMask));
    dglEnableVertexAttribArray(5);
    dglVertexAttribPointer(5, 4, GL_UNSIGNED_BYTE, GL_TRUE,
                           sizeof(struct DioramaTerrainVertex),
                            (void *)offsetof(struct DioramaTerrainVertex, color));
}

static bool CreateTreeBuffers(void)
{
    struct DioramaTerrainBounds bounds;

    if (!DioramaTerrain_BuildTreeModel(sScratchVertices,
            DIORAMA_TERRAIN_MAX_VERTICES, &sTreeModelVertexCount, &bounds)
     || sTreeModelVertexCount == 0)
        return false;
    dglGenVertexArrays(1, &sTreeVertexArray);
    dglGenBuffers(1, &sTreeVertexBuffer);
    dglGenBuffers(1, &sTreeInstanceBuffer);
    ConfigureVertexArray(sTreeVertexArray, sTreeVertexBuffer);
    dglBindBuffer(GL_ARRAY_BUFFER, sTreeVertexBuffer);
    dglBufferData(GL_ARRAY_BUFFER,
                  sTreeModelVertexCount * sizeof(struct DioramaTerrainVertex),
                  sScratchVertices, GL_STATIC_DRAW);
    dglBindBuffer(GL_ARRAY_BUFFER, sTreeInstanceBuffer);
    dglEnableVertexAttribArray(6);
    dglVertexAttribPointer(6, 3, GL_FLOAT, GL_FALSE,
                           sizeof(struct DioramaTerrainTreeInstance), NULL);
    dglVertexAttribDivisor(6, 1);
    return true;
}

static struct GLTerrainChunk *FindChunk(int chunkX, int chunkY)
{
    int i;

    for (i = 0; i < TERRAIN_CHUNK_CACHE_SIZE; i++)
        if (sChunks[i].occupied && sChunks[i].chunkX == chunkX && sChunks[i].chunkY == chunkY)
            return &sChunks[i];
    return NULL;
}

static struct GLTerrainChunk *AllocateChunk(void)
{
    struct GLTerrainChunk *oldest = NULL;
    int i;

    for (i = 0; i < TERRAIN_CHUNK_CACHE_SIZE; i++)
    {
        if (!sChunks[i].occupied)
            return &sChunks[i];
        if (!sChunks[i].active
         && (oldest == NULL || sChunks[i].lastSeenSequence < oldest->lastSeenSequence))
            oldest = &sChunks[i];
    }
    return oldest;
}

static const struct DioramaCellSnapshot *FindSnapshotCell(
    const struct DioramaSceneSnapshot *snapshot, int mapX, int mapY)
{
    int gridX = mapX - snapshot->gridOriginX;
    int gridY = mapY - snapshot->gridOriginY;
    int index;

    if (gridX < 0 || gridY < 0 || gridX >= DIORAMA_GRID_WIDTH || gridY >= DIORAMA_GRID_HEIGHT)
        return NULL;
    index = gridY * DIORAMA_GRID_WIDTH + gridX;
    if (index >= snapshot->visibleCellCount
     || snapshot->cells[index].mapX != mapX || snapshot->cells[index].mapY != mapY)
        return NULL;
    return &snapshot->cells[index];
}

static struct DioramaTerrainCell *FindTerrainInputCell(
    struct DioramaTerrainChunkInput *input, int mapX, int mapY)
{
    int x = mapX - input->chunkX * DIORAMA_TERRAIN_CHUNK_SIZE
          + DIORAMA_TERRAIN_HALO;
    int y = mapY - input->chunkY * DIORAMA_TERRAIN_CHUNK_SIZE
          + DIORAMA_TERRAIN_HALO;
    struct DioramaTerrainCell *cell;

    if (x < 0 || y < 0 || x >= DIORAMA_TERRAIN_INPUT_SIZE
     || y >= DIORAMA_TERRAIN_INPUT_SIZE)
        return NULL;
    cell = &input->cells[y * DIORAMA_TERRAIN_INPUT_SIZE + x];
    if (!cell->present || cell->mapX != mapX || cell->mapY != mapY)
        return NULL;
    return cell;
}

static void SetTerrainMaterial(struct DioramaTerrainMaterial *material,
                               uint16_t metatileId, uint8_t layer)
{
    struct DioramaAtlasUv uv = DioramaAtlas_GetUv(metatileId);

    material->metatileId = metatileId;
    material->layer = layer;
    material->rotation = 0;
    material->flags = 0;
    material->u0 = uv.u0;
    material->v0 = uv.v0;
    material->u1 = uv.u1;
    material->v1 = uv.v1;
}

static void SetTerrainMaterialTransformed(struct DioramaTerrainMaterial *material,
                                          uint16_t metatileId, uint8_t layer,
                                          uint8_t rotation, uint8_t flags)
{
    SetTerrainMaterial(material, metatileId, layer);
    material->rotation = rotation;
    material->flags = flags;
}

static uint32_t PixelColor(const struct DioramaSceneSnapshot *snapshot,
                           const struct DioramaGeneratedPixelV2 *pixel)
{
    uint8_t colorIndex = DioramaMetatile_DecodePixel(
        snapshot->tileGraphics, pixel->expectedTileEntry,
        pixel->sourceX & 7, pixel->sourceY & 7);
    uint8_t palette = pixel->expectedTileEntry >> 12;
    uint32_t argb;
    uint8_t red;
    uint8_t green;
    uint8_t blue;

    if (colorIndex == 0 || palette >= 16)
        return 0;
    argb = DioramaMetatile_ConvertColor(
        snapshot->fadedPalette[palette * 16 + colorIndex], false);
    red = (argb >> 16) & 0xFF;
    green = (argb >> 8) & 0xFF;
    blue = argb & 0xFF;
    return red | ((uint32_t)green << 8) | ((uint32_t)blue << 16) | UINT32_C(0xFF000000);
}

static uint8_t TreeShade(const struct DioramaSceneSnapshot *snapshot,
                         const struct DioramaCellSnapshot *cell, int x, int y)
{
    static const uint16_t acceptedColors = (1 << 1) | (1 << 2) | (1 << 3)
                                         | (1 << 4) | (1 << 6) | (1 << 8);
    int subtile = (y / 8) * 2 + x / 8;
    uint16_t entry = cell->tileEntries[4 + subtile];
    uint8_t color = DioramaMetatile_DecodePixel(snapshot->tileGraphics, entry,
                                                x & 7, y & 7);
    uint8_t palette = entry >> 12;
    uint32_t argb;
    uint8_t red;
    uint8_t green;
    uint8_t blue;
    uint8_t darkest;

    if (color == 0)
    {
        entry = cell->tileEntries[subtile];
        color = DioramaMetatile_DecodePixel(snapshot->tileGraphics, entry,
                                            x & 7, y & 7);
        palette = entry >> 12;
    }
    if (color == 0 || palette != 2 || !(acceptedColors & (1 << color)))
        return DIORAMA_TREE_SHADE_OFF;
    argb = DioramaMetatile_ConvertColor(snapshot->unfadedPalette[palette * 16 + color], false);
    red = (argb >> 16) & 0xFF;
    green = (argb >> 8) & 0xFF;
    blue = argb & 0xFF;
    darkest = red < green ? red : green;
    if (blue < darkest)
        darkest = blue;
    if (darkest <= 64)
        return DIORAMA_TREE_SHADE_BLACK;
    if (darkest <= 140)
        return DIORAMA_TREE_SHADE_DARK;
    if (darkest <= 217)
        return DIORAMA_TREE_SHADE_LIGHT;
    return DIORAMA_TREE_SHADE_WHITE;
}

static bool AppendPixelObjects(const struct DioramaSceneSnapshot *snapshot,
                               const struct DioramaResolvedCell *resolvedCells,
                               int chunkX, int chunkY,
                               struct DioramaTerrainChunkInput *input)
{
    size_t objectIndex;

    for (objectIndex = 0; objectIndex < gDioramaPixelObjectV2Count; objectIndex++)
    {
        const struct DioramaGeneratedPixelObjectV2 *object = &gDioramaPixelObjectsV2[objectIndex];
        const struct DioramaGeneratedPixelV2 *pixels;
        const struct DioramaCellSnapshot *anchor = NULL;
        const struct DioramaResolvedCell *anchorResolved = NULL;
        bool supportActive;
        size_t pixelCount;
        size_t pixelIndex;
        int index;
        int translationX;
        int translationY;

        for (index = 0; index < snapshot->visibleCellCount; index++)
            if (resolvedCells[index].pixelObjectId == object->id)
            {
                anchor = &snapshot->cells[index];
                anchorResolved = &resolvedCells[index];
                break;
            }
        if (anchor == NULL || !(anchor->flags & DIORAMA_CELL_SOURCE_VALID))
            continue;
        if (object->layoutId != anchor->sourceLayoutId
         || !((object->mapGroup == UINT8_MAX && object->mapNumber == UINT8_MAX)
           || (object->mapGroup == anchor->sourceMapGroup
            && object->mapNumber == anchor->sourceMapNum)))
            continue;
        supportActive = object->supportStructureId == 0;
        for (index = 0; !supportActive && index < snapshot->visibleCellCount; index++)
            supportActive = resolvedCells[index].structureId == object->supportStructureId;
        if (!supportActive)
            continue;
        translationX = anchor->mapX - anchor->sourceMapX;
        translationY = anchor->mapY - anchor->sourceMapY;
        pixels = DioramaRules_GetPixelObjectPixels(object, &pixelCount);
        if (pixels == NULL)
            return false;
        for (pixelIndex = 0; pixelIndex < pixelCount; pixelIndex++)
        {
            const struct DioramaGeneratedPixelV2 *sourcePixel = &pixels[pixelIndex];
            int sourceX = sourcePixel->sourceCellOffset
                        % gDioramaLayoutsV2[object->layoutId - 1].width;
            int sourceY = sourcePixel->sourceCellOffset
                        / gDioramaLayoutsV2[object->layoutId - 1].width;
            const struct DioramaCellSnapshot *sourceCell = FindSnapshotCell(
                snapshot, sourceX + translationX, sourceY + translationY);
            int32_t xQ32 = sourcePixel->xQ32 + translationX * 32;
            int32_t zQ32 = sourcePixel->zQ32 + translationY * 32;
            int ownerChunkX = DioramaTerrain_FloorDiv(
                xQ32 + sourcePixel->sizeXQ32 / 2, DIORAMA_TERRAIN_CHUNK_SIZE * 32);
            int ownerChunkY = DioramaTerrain_FloorDiv(
                zQ32 + sourcePixel->sizeZQ32 / 2, DIORAMA_TERRAIN_CHUNK_SIZE * 32);
            struct DioramaTerrainPixelPrimitive *target;
            uint32_t color;

            if (ownerChunkX != chunkX || ownerChunkY != chunkY)
                continue;
            if (sourceCell == NULL || sourceCell->sourceLayoutId != object->layoutId
             || sourceCell->metatileId != sourcePixel->expectedMetatile)
                continue;
            color = PixelColor(snapshot, sourcePixel);
            if (color == 0)
                continue;
            if (input->pixelCount >= DIORAMA_TERRAIN_MAX_PIXEL_PRIMITIVES)
                return false;
            target = &input->pixels[input->pixelCount++];
            target->xQ32 = xQ32;
            target->yQ32 = sourcePixel->yQ32 + object->supportOffsetQ16 * 2
                         + (int32_t)(anchorResolved->groundHeight * 32.0f);
            target->zQ32 = zQ32;
            target->rgba = color;
            target->sourceCellOffset = sourcePixel->sourceCellOffset;
            target->objectId = object->id;
            target->structureId = object->structureId;
            target->expectedMetatile = sourcePixel->expectedMetatile;
            target->expectedTileEntry = sourcePixel->expectedTileEntry;
            target->sizeXQ32 = sourcePixel->sizeXQ32;
            target->sizeYQ32 = sourcePixel->sizeYQ32;
            target->sizeZQ32 = sourcePixel->sizeZQ32;
            target->sourceLayer = sourcePixel->sourceLayer;
            target->sourceX = sourcePixel->sourceX;
            target->sourceY = sourcePixel->sourceY;
            target->kind = object->kind;
        }
    }
    return true;
}

static void BuildMountainArtMask(const struct DioramaSceneSnapshot *snapshot,
                                 const struct DioramaCellSnapshot *source,
                                 uint8_t terraceProfile,
                                 uint16_t rows[DIORAMA_VOXELS_PER_CELL],
                                 uint8_t heights[DIORAMA_VOXELS_PER_CELL
                                               * DIORAMA_VOXELS_PER_CELL])
{
    uint32_t pixels[DIORAMA_VOXELS_PER_CELL * DIORAMA_VOXELS_PER_CELL];
    uint16_t remaining[DIORAMA_VOXELS_PER_CELL];
    uint16_t eroded[DIORAMA_VOXELS_PER_CELL];
    unsigned visible = 0;
    unsigned depth = 0;
    unsigned maxDepth = 0;
    int row;

    DioramaMetatile_ComposeLayer(snapshot->tileGraphics, source->tileEntries,
                                 snapshot->unfadedPalette, 1, pixels);
    DioramaMetatile_BuildAlphaMask(pixels, rows);
    for (row = 0; row < DIORAMA_VOXELS_PER_CELL; row++)
        for (uint16_t bits = rows[row]; bits != 0; bits >>= 1)
            visible += bits & 1;
    if (terraceProfile != DIORAMA_TERRACE_NONE)
    {
        DioramaTerrain_BuildTerraceHeightmap(terraceProfile, rows,
            visible != 0 && visible != DIORAMA_VOXELS_PER_CELL * DIORAMA_VOXELS_PER_CELL,
            rows, heights);
        return;
    }

    if (visible == 0 || visible == DIORAMA_VOXELS_PER_CELL * DIORAMA_VOXELS_PER_CELL)
    {
        DioramaMetatile_Compose(snapshot->tileGraphics, source->tileEntries,
                                snapshot->unfadedPalette, pixels);
        DioramaMetatile_BuildSilhouetteMask(pixels, rows);
    }

    memcpy(remaining, rows, sizeof(remaining));
    memset(heights, 0, DIORAMA_VOXELS_PER_CELL * DIORAMA_VOXELS_PER_CELL);
    while (true)
    {
        bool any = false;

        depth++;
        memset(eroded, 0, sizeof(eroded));
        for (row = 0; row < DIORAMA_VOXELS_PER_CELL; row++)
        {
            uint16_t bits = remaining[row];

            if (bits == 0)
                continue;
            any = true;
            for (int x = 0; x < DIORAMA_VOXELS_PER_CELL; x++)
                if (bits & (1u << x))
                    heights[row * DIORAMA_VOXELS_PER_CELL + x] = depth;
            if (row != 0 && row + 1 < DIORAMA_VOXELS_PER_CELL)
                eroded[row] = bits & (uint16_t)(bits << 1) & (bits >> 1)
                            & remaining[row - 1] & remaining[row + 1];
        }
        if (!any)
            break;
        maxDepth = depth;
        memcpy(remaining, eroded, sizeof(remaining));
    }
    if (maxDepth == 0)
        return;
    for (int pixel = 0;
         pixel < DIORAMA_VOXELS_PER_CELL * DIORAMA_VOXELS_PER_CELL; pixel++)
    {
        unsigned pixelDepth = heights[pixel];

        if (pixelDepth == 0)
            continue;
        heights[pixel] = maxDepth == 1 ? DIORAMA_VOXELS_PER_CELL
            : 4 + (pixelDepth - 1) * (DIORAMA_VOXELS_PER_CELL - 4)
                / (maxDepth - 1);
    }
}

static bool BuildInput(const struct DioramaSceneSnapshot *snapshot,
                        const struct DioramaResolvedCell *resolvedCells,
                       int chunkX, int chunkY,
                       struct DioramaTerrainChunkInput *input)
{
    int originX = chunkX * DIORAMA_TERRAIN_CHUNK_SIZE;
    int originY = chunkY * DIORAMA_TERRAIN_CHUNK_SIZE;
    int y;
    int x;
    int face;
    bool hasInterior = false;

    memset(input, 0, sizeof(*input));
    input->chunkX = chunkX;
    input->chunkY = chunkY;
    input->mapGeneration = snapshot->mapGeneration;
    input->rulesGeneration = DioramaRules_GetGeneration();
    for (y = 0; y < DIORAMA_TERRAIN_INPUT_SIZE; y++)
    {
        for (x = 0; x < DIORAMA_TERRAIN_INPUT_SIZE; x++)
        {
            int mapX = originX + x - DIORAMA_TERRAIN_HALO;
            int mapY = originY + y - DIORAMA_TERRAIN_HALO;
            int gridX = mapX - snapshot->gridOriginX;
            int gridY = mapY - snapshot->gridOriginY;
            int gridIndex;
            const struct DioramaCellSnapshot *source;
            struct DioramaTerrainCell *cell;

            cell = &input->cells[y * DIORAMA_TERRAIN_INPUT_SIZE + x];
            cell->mapX = mapX;
            cell->mapY = mapY;
            if (gridX < 0 || gridY < 0 || gridX >= DIORAMA_GRID_WIDTH || gridY >= DIORAMA_GRID_HEIGHT)
                continue;
            gridIndex = gridY * DIORAMA_GRID_WIDTH + gridX;
            if (gridIndex >= snapshot->visibleCellCount)
                continue;
            source = &snapshot->cells[gridIndex];
            if (source->mapX != mapX || source->mapY != mapY || source->metatileId >= DIORAMA_TILE_COUNT)
                continue;
            cell->present = true;
            cell->mapX = source->mapX;
            cell->mapY = source->mapY;
            cell->metatileId = source->metatileId;
            cell->behavior = source->behavior;
            cell->layerType = source->layerType;
            cell->collision = source->collision;
            cell->rawElevation = source->elevation;
            cell->reflective = MetatileBehavior_IsReflective(source->behavior);
            cell->shape = resolvedCells[gridIndex].shape;
            cell->archetype = resolvedCells[gridIndex].archetype;
            cell->terrainClass = resolvedCells[gridIndex].terrainClass;
            cell->semanticProfile = resolvedCells[gridIndex].semanticProfile;
            cell->profile = resolvedCells[gridIndex].profile;
            cell->planeAxis = resolvedCells[gridIndex].planeAxis;
            cell->effectiveElevation = resolvedCells[gridIndex].effectiveElevation;
            cell->surfaceCount = resolvedCells[gridIndex].surfaceCount;
            cell->claimOwner = resolvedCells[gridIndex].claimOwner;
            cell->regionId = resolvedCells[gridIndex].regionId;
            cell->structureId = resolvedCells[gridIndex].structureId;
            cell->rulePriority = resolvedCells[gridIndex].rulePriority;
            cell->structureTemplateId = resolvedCells[gridIndex].structureTemplateId;
            cell->structureX = DioramaTerrain_VisibleStructureOrigin(
                source->mapX, resolvedCells[gridIndex].structureLocalX);
            cell->structureY = DioramaTerrain_VisibleStructureOrigin(
                source->mapY, resolvedCells[gridIndex].structureLocalY);
            cell->structureWidth = resolvedCells[gridIndex].structureWidth;
            cell->structureHeight = resolvedCells[gridIndex].structureHeight;
            cell->structureRoofRows = resolvedCells[gridIndex].structureRoofRows;
            cell->structureLocalX = resolvedCells[gridIndex].structureLocalX;
            cell->structureLocalY = resolvedCells[gridIndex].structureLocalY;
            cell->structureOwnerKind = resolvedCells[gridIndex].structureOwnerKind;
            cell->doorFold = resolvedCells[gridIndex].doorFold;
            cell->voidKind = resolvedCells[gridIndex].voidKind;
            cell->cliffEdgeMask = resolvedCells[gridIndex].cliffEdgeMask;
            cell->cliffBaseMask = resolvedCells[gridIndex].cliffBaseMask;
            cell->cliffTransitionMask = resolvedCells[gridIndex].cliffTransitionMask;
            cell->cliffCornerMask = resolvedCells[gridIndex].cliffCornerMask;
            cell->terraceProfile = resolvedCells[gridIndex].terraceProfile;
            cell->groundHeight = resolvedCells[gridIndex].groundHeight;
            cell->visualHeight = resolvedCells[gridIndex].topHeight;
            cell->featureHeight = resolvedCells[gridIndex].featureHeight;
            cell->structureBodyHeight = resolvedCells[gridIndex].structureBodyHeight;
            cell->structureRoofHeight = resolvedCells[gridIndex].structureRoofHeight;
            cell->southFacadeUnitHeight = resolvedCells[gridIndex].structureSouthFacadeUnitHeight;
            cell->measuredAxis = resolvedCells[gridIndex].measuredAxis;
            cell->measuredExtentBands = resolvedCells[gridIndex].measuredExtentBands;
            cell->measuredPeriodBands = resolvedCells[gridIndex].measuredPeriodBands;
            cell->measuredRoofBands = resolvedCells[gridIndex].measuredRoofBands;
            cell->measuredRunLocal = resolvedCells[gridIndex].measuredRunLocal;
            cell->measuredRunLength = resolvedCells[gridIndex].measuredRunLength;
            cell->measuredFlags = resolvedCells[gridIndex].measuredFlags;
            cell->measuredBandCount = resolvedCells[gridIndex].measuredBandCount;
            cell->measuredConfidence = resolvedCells[gridIndex].measuredConfidence;
            for (face = 0; face < cell->surfaceCount; face++)
            {
                cell->surfaces[face].bottomHeight = resolvedCells[gridIndex].surfaces[face].bottomHeight;
                cell->surfaces[face].topHeight = resolvedCells[gridIndex].surfaces[face].topHeight;
                cell->surfaces[face].gameplayElevation = resolvedCells[gridIndex].surfaces[face].gameplayElevation;
            }
            for (face = 0; face < DIORAMA_MATERIAL_FACE_COUNT; face++)
            {
                uint16_t materialId = resolvedCells[gridIndex].materials[face].metatileId;
                uint8_t materialLayer = resolvedCells[gridIndex].materials[face].layer;

                if (resolvedCells[gridIndex].pixelObjectId != 0
                 && resolvedCells[gridIndex].groundMode == DIORAMA_PIXEL_GROUND_SOURCE_BASE)
                {
                    materialId = source->metatileId;
                    materialLayer = DIORAMA_MATERIAL_BASE;
                }
                else if (resolvedCells[gridIndex].pixelObjectId != 0
                      && resolvedCells[gridIndex].baseMetatileId != DIORAMA_MATERIAL_METATILE_SELF)
                {
                    materialId = resolvedCells[gridIndex].baseMetatileId;
                    materialLayer = DIORAMA_MATERIAL_FULL;
                }
                if (materialId == DIORAMA_MATERIAL_METATILE_SELF)
                    materialId = source->metatileId;
                SetTerrainMaterialTransformed(
                    &cell->materials[face], materialId,
                    materialLayer,
                    resolvedCells[gridIndex].materials[face].rotation,
                    resolvedCells[gridIndex].materials[face].flags);
                SetTerrainMaterial(&cell->underlayMaterials[face], source->metatileId,
                                   DIORAMA_MATERIAL_BASE);
            }
            for (face = 0; face < cell->measuredBandCount; face++)
            {
                float middle;

                SetTerrainMaterial(&cell->measuredBands[face],
                    resolvedCells[gridIndex].measuredBands[face].metatileId,
                    resolvedCells[gridIndex].measuredBands[face].layer);
                if (cell->measuredAxis == DIORAMA_PLANE_AXIS_Z)
                    middle = (cell->measuredBands[face].v0
                            + cell->measuredBands[face].v1) * 0.5f;
                else
                    middle = (cell->measuredBands[face].u0
                            + cell->measuredBands[face].u1) * 0.5f;
                if (cell->measuredAxis == DIORAMA_PLANE_AXIS_Z)
                {
                    if (resolvedCells[gridIndex].measuredBands[face].sourceHalf == 0)
                        cell->measuredBands[face].v1 = middle;
                    else
                        cell->measuredBands[face].v0 = middle;
                }
                else
                {
                    if (resolvedCells[gridIndex].measuredBands[face].sourceHalf == 0)
                        cell->measuredBands[face].u1 = middle;
                    else
                        cell->measuredBands[face].u0 = middle;
                    cell->measuredBands[face].rotation = 1;
                }
            }
            if (cell->measuredFlags & DIORAMA_MEASURED_SILHOUETTE)
            {
                bool hasForeground = false;

                DioramaAtlas_GetSilhouetteAlphaMask(source->metatileId,
                                                    cell->foregroundAlpha);
                for (face = 0; face < DIORAMA_VOXELS_PER_CELL; face++)
                    hasForeground |= cell->foregroundAlpha[face] != 0;
                if (!hasForeground)
                    for (face = 0; face < DIORAMA_VOXELS_PER_CELL; face++)
                        cell->foregroundAlpha[face] = 0xFFFF;
            }
            if (cell->measuredFlags & DIORAMA_MEASURED_MOUNTAIN_ART)
                BuildMountainArtMask(snapshot, source, cell->terraceProfile,
                                     cell->foregroundAlpha,
                                     cell->mountainHeight);
            if (cell->shape == DIORAMA_SHAPE_LEDGE)
            {
                DioramaAtlas_GetForegroundAlphaMask(source->metatileId,
                                                    cell->foregroundAlpha);
                SetTerrainMaterial(&cell->materials[DIORAMA_MATERIAL_FACE_TOP],
                                   source->metatileId, DIORAMA_MATERIAL_BASE);
                for (face = DIORAMA_MATERIAL_FACE_NORTH;
                     face <= DIORAMA_MATERIAL_FACE_WEST; face++)
                    SetTerrainMaterial(&cell->materials[face], source->metatileId,
                                        DIORAMA_MATERIAL_FOREGROUND);
            }
            if (cell->archetype == DIORAMA_ARCHETYPE_ROUND_HULL
             || cell->archetype == DIORAMA_ARCHETYPE_GROUPED_HULL)
            {
                int pixel;

                SetTerrainMaterial(&cell->treeMaterial, source->metatileId,
                                   DIORAMA_MATERIAL_FULL);
                cell->treeMaskDirect = true;
                for (pixel = 0;
                     pixel < DIORAMA_VOXELS_PER_CELL * DIORAMA_VOXELS_PER_CELL;
                     pixel++)
                    cell->treeShade[pixel] = TreeShade(
                        snapshot, source, pixel % DIORAMA_VOXELS_PER_CELL,
                        pixel / DIORAMA_VOXELS_PER_CELL);
            }
            else if (cell->shape == DIORAMA_SHAPE_CLIFF)
            {
                uint16_t alpha[DIORAMA_VOXELS_PER_CELL];
                bool hasForeground = false;

                DioramaAtlas_GetForegroundAlphaMask(source->metatileId, alpha);
                for (face = 0; face < DIORAMA_VOXELS_PER_CELL; face++)
                    hasForeground |= alpha[face] != 0;
                SetTerrainMaterial(&cell->materials[DIORAMA_MATERIAL_FACE_TOP],
                                   source->metatileId,
                                   cell->terraceProfile != DIORAMA_TERRACE_NONE
                                     ? DIORAMA_MATERIAL_FULL
                                     : DIORAMA_MATERIAL_BASE);
                if (hasForeground)
                    for (face = DIORAMA_MATERIAL_FACE_NORTH;
                         face <= DIORAMA_MATERIAL_FACE_WEST; face++)
                        SetTerrainMaterial(&cell->materials[face], source->metatileId,
                            cell->terraceProfile != DIORAMA_TERRACE_NONE
                              ? DIORAMA_MATERIAL_FULL : DIORAMA_MATERIAL_FOREGROUND);
            }
            if (resolvedCells[gridIndex].structureSouthFacadeRows != 0
             && cell->structureLocalY == cell->structureHeight - 1)
            {
                uint8_t facadeRow;

                for (facadeRow = 0;
                     facadeRow < resolvedCells[gridIndex].structureSouthFacadeRows;
                     facadeRow++)
                {
                    const struct DioramaCellSnapshot *facadeSource = FindSnapshotCell(
                        snapshot, cell->structureX + cell->structureLocalX,
                        cell->structureY + cell->structureHeight - 1 - facadeRow);
                    uint16_t materialId = resolvedCells[gridIndex]
                        .materials[DIORAMA_MATERIAL_FACE_SOUTH].metatileId;

                    if (facadeSource == NULL || facadeSource->metatileId >= DIORAMA_TILE_COUNT)
                        break;
                    if (materialId == DIORAMA_MATERIAL_METATILE_SELF)
                        materialId = facadeSource->metatileId;
                    SetTerrainMaterial(&cell->southFacadeMaterials[facadeRow], materialId,
                        resolvedCells[gridIndex].materials[DIORAMA_MATERIAL_FACE_SOUTH].layer);
                }
                if (facadeRow == resolvedCells[gridIndex].structureSouthFacadeRows)
                    cell->southFacadeCount = facadeRow;
            }
            if (x >= DIORAMA_TERRAIN_HALO && x < DIORAMA_TERRAIN_HALO + DIORAMA_TERRAIN_CHUNK_SIZE
             && y >= DIORAMA_TERRAIN_HALO && y < DIORAMA_TERRAIN_HALO + DIORAMA_TERRAIN_CHUNK_SIZE)
                hasInterior = true;
        }
    }
    for (y = 0; y < DIORAMA_TERRAIN_INPUT_SIZE; y++)
        for (x = 0; x < DIORAMA_TERRAIN_INPUT_SIZE; x++)
        {
            struct DioramaTerrainCell *cell =
                &input->cells[y * DIORAMA_TERRAIN_INPUT_SIZE + x];

            if (!cell->present)
                continue;
            if (cell->archetype == DIORAMA_ARCHETYPE_ROUND_HULL)
            {
                if (cell->metatileId == 198 || cell->metatileId == 199)
                    continue;
                for (face = 0;
                     !cell->treeHullReady
                     && face < DIORAMA_VOXELS_PER_CELL * DIORAMA_VOXELS_PER_CELL;
                     face++)
                    cell->treeHullReady = cell->treeShade[face] != DIORAMA_TREE_SHADE_OFF;
            }
            else if (cell->archetype == DIORAMA_ARCHETYPE_GROUPED_HULL)
            {
                int bodyRows = cell->structureHeight >= 2 ? 2 : 1;
                int bodyStart = cell->structureHeight - bodyRows;
                bool ready = cell->structureId != 0 && cell->structureWidth > 0
                          && cell->structureWidth <= 2;

                for (int sourceY = 0; ready && sourceY < bodyRows; sourceY++)
                    for (int sourceX = 0; ready && sourceX < cell->structureWidth; sourceX++)
                    {
                        struct DioramaTerrainCell *sourceCell = FindTerrainInputCell(
                            input, cell->structureX + sourceX,
                            cell->structureY + bodyStart + sourceY);

                        ready = sourceCell != NULL
                             && sourceCell->archetype == DIORAMA_ARCHETYPE_GROUPED_HULL
                             && sourceCell->structureId == cell->structureId;
                    }
                cell->treeHullReady = ready;
            }
            if ((cell->archetype == DIORAMA_ARCHETYPE_ROUND_HULL
              || cell->archetype == DIORAMA_ARCHETYPE_GROUPED_HULL)
             && !cell->treeHullReady
             && !(cell->archetype == DIORAMA_ARCHETYPE_GROUPED_HULL
               && cell->structureWidth == 2))
                for (face = 0; face < DIORAMA_MATERIAL_FACE_COUNT; face++)
                    SetTerrainMaterial(&cell->materials[face], cell->metatileId,
                                       DIORAMA_MATERIAL_FULL);
        }
    {
        uint16_t counts[DIORAMA_TILE_COUNT] = {0};
        uint16_t groundMetatile = 1;

        for (int index = 0; index < snapshot->visibleCellCount; index++)
            if (snapshot->cells[index].metatileId < DIORAMA_TILE_COUNT
             && resolvedCells[index].shape == DIORAMA_SHAPE_FLAT
             && resolvedCells[index].archetype == DIORAMA_ARCHETYPE_GROUND)
                counts[snapshot->cells[index].metatileId]++;
        for (uint16_t metatile = 0; metatile < DIORAMA_TILE_COUNT; metatile++)
            if (counts[metatile] > counts[groundMetatile])
                groundMetatile = metatile;
        for (y = 0; y < DIORAMA_TERRAIN_INPUT_SIZE; y++)
            for (x = 0; x < DIORAMA_TERRAIN_INPUT_SIZE; x++)
            {
                struct DioramaTerrainCell *cell =
                    &input->cells[y * DIORAMA_TERRAIN_INPUT_SIZE + x];

                if (cell->present
                 && (cell->treeHullReady
                  || (cell->archetype == DIORAMA_ARCHETYPE_GROUPED_HULL
                   && cell->structureWidth == 2)))
                    for (face = 0; face < DIORAMA_MATERIAL_FACE_COUNT; face++)
                        SetTerrainMaterial(&cell->underlayMaterials[face], groundMetatile,
                                           DIORAMA_MATERIAL_FULL);
            }
    }
    return hasInterior && AppendPixelObjects(snapshot, resolvedCells, chunkX, chunkY, input);
}

static bool UploadChunk(struct GLTerrainChunk *chunk,
                        const struct DioramaTerrainChunkInput *input,
                        uint64_t signature)
{
    struct DioramaTerrainMesh mesh;

    if (!DioramaTerrain_BuildChunk(input, sScratchVertices,
                                   DIORAMA_TERRAIN_MAX_VERTICES, &mesh))
        return false;
    if (chunk->vertexArray == 0)
    {
        dglGenVertexArrays(1, &chunk->vertexArray);
        dglGenBuffers(1, &chunk->vertexBuffer);
        ConfigureVertexArray(chunk->vertexArray, chunk->vertexBuffer);
    }
    dglBindBuffer(GL_ARRAY_BUFFER, chunk->vertexBuffer);
    dglBufferData(GL_ARRAY_BUFFER,
                  mesh.vertexCount * sizeof(struct DioramaTerrainVertex),
                  sScratchVertices, GL_DYNAMIC_DRAW);
    chunk->occupied = true;
    chunk->chunkX = input->chunkX;
    chunk->chunkY = input->chunkY;
    chunk->mapGeneration = input->mapGeneration;
    chunk->rulesGeneration = input->rulesGeneration;
    chunk->signature = signature;
    chunk->mesh = mesh;
    return true;
}

bool DioramaGLTerrain_Init(void)
{
    memset(sChunks, 0, sizeof(sChunks));
    memset(&sMetrics, 0, sizeof(sMetrics));
    sMapGeneration = 0;
    sMapEditGeneration = 0;
    sRulesGeneration = 0;
    sLastSyncSequence = 0;
    if (!CreateProgram())
        return false;
    if (!CreateTreeBuffers())
        return false;
    dglGenVertexArrays(1, &sDebugVertexArray);
    dglGenBuffers(1, &sDebugVertexBuffer);
    ConfigureVertexArray(sDebugVertexArray, sDebugVertexBuffer);
    return true;
}

void DioramaGLTerrain_Reset(void)
{
    int i;

    for (i = 0; i < TERRAIN_CHUNK_CACHE_SIZE; i++)
    {
        sChunks[i].occupied = false;
        sChunks[i].active = false;
        sChunks[i].signature = 0;
    }
    memset(&sMetrics, 0, sizeof(sMetrics));
}

bool DioramaGLTerrain_Sync(const struct DioramaSceneSnapshot *snapshot,
                           const struct DioramaResolvedCell *resolvedCells)
{
    static struct DioramaTerrainChunkInput input;
    int minChunkX;
    int maxChunkX;
    int minChunkY;
    int maxChunkY;
    int chunkX;
    int chunkY;
    int i;
    uint32_t rulesGeneration = DioramaRules_GetGeneration();
    bool forceAllDirty = DioramaTerrain_ShouldInvalidateAll(
        sLastSyncSequence, snapshot->sequence,
        sMapEditGeneration, snapshot->mapEditGeneration,
        snapshot->dirtyCellCount, snapshot->dirtyOverflow);

    sClaimDebugVertexCount = 0;
    for (i = 0; i < snapshot->visibleCellCount && i < DIORAMA_MAX_VISIBLE_CELLS; i++)
    {
        const struct DioramaCellSnapshot *cell = &snapshot->cells[i];
        const struct DioramaResolvedCell *resolved = &resolvedCells[i];
        static const int8_t directions[4][2] = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}};
        uint8_t direction;

        if (resolved->structureId == 0 || resolved->shape == DIORAMA_SHAPE_HIDDEN)
            continue;
        for (direction = 0; direction < 4; direction++)
        {
            int gridX = cell->mapX + directions[direction][0] - snapshot->gridOriginX;
            int gridY = cell->mapY + directions[direction][1] - snapshot->gridOriginY;
            uint16_t neighborId = 0;
            struct DioramaTerrainVertex *first;
            struct DioramaTerrainVertex *second;
            float x0 = cell->mapX;
            float x1 = cell->mapX + 1.0f;
            float z0 = cell->mapY;
            float z1 = cell->mapY + 1.0f;
            float y = resolved->topHeight + 0.04f;

            if (gridX >= 0 && gridY >= 0 && gridX < DIORAMA_GRID_WIDTH
             && gridY < DIORAMA_GRID_HEIGHT)
            {
                int neighbor = gridY * DIORAMA_GRID_WIDTH + gridX;
                if (neighbor < snapshot->visibleCellCount
                 && snapshot->cells[neighbor].mapX == cell->mapX + directions[direction][0]
                 && snapshot->cells[neighbor].mapY == cell->mapY + directions[direction][1])
                    neighborId = resolvedCells[neighbor].structureId;
            }
            if (neighborId == resolved->structureId
             || sClaimDebugVertexCount + 2 > TERRAIN_DEBUG_MAX_VERTICES)
                continue;
            first = &sDebugVertices[sClaimDebugVertexCount++];
            second = &sDebugVertices[sClaimDebugVertexCount++];
            memset(first, 0, sizeof(*first));
            memset(second, 0, sizeof(*second));
            first->y = second->y = y;
            first->shade = second->shade = 1.0f;
            if (direction == 0 || direction == 2)
            {
                first->x = x0;
                second->x = x1;
                first->z = second->z = direction == 0 ? z0 : z1;
            }
            else
            {
                first->z = z0;
                second->z = z1;
                first->x = second->x = direction == 3 ? x0 : x1;
            }
        }
    }

    sMetrics.rebuiltChunks = 0;
    if (snapshot->mapGeneration != sMapGeneration || rulesGeneration != sRulesGeneration)
    {
        DioramaGLTerrain_Reset();
        sMapGeneration = snapshot->mapGeneration;
        sRulesGeneration = rulesGeneration;
    }
    for (i = 0; i < TERRAIN_CHUNK_CACHE_SIZE; i++)
        sChunks[i].active = false;
    minChunkX = DioramaTerrain_FloorDiv(snapshot->gridOriginX, DIORAMA_TERRAIN_CHUNK_SIZE);
    maxChunkX = DioramaTerrain_FloorDiv(snapshot->gridOriginX + DIORAMA_GRID_WIDTH - 1,
                                       DIORAMA_TERRAIN_CHUNK_SIZE);
    minChunkY = DioramaTerrain_FloorDiv(snapshot->gridOriginY, DIORAMA_TERRAIN_CHUNK_SIZE);
    maxChunkY = DioramaTerrain_FloorDiv(snapshot->gridOriginY + DIORAMA_GRID_HEIGHT - 1,
                                       DIORAMA_TERRAIN_CHUNK_SIZE);
    for (chunkY = minChunkY; chunkY <= maxChunkY; chunkY++)
    {
        for (chunkX = minChunkX; chunkX <= maxChunkX; chunkX++)
        {
            struct GLTerrainChunk *chunk;
            uint64_t signature;
            bool newIdentity = false;
            bool forceDirty = forceAllDirty;
            uint8_t dirtyIndex;

            if (!BuildInput(snapshot, resolvedCells, chunkX, chunkY, &input))
                continue;
            signature = DioramaTerrain_ChunkSignature(&input);
            for (dirtyIndex = 0; !forceDirty && dirtyIndex < snapshot->dirtyCellCount; dirtyIndex++)
                forceDirty = DioramaTerrain_DirtyCellAffectsChunk(
                    snapshot->dirtyCells[dirtyIndex].mapX,
                    snapshot->dirtyCells[dirtyIndex].mapY,
                    chunkX, chunkY);
            chunk = FindChunk(chunkX, chunkY);
            if (chunk == NULL)
            {
                chunk = AllocateChunk();
                newIdentity = true;
            }
            if (chunk == NULL)
                return false;
            if (newIdentity || !chunk->occupied || chunk->mapGeneration != snapshot->mapGeneration
             || chunk->rulesGeneration != rulesGeneration
             || chunk->signature != signature || forceDirty)
            {
                if (!UploadChunk(chunk, &input, signature))
                    return false;
                sMetrics.rebuiltChunks++;
            }
            chunk->active = true;
            chunk->lastSeenSequence = snapshot->sequence;
        }
    }

    sMetrics.residentChunks = 0;
    sMetrics.activeChunks = 0;
    for (i = 0; i < TERRAIN_CHUNK_CACHE_SIZE; i++)
    {
        if (sChunks[i].occupied) sMetrics.residentChunks++;
        if (sChunks[i].active) sMetrics.activeChunks++;
    }
    sMapEditGeneration = snapshot->mapEditGeneration;
    sLastSyncSequence = snapshot->sequence;
    return sMetrics.activeChunks != 0;
}

static uint32_t AppendBoundsVertices(uint32_t count, const struct DioramaTerrainBounds *bounds)
{
    static const uint8_t cornerOrder[8] = {0, 1, 1, 2, 2, 3, 3, 0};
    float x[4] = {bounds->minX, bounds->maxX, bounds->maxX, bounds->minX};
    float z[4] = {bounds->maxZ, bounds->maxZ, bounds->minZ, bounds->minZ};
    float y = bounds->maxY + 0.03f;
    int i;

    for (i = 0; i < 8 && count < TERRAIN_DEBUG_MAX_VERTICES; i++, count++)
    {
        int corner = cornerOrder[i];
        sDebugVertices[count].x = x[corner];
        sDebugVertices[count].y = y;
        sDebugVertices[count].z = z[corner];
        sDebugVertices[count].u = 0.0f;
        sDebugVertices[count].v = 0.0f;
        sDebugVertices[count].shade = 1.0f;
        sDebugVertices[count].textureLayer = DIORAMA_TERRAIN_TEXTURE_FULL;
        sDebugVertices[count].reflectionMask = 0.0f;
    }
    return count;
}

void DioramaGLTerrain_Draw(GLuint atlasTexture, GLuint baseAtlasTexture,
                           GLuint foregroundAtlasTexture, float cameraX, float cameraZ,
                           float cameraPitch, float focalLength,
                           float aspectCorrection, bool debug)
{
    uint32_t debugVertexCount = sClaimDebugVertexCount;
    uint32_t visibleTreeCount = 0;
    int i;

    sMetrics.visibleChunks = 0;
    sMetrics.culledChunks = 0;
    sMetrics.vertices = 0;
    sMetrics.triangles = 0;
    sMetrics.drawCalls = 0;
    dglUseProgram(sProgram);
    dglUniform2f(sCameraLocation, cameraX, cameraZ);
    dglUniform1f(sCameraPitchLocation, cameraPitch);
    dglUniform1f(sFocalLengthLocation, focalLength);
    dglUniform1f(sAspectCorrectionLocation, aspectCorrection);
    dglUniform1i(sImageLocation, 0);
    dglUniform1i(sBaseImageLocation, 1);
    dglUniform1i(sForegroundImageLocation, 2);
    dglUniform4f(sDebugColorLocation, 0.0f, 0.0f, 0.0f, -1.0f);
    dglUniform1i(sRenderPassLocation, 0);
    dglActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, atlasTexture);
    dglActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, baseAtlasTexture);
    dglActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, foregroundAtlasTexture);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    if (debug)
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

    for (i = 0; i < TERRAIN_CHUNK_CACHE_SIZE; i++)
    {
        struct GLTerrainChunk *chunk = &sChunks[i];

        if (!chunk->active)
            continue;
        if (!DioramaTerrain_IsBoundsVisible(&chunk->mesh.bounds, cameraX, cameraZ,
                                             cameraPitch, focalLength))
        {
            sMetrics.culledChunks++;
            continue;
        }
        sMetrics.visibleChunks++;
        sMetrics.vertices += chunk->mesh.vertexCount;
        sMetrics.triangles += chunk->mesh.vertexCount / 3;
        sMetrics.drawCalls++;
        dglBindVertexArray(chunk->vertexArray);
        glDrawArrays(GL_TRIANGLES, 0, chunk->mesh.vertexCount);
        for (uint16_t tree = 0; tree < chunk->mesh.treeInstanceCount
             && visibleTreeCount < TERRAIN_MAX_VISIBLE_TREE_INSTANCES; tree++)
            sVisibleTreeInstances[visibleTreeCount++] = chunk->mesh.treeInstances[tree];
        if (debug)
            debugVertexCount = AppendBoundsVertices(debugVertexCount, &chunk->mesh.bounds);
    }
    if (visibleTreeCount != 0)
    {
        dglBindVertexArray(sTreeVertexArray);
        dglBindBuffer(GL_ARRAY_BUFFER, sTreeInstanceBuffer);
        dglBufferData(GL_ARRAY_BUFFER,
                      visibleTreeCount * sizeof(struct DioramaTerrainTreeInstance),
                      sVisibleTreeInstances, GL_STREAM_DRAW);
        dglDrawArraysInstanced(GL_TRIANGLES, 0, sTreeModelVertexCount, visibleTreeCount);
        sMetrics.vertices += sTreeModelVertexCount * visibleTreeCount;
        sMetrics.triangles += sTreeModelVertexCount * visibleTreeCount / 3;
        sMetrics.drawCalls++;
    }
    sMetrics.visibleTreeInstances = visibleTreeCount;
    sMetrics.treeModelVertices = sTreeModelVertexCount;
    if (debug)
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glEnable(GL_STENCIL_TEST);
    glStencilMask(0x01);
    glStencilFunc(GL_ALWAYS, 1, 0x01);
    glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glDepthMask(GL_FALSE);
    dglUniform1i(sRenderPassLocation, 1);
    for (i = 0; i < TERRAIN_CHUNK_CACHE_SIZE; i++)
    {
        struct GLTerrainChunk *chunk = &sChunks[i];

        if (!chunk->active
         || !DioramaTerrain_IsBoundsVisible(&chunk->mesh.bounds, cameraX, cameraZ,
                                             cameraPitch, focalLength))
            continue;
        dglBindVertexArray(chunk->vertexArray);
        glDrawArrays(GL_TRIANGLES, 0, chunk->mesh.vertexCount);
        sMetrics.drawCalls++;
    }
    glStencilMask(0x02);
    glStencilFunc(GL_ALWAYS, 2, 0x02);
    dglUniform1i(sRenderPassLocation, 2);
    for (i = 0; i < TERRAIN_CHUNK_CACHE_SIZE; i++)
    {
        struct GLTerrainChunk *chunk = &sChunks[i];

        if (!chunk->active
         || !DioramaTerrain_IsBoundsVisible(&chunk->mesh.bounds, cameraX, cameraZ,
                                             cameraPitch, focalLength))
            continue;
        dglBindVertexArray(chunk->vertexArray);
        glDrawArrays(GL_TRIANGLES, 0, chunk->mesh.vertexCount);
        sMetrics.drawCalls++;
    }
    dglUniform1i(sRenderPassLocation, 0);
    glDepthMask(GL_TRUE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glStencilMask(0x00);
    glDisable(GL_STENCIL_TEST);
    if (debug)
    {
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        dglUniform4f(sDebugColorLocation, 1.0f, 0.85f, 0.1f, 1.0f);
        dglBindVertexArray(sDebugVertexArray);
        dglBindBuffer(GL_ARRAY_BUFFER, sDebugVertexBuffer);
        dglBufferData(GL_ARRAY_BUFFER,
                      debugVertexCount * sizeof(struct DioramaTerrainVertex),
                      sDebugVertices, GL_STREAM_DRAW);
        glDrawArrays(GL_LINES, 0, debugVertexCount);
        sMetrics.drawCalls++;
    }
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
}

const struct DioramaTerrainMetrics *DioramaGLTerrain_GetMetrics(void)
{
    return &sMetrics;
}

void DioramaGLTerrain_Shutdown(void)
{
    int i;

    for (i = 0; i < TERRAIN_CHUNK_CACHE_SIZE; i++)
    {
        if (sChunks[i].vertexBuffer != 0)
            dglDeleteBuffers(1, &sChunks[i].vertexBuffer);
        if (sChunks[i].vertexArray != 0)
            dglDeleteVertexArrays(1, &sChunks[i].vertexArray);
    }
    if (sDebugVertexBuffer != 0)
        dglDeleteBuffers(1, &sDebugVertexBuffer);
    if (sDebugVertexArray != 0)
        dglDeleteVertexArrays(1, &sDebugVertexArray);
    if (sTreeInstanceBuffer != 0)
        dglDeleteBuffers(1, &sTreeInstanceBuffer);
    if (sTreeVertexBuffer != 0)
        dglDeleteBuffers(1, &sTreeVertexBuffer);
    if (sTreeVertexArray != 0)
        dglDeleteVertexArrays(1, &sTreeVertexArray);
    if (sProgram != 0)
        dglDeleteProgram(sProgram);
    memset(sChunks, 0, sizeof(sChunks));
    sDebugVertexBuffer = 0;
    sDebugVertexArray = 0;
    sTreeInstanceBuffer = 0;
    sTreeVertexBuffer = 0;
    sTreeVertexArray = 0;
    sTreeModelVertexCount = 0;
    sProgram = 0;
}

#endif
