#ifdef ENABLE_DIORAMA

#include <stddef.h>
#include <SDL2/SDL.h>
#ifdef NATIVE_LINUX
#include <SDL2/SDL_image.h>
#endif

#include "diorama/gl_compositor.h"
#include "diorama/gl_loader.h"
#include "diorama/metatile_atlas.h"
#include "diorama/scene_snapshot.h"
#include "gba/defines.h"

#define MAX_BORDER_BACKGROUNDS 15

struct DioramaTexture
{
    GLuint id;
    int width;
    int height;
};

static SDL_Window *sWindow;
static SDL_GLContext sContext;
static GLuint sProgram;
static GLuint sVertexArray;
static GLuint sVertexBuffer;
static struct DioramaTexture sFrameTexture;
static struct DioramaTexture sDebugTexture;
static struct DioramaTexture sAtlasTexture;
static struct DioramaTexture sBackgroundTextures[MAX_BORDER_BACKGROUNDS];
static struct DioramaTexture sBorderTexture;
static u8 sBackgroundCount;
static struct DioramaSceneSnapshot sSceneSnapshot;
static u32 sDebugPixels[DISPLAY_WIDTH * DISPLAY_HEIGHT];
static u32 sAtlasPixels[DIORAMA_ATLAS_PIXEL_COUNT];
static u8 sPresentMetatiles[DIORAMA_ATLAS_PRESENT_BYTES];
static float sMapVertices[DIORAMA_MAX_VISIBLE_CELLS * 6 * 5];
static uint64_t sDebugSequence;
static u32 sAtlasMapGeneration;
static u32 sAtlasPaletteGeneration;
static u32 sAtlasAnimationGeneration;
static bool sHasSceneSnapshot;
static enum DioramaRenderMode sRenderMode = DIORAMA_RENDER_AUTO;

#define RGB(r, g, b) (0xFF000000u | ((u32)(r) << 16) | ((u32)(g) << 8) | (u32)(b))

static void FillRect(int x, int y, int width, int height, u32 color)
{
    int left = x < 0 ? 0 : x;
    int top = y < 0 ? 0 : y;
    int right = x + width > DISPLAY_WIDTH ? DISPLAY_WIDTH : x + width;
    int bottom = y + height > DISPLAY_HEIGHT ? DISPLAY_HEIGHT : y + height;
    int px;
    int py;

    for (py = top; py < bottom; py++)
        for (px = left; px < right; px++)
            sDebugPixels[py * DISPLAY_WIDTH + px] = color;
}

static u16 GetGlyph(char character)
{
#define GLYPH(a, b, c, d, e) ((a) << 12 | (b) << 9 | (c) << 6 | (d) << 3 | (e))
    switch (character)
    {
    case '0': return GLYPH(7, 5, 5, 5, 7);
    case '1': return GLYPH(2, 6, 2, 2, 7);
    case '2': return GLYPH(7, 1, 7, 4, 7);
    case '3': return GLYPH(7, 1, 7, 1, 7);
    case '4': return GLYPH(5, 5, 7, 1, 1);
    case '5': return GLYPH(7, 4, 7, 1, 7);
    case '6': return GLYPH(7, 4, 7, 5, 7);
    case '7': return GLYPH(7, 1, 2, 2, 2);
    case '8': return GLYPH(7, 5, 7, 5, 7);
    case '9': return GLYPH(7, 5, 7, 1, 7);
    case 'A': return GLYPH(2, 5, 7, 5, 5);
    case 'B': return GLYPH(6, 5, 6, 5, 6);
    case 'C': return GLYPH(3, 4, 4, 4, 3);
    case 'D': return GLYPH(6, 5, 5, 5, 6);
    case 'E': return GLYPH(7, 4, 6, 4, 7);
    case 'F': return GLYPH(7, 4, 6, 4, 4);
    case 'G': return GLYPH(3, 4, 5, 5, 3);
    case 'I': return GLYPH(7, 2, 2, 2, 7);
    case 'J': return GLYPH(1, 1, 1, 5, 2);
    case 'K': return GLYPH(5, 5, 6, 5, 5);
    case 'M': return GLYPH(5, 7, 7, 5, 5);
    case 'N': return GLYPH(5, 7, 7, 7, 5);
    case 'O': return GLYPH(2, 5, 5, 5, 2);
    case 'P': return GLYPH(6, 5, 6, 4, 4);
    case 'Q': return GLYPH(2, 5, 5, 7, 3);
    case 'R': return GLYPH(6, 5, 6, 5, 5);
    case 'S': return GLYPH(3, 4, 2, 1, 6);
    case 'T': return GLYPH(7, 2, 2, 2, 2);
    case 'U': return GLYPH(5, 5, 5, 5, 7);
    case '-': return GLYPH(0, 0, 7, 0, 0);
    case ':': return GLYPH(0, 2, 0, 2, 0);
    case ',': return GLYPH(0, 0, 0, 2, 4);
    default: return 0;
    }
#undef GLYPH
}

