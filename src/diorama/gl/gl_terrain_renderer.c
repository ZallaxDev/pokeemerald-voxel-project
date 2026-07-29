#ifdef ENABLE_DIORAMA

#include <stddef.h>
#include <string.h>
#include <SDL2/SDL.h>

#include "diorama/gl_loader.h"
#include "diorama/gl_terrain_renderer.h"
#include "diorama/metatile_atlas.h"
#include "diorama/rules.h"
#include "diorama/terrain_mesh.h"

#define TERRAIN_CHUNK_CACHE_SIZE 36
#define TERRAIN_DEBUG_MAX_VERTICES (TERRAIN_CHUNK_CACHE_SIZE * 8)

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
static struct DioramaResolvedCell sResolvedCells[DIORAMA_MAX_VISIBLE_CELLS];
static struct DioramaTerrainMetrics sMetrics;
static GLuint sProgram;
static GLuint sDebugVertexArray;
static GLuint sDebugVertexBuffer;
static GLint sCameraLocation;
static GLint sCameraPitchLocation;
static GLint sFocalLengthLocation;
static GLint sImageLocation;
static GLint sBaseImageLocation;
static GLint sForegroundImageLocation;
static GLint sDebugColorLocation;
static uint32_t sMapGeneration;
static uint32_t sMapEditGeneration;
static uint32_t sRulesGeneration;
static uint64_t sLastSyncSequence;

static const char sTerrainVertexShader[] =
    "#version 330 core\n"
    "layout(location = 0) in vec3 position;\n"
    "layout(location = 1) in vec2 texCoord;\n"
    "layout(location = 2) in float vertexShade;\n"
    "layout(location = 3) in float vertexTextureLayer;\n"
    "uniform vec2 cameraPosition;\n"
    "uniform float cameraPitch;\n"
    "uniform float focalLength;\n"
    "out vec2 uv;\n"
    "out float shade;\n"
    "flat out int textureLayer;\n"
    "void main() {\n"
    "  const float cameraHeight = 16.0;\n"
    "  const float cameraTargetVertical = -0.458944;\n"
    "  const float nearDepth = 0.1;\n"
    "  const float farDepth = 80.0;\n"
    "  float pitchSin = sin(cameraPitch);\n"
    "  float pitchCos = cos(cameraPitch);\n"
    "  float cameraDistance = (cameraHeight * pitchCos + cameraTargetVertical) / pitchSin;\n"
    "  float relativeX = position.x - cameraPosition.x;\n"
    "  float relativeZ = position.z - cameraPosition.y;\n"
    "  float depth = (cameraHeight - position.y) * pitchSin + (relativeZ + cameraDistance) * pitchCos;\n"
    "  float vertical = -(cameraHeight - position.y) * pitchCos + (relativeZ + cameraDistance) * pitchSin;\n"
    "  float clipX = relativeX * focalLength * 2.0 / 240.0;\n"
    "  float clipY = -0.04 * depth + vertical * focalLength * 2.0 / 160.0;\n"
    "  float clipZ = ((farDepth + nearDepth) / (farDepth - nearDepth)) * depth"
    "              - (2.0 * farDepth * nearDepth) / (farDepth - nearDepth);\n"
    "  gl_Position = vec4(clipX, clipY, clipZ, depth);\n"
    "  uv = texCoord;\n"
    "  shade = vertexShade;\n"
    "  textureLayer = int(vertexTextureLayer);\n"
    "}\n";

static const char sTerrainFragmentShader[] =
    "#version 330 core\n"
    "in vec2 uv;\n"
    "in float shade;\n"
    "flat in int textureLayer;\n"
    "out vec4 color;\n"
    "uniform sampler2D image;\n"
    "uniform sampler2D baseImage;\n"
    "uniform sampler2D foregroundImage;\n"
    "uniform vec4 debugColor;\n"
    "void main() {\n"
    "  if (debugColor.a >= 0.0) { color = debugColor; return; }\n"
    "  vec4 texel = textureLayer == 1 ? texture(baseImage, uv)\n"
    "             : (textureLayer == 2 ? texture(foregroundImage, uv) : texture(image, uv));\n"
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
    sImageLocation = dglGetUniformLocation(sProgram, "image");
    sBaseImageLocation = dglGetUniformLocation(sProgram, "baseImage");
    sForegroundImageLocation = dglGetUniformLocation(sProgram, "foregroundImage");
    sDebugColorLocation = dglGetUniformLocation(sProgram, "debugColor");
    return sCameraLocation >= 0 && sCameraPitchLocation >= 0
        && sFocalLengthLocation >= 0 && sImageLocation >= 0
        && sBaseImageLocation >= 0 && sForegroundImageLocation >= 0
        && sDebugColorLocation >= 0;
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

