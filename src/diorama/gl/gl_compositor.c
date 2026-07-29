#ifdef ENABLE_DIORAMA

#include <stddef.h>
#include <SDL2/SDL.h>
#ifdef NATIVE_LINUX
#include <SDL2/SDL_image.h>
#endif

#include "diorama/gl_compositor.h"
#include "diorama/gl_loader.h"
#include "diorama/gl_terrain_renderer.h"
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
static u32 sAtlasMapGeneration;
static u32 sAtlasPaletteGeneration;
static u32 sAtlasAnimationGeneration;
static bool sHasSceneSnapshot;
static bool sTerrainAvailable;
static bool sTerrainDebug;
static enum DioramaRenderMode sRenderMode = DIORAMA_RENDER_AUTO;

#define RGB(r, g, b) (0xFF000000u | ((u32)(r) << 16) | ((u32)(g) << 8) | (u32)(b))
#define RGBA(r, g, b, a) (((u32)(a) << 24) | ((u32)(r) << 16) | ((u32)(g) << 8) | (u32)(b))

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
    case 'H': return GLYPH(5, 5, 7, 5, 5);
    case 'I': return GLYPH(7, 2, 2, 2, 7);
    case 'J': return GLYPH(1, 1, 1, 5, 2);
    case 'K': return GLYPH(5, 5, 6, 5, 5);
    case 'L': return GLYPH(4, 4, 4, 4, 7);
    case 'M': return GLYPH(5, 7, 7, 5, 5);
    case 'N': return GLYPH(5, 7, 7, 7, 5);
    case 'O': return GLYPH(2, 5, 5, 5, 2);
    case 'P': return GLYPH(6, 5, 6, 4, 4);
    case 'Q': return GLYPH(2, 5, 5, 7, 3);
    case 'R': return GLYPH(6, 5, 6, 5, 5);
    case 'S': return GLYPH(3, 4, 2, 1, 6);
    case 'T': return GLYPH(7, 2, 2, 2, 2);
    case 'U': return GLYPH(5, 5, 5, 5, 7);
    case 'V': return GLYPH(5, 5, 5, 5, 2);
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
    const struct DioramaTerrainMetrics *metrics = DioramaGLTerrain_GetMetrics();

    memset(sDebugPixels, 0, sizeof(sDebugPixels));
    FillRect(2, 2, 74, 66, RGBA(10, 14, 18, 220));
    DrawValue(5, 5, "GEN:", snapshot->mapGeneration);
    DrawValue(5, 14, "CH:", metrics->activeChunks);
    DrawValue(5, 23, "VIS:", metrics->visibleChunks);
    DrawValue(5, 32, "CUL:", metrics->culledChunks);
    DrawValue(5, 41, "REB:", metrics->rebuiltChunks);
    DrawValue(5, 50, "TRI:", metrics->triangles);
    DrawValue(5, 59, "DRA:", metrics->drawCalls);

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
    sTerrainAvailable = DioramaGLTerrain_Init();
    if (!sTerrainAvailable)
        SDL_Log("Diorama terrain renderer unavailable; retaining the 2D OpenGL fallback");

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
    sHasSceneSnapshot = false;
    sTerrainDebug = false;
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
    bool drawTerrain = false;

    if (DioramaSnapshotExchange_CopyLatest(&sSceneSnapshot))
    {
        sHasSceneSnapshot = true;
    }
    if (sRenderMode == DIORAMA_RENDER_AUTO
     && sTerrainAvailable
     && sHasSceneSnapshot
     && DioramaSnapshot_CanRenderFlatMap(&sSceneSnapshot))
    {
        EnsureAtlas(&sSceneSnapshot);
        drawTerrain = DioramaGLTerrain_Sync(&sSceneSnapshot);
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
    if (drawTerrain)
    {
        glEnable(GL_SCISSOR_TEST);
        glScissor(gameX, outputHeight - gameY - gameHeight, gameWidth, gameHeight);
        glClearColor(0.035f, 0.055f, 0.07f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glViewport(gameX, outputHeight - gameY - gameHeight, gameWidth, gameHeight);
        DioramaGLTerrain_Draw(&sSceneSnapshot, sAtlasTexture.id, sTerrainDebug);
        glDisable(GL_SCISSOR_TEST);
        glViewport(0, 0, outputWidth, outputHeight);
        dglUseProgram(sProgram);
        dglBindVertexArray(sVertexArray);
        if (sTerrainDebug)
        {
            BuildDebugImage(&sSceneSnapshot);
            DrawTexture(&sDebugTexture, outputWidth, outputHeight,
                        gameX, gameY, gameWidth, gameHeight,
                        0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, true);
        }
    }
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

void DioramaGL_ToggleTerrainDebug(void)
{
    sTerrainDebug = !sTerrainDebug;
}

void DioramaGL_Shutdown(void)
{
    DioramaGLTerrain_Shutdown();
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