static void DrawText(int x, int y, const char *text, u32 color)
{
    for (; *text != '\0'; text++, x += 4)
    {
        u16 glyph = GetGlyph(*text);
        int row;
        int column;

        for (row = 0; row < 5; row++)
            for (column = 0; column < 3; column++)
                if (glyph & (1 << ((4 - row) * 3 + (2 - column))))
                    FillRect(x + column, y + row, 1, 1, color);
    }
}

static void DrawValue(int x, int y, const char *label, int value)
{
    char text[24];

    SDL_snprintf(text, sizeof(text), "%s%d", label, value);
    DrawText(x, y, text, RGB(220, 230, 235));
}

static void BuildDebugImage(const struct DioramaSceneSnapshot *snapshot)
{
    int i;

    FillRect(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, RGB(10, 14, 18));
    for (i = 0; i < snapshot->visibleCellCount; i++)
    {
        const struct DioramaCellSnapshot *cell = &snapshot->cells[i];
        int column = i % DIORAMA_GRID_WIDTH;
        int row = i / DIORAMA_GRID_WIDTH;
        int x0 = 2 + column * 156 / DIORAMA_GRID_WIDTH;
        int y0 = 2 + row * 156 / DIORAMA_GRID_HEIGHT;
        int x1 = 2 + (column + 1) * 156 / DIORAMA_GRID_WIDTH;
        int y1 = 2 + (row + 1) * 156 / DIORAMA_GRID_HEIGHT;
        u8 red = 35 + (cell->behavior * 29 + cell->collision * 70) % 170;
        u8 green = 35 + (cell->metatileId * 13) % 170;
        u8 blue = 35 + (cell->elevation * 17 + cell->layerType * 31) % 170;

        FillRect(x0, y0, x1 - x0 - 1, y1 - y0 - 1, RGB(red, green, blue));
    }

    FillRect(2 + 16 * 156 / 33, 2 + 16 * 156 / 33, 5, 1, RGB(255, 240, 80));
    FillRect(2 + 16 * 156 / 33, 2 + 16 * 156 / 33, 1, 5, RGB(255, 240, 80));
    for (i = 0; i < snapshot->objectCount; i++)
    {
        const struct DioramaObjectSnapshot *object = &snapshot->objects[i];
        int column = object->currentMapX - snapshot->gridOriginX;
        int row = object->currentMapY - snapshot->gridOriginY;

        if (column >= 0 && column < DIORAMA_GRID_WIDTH && row >= 0 && row < DIORAMA_GRID_HEIGHT)
            FillRect(2 + column * 156 / 33, 2 + row * 156 / 33, 3, 3,
                     object->flags & 1 ? RGB(255, 255, 255) : RGB(255, 70, 210));
    }

    DrawText(164, 4, "AUTO", RGB(100, 230, 170));
    DrawValue(164, 13, "SEQ:", snapshot->sequence % 1000000);
    DrawValue(164, 22, "GEN:", snapshot->mapGeneration);
    DrawValue(164, 31, "MAP:", snapshot->mapGroup);
    DrawValue(164, 40, "NUM:", snapshot->mapNum);
    DrawValue(164, 49, "CAM:", snapshot->cameraMapX);
    DrawValue(164, 58, "   :", snapshot->cameraMapY);
    DrawValue(164, 67, "OBJ:", snapshot->objectCount);
    DrawValue(164, 76, "KIND:", snapshot->sceneKind);
    DrawValue(164, 85, "PAL:", snapshot->paletteGeneration);

    glBindTexture(GL_TEXTURE_2D, sDebugTexture.id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT,
                    GL_BGRA, GL_UNSIGNED_BYTE, sDebugPixels);
}

static const char sVertexShaderSource[] =
    "#version 330 core\n"
    "layout(location = 0) in vec3 position;\n"
    "layout(location = 1) in vec2 texCoord;\n"
    "out vec2 uv;\n"
    "void main() { gl_Position = vec4(position.xy * position.z, 0.0, position.z); uv = texCoord; }\n";