static bool BuildInput(const struct DioramaSceneSnapshot *snapshot,
                        const struct DioramaResolvedCell *resolvedCells,
                       int chunkX, int chunkY,
                       struct DioramaTerrainChunkInput *input)
{
    int originX = chunkX * DIORAMA_TERRAIN_CHUNK_SIZE;
    int originY = chunkY * DIORAMA_TERRAIN_CHUNK_SIZE;
    int y;
    int x;
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
            int face;

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
            cell->shape = resolvedCells[gridIndex].shape;
            cell->profile = resolvedCells[gridIndex].profile;
            cell->planeAxis = resolvedCells[gridIndex].planeAxis;
            cell->structureId = resolvedCells[gridIndex].structureId;
            cell->structureX = resolvedCells[gridIndex].structureX;
            cell->structureY = resolvedCells[gridIndex].structureY;
            cell->structureWidth = resolvedCells[gridIndex].structureWidth;
            cell->structureHeight = resolvedCells[gridIndex].structureHeight;
            cell->structureRoofRows = resolvedCells[gridIndex].structureRoofRows;
            cell->structureLocalX = resolvedCells[gridIndex].structureLocalX;
            cell->structureLocalY = resolvedCells[gridIndex].structureLocalY;
            cell->groundHeight = resolvedCells[gridIndex].groundHeight;
            cell->visualHeight = resolvedCells[gridIndex].topHeight;
            cell->featureHeight = resolvedCells[gridIndex].featureHeight;
            cell->structureBodyHeight = resolvedCells[gridIndex].structureBodyHeight;
            cell->structureRoofHeight = resolvedCells[gridIndex].structureRoofHeight;
            for (face = 0; face < DIORAMA_MATERIAL_FACE_COUNT; face++)
            {
                uint16_t materialId = resolvedCells[gridIndex].materials[face].metatileId;
                struct DioramaAtlasUv uv;

                if (materialId == DIORAMA_MATERIAL_METATILE_SELF)
                    materialId = source->metatileId;
                uv = DioramaAtlas_GetUv(materialId);
                cell->materials[face].metatileId = materialId;
                cell->materials[face].layer = resolvedCells[gridIndex].materials[face].layer;
                cell->materials[face].u0 = uv.u0;
                cell->materials[face].v0 = uv.v0;
                cell->materials[face].u1 = uv.u1;
                cell->materials[face].v1 = uv.v1;
            }
            if (x >= DIORAMA_TERRAIN_HALO && x < DIORAMA_TERRAIN_HALO + DIORAMA_TERRAIN_CHUNK_SIZE
             && y >= DIORAMA_TERRAIN_HALO && y < DIORAMA_TERRAIN_HALO + DIORAMA_TERRAIN_CHUNK_SIZE)
                hasInterior = true;
        }
    }
    return hasInterior;
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

bool DioramaGLTerrain_Sync(const struct DioramaSceneSnapshot *snapshot)
{
    struct DioramaTerrainChunkInput input;
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

    sMetrics.rebuiltChunks = 0;
    if (snapshot->mapGeneration != sMapGeneration || rulesGeneration != sRulesGeneration)
    {
        DioramaGLTerrain_Reset();
        sMapGeneration = snapshot->mapGeneration;
        sRulesGeneration = rulesGeneration;
    }
    for (i = 0; i < TERRAIN_CHUNK_CACHE_SIZE; i++)
        sChunks[i].active = false;
    DioramaRules_ResolveGrid(snapshot, sResolvedCells);

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

            if (!BuildInput(snapshot, sResolvedCells, chunkX, chunkY, &input))
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
    }
    return count;
}

void DioramaGLTerrain_Draw(GLuint atlasTexture, GLuint baseAtlasTexture,
                           GLuint foregroundAtlasTexture, float cameraX, float cameraZ,
                           float cameraPitch, float focalLength, bool debug)
{
    uint32_t debugVertexCount = 0;
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
    dglUniform1i(sImageLocation, 0);
    dglUniform1i(sBaseImageLocation, 1);
    dglUniform1i(sForegroundImageLocation, 2);
    dglUniform4f(sDebugColorLocation, 0.0f, 0.0f, 0.0f, -1.0f);
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
        if (debug)
            debugVertexCount = AppendBoundsVertices(debugVertexCount, &chunk->mesh.bounds);
    }
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
    if (sProgram != 0)
        dglDeleteProgram(sProgram);
    memset(sChunks, 0, sizeof(sChunks));
    sDebugVertexBuffer = 0;
    sDebugVertexArray = 0;
    sProgram = 0;
}

#endif