static const char sFragmentShaderSource[] =
    "#version 330 core\n"
    "in vec2 uv;\n"
    "out vec4 color;\n"
    "uniform sampler2D image;\n"
    "void main() { color = texture(image, uv); }\n";

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
        SDL_Log("Diorama shader compile failed: %s", log);
        dglDeleteShader(shader);
        return 0;
    }
    return shader;
}

static bool CreateProgram(void)
{
    GLuint vertexShader = CompileShader(GL_VERTEX_SHADER, sVertexShaderSource);
    GLuint fragmentShader = CompileShader(GL_FRAGMENT_SHADER, sFragmentShaderSource);
    GLint linked;

    if (vertexShader == 0 || fragmentShader == 0)
    {
        if (vertexShader != 0)
            dglDeleteShader(vertexShader);
        if (fragmentShader != 0)
            dglDeleteShader(fragmentShader);
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
        SDL_Log("Diorama shader link failed: %s", log);
        return false;
    }
    return true;
}

static void ConfigureTexture(GLuint texture)
{
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

static struct DioramaTexture LoadTexture(const char *path)
{
    struct DioramaTexture texture = {0};
#ifdef NATIVE_LINUX
    SDL_Surface *loaded = IMG_Load(path);
#else
    SDL_Surface *loaded = SDL_LoadBMP(path);
#endif
    SDL_Surface *rgba;

    if (loaded == NULL)
    {
        SDL_Log("Diorama artwork could not be loaded (%s): %s", path, SDL_GetError());
        return texture;
    }
    rgba = SDL_ConvertSurfaceFormat(loaded, SDL_PIXELFORMAT_ABGR8888, 0);
    SDL_FreeSurface(loaded);
    if (rgba == NULL)
    {
        SDL_Log("Diorama artwork conversion failed (%s): %s", path, SDL_GetError());
        return texture;
    }

    texture.width = rgba->w;
    texture.height = rgba->h;
    glGenTextures(1, &texture.id);
    ConfigureTexture(texture.id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, texture.width, texture.height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, rgba->pixels);
    SDL_FreeSurface(rgba);
    return texture;
}

static void DrawTexture(const struct DioramaTexture *texture,
                        int outputWidth, int outputHeight,
                        float x, float y, float width, float height,
                        float sourceX, float sourceY, float sourceWidth, float sourceHeight,
                        bool blend)
{
    float left = x * 2.0f / outputWidth - 1.0f;
    float right = (x + width) * 2.0f / outputWidth - 1.0f;
    float top = 1.0f - y * 2.0f / outputHeight;
    float bottom = 1.0f - (y + height) * 2.0f / outputHeight;
    float u0 = sourceX / texture->width;
    float v0 = sourceY / texture->height;
    float u1 = (sourceX + sourceWidth) / texture->width;
    float v1 = (sourceY + sourceHeight) / texture->height;
    const float vertices[] = {
        left,  top,    1.0f, u0, v0,
        right, top,    1.0f, u1, v0,
        right, bottom, 1.0f, u1, v1,
        left,  top,    1.0f, u0, v0,
        right, bottom, 1.0f, u1, v1,
        left,  bottom, 1.0f, u0, v1,
    };

    if (texture->id == 0 || outputWidth <= 0 || outputHeight <= 0)
        return;
    if (blend)
    {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }
    else
    {
        glDisable(GL_BLEND);
    }
    glBindTexture(GL_TEXTURE_2D, texture->id);
    dglBindBuffer(GL_ARRAY_BUFFER, sVertexBuffer);
    dglBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

static void EnsureAtlas(const struct DioramaSceneSnapshot *snapshot)
{
    bool reset = snapshot->mapGeneration != sAtlasMapGeneration
              || snapshot->paletteGeneration != sAtlasPaletteGeneration
              || snapshot->tilesetAnimationGeneration != sAtlasAnimationGeneration;
    bool changed;

    if (reset)
    {
        DioramaAtlas_Clear(sAtlasPixels, sPresentMetatiles);
        sAtlasMapGeneration = snapshot->mapGeneration;
        sAtlasPaletteGeneration = snapshot->paletteGeneration;
        sAtlasAnimationGeneration = snapshot->tilesetAnimationGeneration;
    }
    changed = DioramaAtlas_Update(snapshot, sAtlasPixels, sPresentMetatiles);
    if (reset || changed)
    {
        glBindTexture(GL_TEXTURE_2D, sAtlasTexture.id);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, DIORAMA_ATLAS_WIDTH, DIORAMA_ATLAS_HEIGHT,
                        GL_BGRA, GL_UNSIGNED_BYTE, sAtlasPixels);
    }
}

static void ProjectMapPoint(float worldX, float worldZ,
                            int outputWidth, int outputHeight,
                            int gameX, int gameY, int gameWidth, int gameHeight,
                            float *screenX, float *screenY, float *screenW)
{
    const float cameraHeight = 16.0f;
    const float cameraDistance = 18.0f;
    const float pitchSin = 0.65f;
    const float pitchCos = 0.759934f;
    const float focalLength = 130.0f;
    float depth = cameraHeight * pitchSin + (worldZ + cameraDistance) * pitchCos;
    float vertical = -cameraHeight * pitchCos + (worldZ + cameraDistance) * pitchSin;
    float virtualX = DISPLAY_WIDTH * 0.5f + worldX * focalLength / depth;
    float virtualY = DISPLAY_HEIGHT * 0.52f - vertical * focalLength / depth;
    float outputX = gameX + virtualX * gameWidth / DISPLAY_WIDTH;
    float outputY = gameY + virtualY * gameHeight / DISPLAY_HEIGHT;

    *screenX = outputX * 2.0f / outputWidth - 1.0f;
    *screenY = 1.0f - outputY * 2.0f / outputHeight;
    *screenW = depth;
}

static int AppendMapVertex(float *vertices, int vertexCount,
                           float x, float y, float w, float u, float v)
{
    vertices[vertexCount * 5] = x;
    vertices[vertexCount * 5 + 1] = y;
    vertices[vertexCount * 5 + 2] = w;
    vertices[vertexCount * 5 + 3] = u;
    vertices[vertexCount * 5 + 4] = v;
    return vertexCount + 1;
}

static void DrawFlatMap(const struct DioramaSceneSnapshot *snapshot,
                        int outputWidth, int outputHeight,
                        int gameX, int gameY, int gameWidth, int gameHeight)
{
    int vertexCount = 0;
    unsigned i;

    for (i = 0; i < snapshot->visibleCellCount; i++)
    {
        const struct DioramaCellSnapshot *cell = &snapshot->cells[i];
        struct DioramaAtlasUv uv;
        float x0;
        float y0;
        float x1;
        float y1;
        float w1;
        float x2;
        float y2;
        float w2;
        float x3;
        float y3;
        float w3;
        float w0;
        float worldLeft;
        float worldRight;
        float worldTop;
        float worldBottom;

        if (cell->metatileId >= DIORAMA_TILE_COUNT)
            continue;
        worldLeft = cell->mapX - snapshot->cameraMapX
                  - (snapshot->cameraSubpixelX + snapshot->cameraPanX) / 16.0f - 0.5f;
        worldRight = worldLeft + 1.0f;
        worldTop = snapshot->cameraMapY
                 + (snapshot->cameraSubpixelY + snapshot->cameraPanY) / 16.0f
                 - cell->mapY + 0.5f;
        worldBottom = worldTop - 1.0f;
        ProjectMapPoint(worldLeft, worldTop, outputWidth, outputHeight,
                        gameX, gameY, gameWidth, gameHeight, &x0, &y0, &w0);
        ProjectMapPoint(worldRight, worldTop, outputWidth, outputHeight,
                        gameX, gameY, gameWidth, gameHeight, &x1, &y1, &w1);
        ProjectMapPoint(worldRight, worldBottom, outputWidth, outputHeight,
                        gameX, gameY, gameWidth, gameHeight, &x2, &y2, &w2);
        ProjectMapPoint(worldLeft, worldBottom, outputWidth, outputHeight,
                        gameX, gameY, gameWidth, gameHeight, &x3, &y3, &w3);
        uv = DioramaAtlas_GetUv(cell->metatileId);

        vertexCount = AppendMapVertex(sMapVertices, vertexCount, x0, y0, w0, uv.u0, uv.v0);
        vertexCount = AppendMapVertex(sMapVertices, vertexCount, x1, y1, w1, uv.u1, uv.v0);
        vertexCount = AppendMapVertex(sMapVertices, vertexCount, x2, y2, w2, uv.u1, uv.v1);
        vertexCount = AppendMapVertex(sMapVertices, vertexCount, x0, y0, w0, uv.u0, uv.v0);
        vertexCount = AppendMapVertex(sMapVertices, vertexCount, x2, y2, w2, uv.u1, uv.v1);
        vertexCount = AppendMapVertex(sMapVertices, vertexCount, x3, y3, w3, uv.u0, uv.v1);
    }

    glEnable(GL_SCISSOR_TEST);
    glScissor(gameX, outputHeight - gameY - gameHeight, gameWidth, gameHeight);
    glClearColor(0.035f, 0.055f, 0.07f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBindTexture(GL_TEXTURE_2D, sAtlasTexture.id);
    dglBindBuffer(GL_ARRAY_BUFFER, sVertexBuffer);
    dglBufferData(GL_ARRAY_BUFFER, vertexCount * 5 * sizeof(float), sMapVertices, GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, vertexCount);
    glDisable(GL_SCISSOR_TEST);
}

bool DioramaGL_Init(SDL_Window *window, u8 backgroundCount)
{
    sWindow = window;
    sBackgroundCount = backgroundCount;
    sContext = SDL_GL_CreateContext(window);
    if (sContext == NULL)
    {
        SDL_Log("OpenGL context could not be created: %s", SDL_GetError());
        return false;
    }
    if (!DioramaGL_LoadFunctions() || !CreateProgram())
        return false;

    dglGenVertexArrays(1, &sVertexArray);
    dglBindVertexArray(sVertexArray);
    dglGenBuffers(1, &sVertexBuffer);
    dglBindBuffer(GL_ARRAY_BUFFER, sVertexBuffer);
    dglEnableVertexAttribArray(0);
    dglVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), NULL);
    dglEnableVertexAttribArray(1);
    dglVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void *)(3 * sizeof(float)));
    dglUseProgram(sProgram);
    dglActiveTexture(GL_TEXTURE0);

    glGenTextures(1, &sFrameTexture.id);
    sFrameTexture.width = DISPLAY_WIDTH;
    sFrameTexture.height = DISPLAY_HEIGHT;
    ConfigureTexture(sFrameTexture.id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, DISPLAY_WIDTH, DISPLAY_HEIGHT, 0,
                 GL_BGRA, GL_UNSIGNED_BYTE, NULL);
    glGenTextures(1, &sDebugTexture.id);
    sDebugTexture.width = DISPLAY_WIDTH;
    sDebugTexture.height = DISPLAY_HEIGHT;
    ConfigureTexture(sDebugTexture.id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, DISPLAY_WIDTH, DISPLAY_HEIGHT, 0,
                  GL_BGRA, GL_UNSIGNED_BYTE, NULL);
    glGenTextures(1, &sAtlasTexture.id);
    sAtlasTexture.width = DIORAMA_ATLAS_WIDTH;
    sAtlasTexture.height = DIORAMA_ATLAS_HEIGHT;
    ConfigureTexture(sAtlasTexture.id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, DIORAMA_ATLAS_WIDTH, DIORAMA_ATLAS_HEIGHT, 0,
                 GL_BGRA, GL_UNSIGNED_BYTE, NULL);
    DioramaAtlas_Clear(sAtlasPixels, sPresentMetatiles);
    sAtlasMapGeneration = 0;
    sAtlasPaletteGeneration = 0;
    sAtlasAnimationGeneration = 0;
    sDebugSequence = 0;
    sHasSceneSnapshot = false;
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    return true;
}

void DioramaGL_LoadArtwork(void)
{
    for (int i = 0; i < sBackgroundCount; i++)
    {
        char filename[16];
#ifdef NATIVE_LINUX
        SDL_snprintf(filename, sizeof(filename), i == 0 ? "BG.png" : "BG%d.png", i);
#else
        SDL_snprintf(filename, sizeof(filename), i == 0 ? "BG.bmp" : "BG%d.bmp", i);
#endif
        sBackgroundTextures[i] = LoadTexture(filename);
    }
#ifdef NATIVE_LINUX
    sBorderTexture = LoadTexture("Border.png");
#else
    sBorderTexture = LoadTexture("Border.bmp");
#endif
}

void DioramaGL_UploadFrame(const u32 *argb8888)
{
    glBindTexture(GL_TEXTURE_2D, sFrameTexture.id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT,
                    GL_BGRA, GL_UNSIGNED_BYTE, argb8888);
}

void DioramaGL_Present(u8 background, bool border, bool integerScale)
{
    int outputWidth;
    int outputHeight;
    int gameWidth;
    int gameHeight;
    int gameX;
    int gameY;
    const struct DioramaTexture *gameTexture = &sFrameTexture;
    bool drawFlatMap = false;

    if (DioramaSnapshotExchange_CopyLatest(&sSceneSnapshot))
    {
        sHasSceneSnapshot = true;
        if (sSceneSnapshot.sequence != sDebugSequence && DioramaSnapshot_CanRenderGrid(&sSceneSnapshot))
        {
            BuildDebugImage(&sSceneSnapshot);
            sDebugSequence = sSceneSnapshot.sequence;
        }
    }
    if (sRenderMode == DIORAMA_RENDER_AUTO
     && sHasSceneSnapshot
     && DioramaSnapshot_CanRenderFlatMap(&sSceneSnapshot))
    {
        EnsureAtlas(&sSceneSnapshot);
        drawFlatMap = true;
    }
    else if (sRenderMode == DIORAMA_RENDER_DEBUG
          && sHasSceneSnapshot
          && DioramaSnapshot_CanRenderGrid(&sSceneSnapshot))
        gameTexture = &sDebugTexture;

    SDL_GL_GetDrawableSize(sWindow, &outputWidth, &outputHeight);
    if (outputWidth <= 0 || outputHeight <= 0)
        return;
    glViewport(0, 0, outputWidth, outputHeight);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    dglUseProgram(sProgram);
    dglBindVertexArray(sVertexArray);

    if (background < sBackgroundCount && sBackgroundTextures[background].id != 0)
        DrawTexture(&sBackgroundTextures[background], outputWidth, outputHeight,
                    0, 0, outputWidth, outputHeight,
                    0, 0, sBackgroundTextures[background].width,
                    sBackgroundTextures[background].height, true);

    if (integerScale)
    {
        int scale = outputWidth / DISPLAY_WIDTH;
        if (outputHeight / DISPLAY_HEIGHT < scale)
            scale = outputHeight / DISPLAY_HEIGHT;
        if (scale < 1)
            scale = 1;
        gameWidth = DISPLAY_WIDTH * scale;
        gameHeight = DISPLAY_HEIGHT * scale;
    }
    else
    {
        gameHeight = outputHeight * 8 / 9;
        gameWidth = gameHeight * 3 / 2;
    }
    gameX = (outputWidth - gameWidth) / 2;
    gameY = (outputHeight - gameHeight) / 2;
    if (drawFlatMap)
        DrawFlatMap(&sSceneSnapshot, outputWidth, outputHeight,
                    gameX, gameY, gameWidth, gameHeight);
    else
        DrawTexture(gameTexture, outputWidth, outputHeight,
                    gameX, gameY, gameWidth, gameHeight,
                    0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, false);

    if (border && sBorderTexture.id != 0)
    {
        int innerWidth = gameWidth - 2;
        int innerHeight = gameHeight - 2;
        DrawTexture(&sBorderTexture, outputWidth, outputHeight,
                    gameX + 1 - innerWidth * 19 / 961,
                    gameY + 1 - innerHeight * 20 / 643,
                    innerWidth * 1000 / 961,
                    innerHeight * 683 / 643,
                    141, 18, 1000, 683, true);
    }
    SDL_GL_SwapWindow(sWindow);
}

void DioramaGL_SetVSync(bool enabled)
{
    if (SDL_GL_SetSwapInterval(enabled ? 1 : 0) < 0)
        SDL_Log("OpenGL VSync could not be changed: %s", SDL_GetError());
}

void DioramaGL_Shutdown(void)
{
    for (int i = 0; i < sBackgroundCount; i++)
        if (sBackgroundTextures[i].id != 0)
            glDeleteTextures(1, &sBackgroundTextures[i].id);
    if (sBorderTexture.id != 0)
        glDeleteTextures(1, &sBorderTexture.id);
    if (sFrameTexture.id != 0)
        glDeleteTextures(1, &sFrameTexture.id);
    if (sDebugTexture.id != 0)
        glDeleteTextures(1, &sDebugTexture.id);
    if (sAtlasTexture.id != 0)
        glDeleteTextures(1, &sAtlasTexture.id);
    if (sVertexBuffer != 0)
        dglDeleteBuffers(1, &sVertexBuffer);
    if (sVertexArray != 0)
        dglDeleteVertexArrays(1, &sVertexArray);
    if (sProgram != 0)
        dglDeleteProgram(sProgram);
    if (sContext != NULL)
        SDL_GL_DeleteContext(sContext);
    sContext = NULL;
}

#endif
