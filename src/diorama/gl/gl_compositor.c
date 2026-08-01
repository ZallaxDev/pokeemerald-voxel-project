#ifdef ENABLE_DIORAMA

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL2/SDL.h>
#ifdef NATIVE_LINUX
#include <SDL2/SDL_image.h>
#endif

#include "diorama/gl_compositor.h"
#include "diorama/gl_loader.h"
#include "diorama/gl_object_renderer.h"
#include "diorama/gl_terrain_renderer.h"
#include "diorama/rules.h"
#include "diorama/metatile_atlas.h"
#include "diorama/presentation_transition.h"
#include "diorama/scene_snapshot.h"
#include "diorama/weather.h"
#include "gba/defines.h"

#define MAX_BORDER_BACKGROUNDS 15
#define CAMERA_DEFAULT_PITCH 0.70758444f
#define CAMERA_MIN_PITCH 0.26179939f
#define CAMERA_MAX_PITCH 1.30899694f
#define CAMERA_PITCH_STEP 0.08726646f
#define CAMERA_DEFAULT_FOCAL_LENGTH 130.0f
#define CAMERA_MIN_FOCAL_LENGTH 80.0f
#define CAMERA_MAX_FOCAL_LENGTH 600.0f
#define CAMERA_ZOOM_STEP 10.0f
#define SOURCE_FADE_SECONDS 0.12
#define CAMERA_TELEPORT_LIMIT 2.0f
#define UI_TRANSIENT_HOLD_FRAMES 3

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
static GLint sOpacityUniform;
static struct DioramaTexture sFrameTexture;
static struct DioramaTexture sUiTexture;
static struct DioramaTexture sDebugTexture;
static struct DioramaTexture sWeatherTexture;
static struct DioramaTexture sAtlasTexture;
static struct DioramaTexture sBaseAtlasTexture;
static struct DioramaTexture sForegroundAtlasTexture;
static struct DioramaTexture sBackgroundTextures[MAX_BORDER_BACKGROUNDS];
static struct DioramaTexture sBorderTexture;
static u8 sBackgroundCount;
static struct DioramaSceneSnapshot sSceneSnapshot;
static struct DioramaSceneSnapshot sPreviousSceneSnapshot;
static struct DioramaSceneSnapshot sIncomingSceneSnapshot;
static struct DioramaSceneSnapshot sRenderedSceneSnapshot;
static struct DioramaSceneSnapshot sPreviousRenderedSceneSnapshot;
static u32 sDebugPixels[DISPLAY_WIDTH * DISPLAY_HEIGHT];
static u32 sWeatherPixels[DISPLAY_WIDTH * DISPLAY_HEIGHT];
static u32 sUiPixels[DISPLAY_WIDTH * DISPLAY_HEIGHT];
static u32 sAtlasPixels[DIORAMA_ATLAS_PIXEL_COUNT];
static u32 sBaseAtlasPixels[DIORAMA_ATLAS_PIXEL_COUNT];
static u32 sForegroundAtlasPixels[DIORAMA_ATLAS_PIXEL_COUNT];
static struct DioramaResolvedCell sAtlasResolvedCells[DIORAMA_MAX_VISIBLE_CELLS];
static u16 sCutoutBaseMetatiles[DIORAMA_TILE_COUNT];
static u16 sPreviousCutoutBaseMetatiles[DIORAMA_TILE_COUNT];
static u8 sPresentMetatiles[DIORAMA_ATLAS_PRESENT_BYTES];
static u8 sDirtyAtlasTiles[DIORAMA_ATLAS_DIRTY_TILE_BYTES];
static u8 sForcedMetatiles[DIORAMA_ATLAS_PRESENT_BYTES];
static u8 sVisibleMetatiles[DIORAMA_ATLAS_PRESENT_BYTES];
static u8 sUpdatedMetatiles[DIORAMA_ATLAS_PRESENT_BYTES];
static u8 sAtlasTileGraphics[DIORAMA_TILE_GRAPHICS_SIZE];
static u32 sAtlasMapGeneration;
static u32 sAtlasPaletteGeneration;
static u32 sAtlasAnimationGeneration;
static bool sHasAtlasTileGraphics;
static bool sHasSceneSnapshot;
static bool sHasPreviousSceneSnapshot;
static bool sHasRenderedSceneSnapshot;
static bool sHasPreviousRenderedSceneSnapshot;
static bool sCurrent3DReady;
static bool sTerrainAvailable;
static bool sObjectsAvailable;
static bool sTerrainDebug;
static float sCameraPitchOffset;
static float sCameraFocalLengthOffset;
static int sSurveyView = -1;
static bool sSurveyCaptureRequested;
static bool sSurveySavedFullscreen;
static bool sSurveySavedTerrainDebug;
static int sSurveySavedWindowWidth;
static int sSurveySavedWindowHeight;
static int sSurveySavedWindowX;
static int sSurveySavedWindowY;
static unsigned sSurveyRun;
static float sSurveySavedFocalLengthOffset;
static enum DioramaRenderMode sSurveySavedRenderMode;
static enum DioramaRenderMode sRenderMode = DIORAMA_RENDER_AUTO;
static uint64_t sProcessedSequence;
static uint64_t sFadeStartCounter;
static float sTwoDOpacity = 1.0f;
static float sFadeStartOpacity = 1.0f;
static float sFadeTargetOpacity = 1.0f;

static const char *SurveyFacingName(uint8_t direction)
{
    switch (direction)
    {
    case 1: return "south";
    case 2: return "north";
    case 3: return "west";
    case 4: return "east";
    default: return "unknown";
    }
}

static void SurveyImagePath(char *path, size_t size,
                            const struct DioramaSceneSnapshot *snapshot,
                            unsigned run, const char *view)
{
    snprintf(path, size, "pokeemerald-survey-%u-%u_%d_%d_%s_run-%u_%s.bmp",
             snapshot->mapGroup, snapshot->mapNum, snapshot->playerMapX,
             snapshot->playerMapY, SurveyFacingName(snapshot->playerFacingDirection),
             run, view);
}

static unsigned NextSurveyRun(const struct DioramaSceneSnapshot *snapshot)
{
    static const char *const sNames[] = {"flat", "v15", "v35", "v50", "v75"};
    unsigned run = 1;

    for (;; run++)
    {
        int view;
        bool available = true;

        for (view = 0; view < 5; view++)
        {
            char path[256];
            FILE *file;

            SurveyImagePath(path, sizeof(path), snapshot, run, sNames[view]);
            file = fopen(path, "rb");
            if (file != NULL)
            {
                fclose(file);
                available = false;
                break;
            }
        }
        if (available)
            return run;
    }
}

static void CaptureSurveyFrame(int width, int height,
                               const struct DioramaSceneSnapshot *snapshot,
                               bool stateMatchesPixels)
{
    static const char *const sNames[] = {"flat", "v15", "v35", "v50", "v75"};
    const char *facing;
    char imagePath[256];
    char metadataPath[272];
    uint8_t *pixels;
    uint8_t *flipped;
    SDL_Surface *surface;
    FILE *metadata;
    size_t stride;
    int y;

    sSurveyCaptureRequested = false;
    if (sSurveyView < 0 || !sHasSceneSnapshot || !stateMatchesPixels
     || snapshot->sceneKind != DIORAMA_SCENE_OVERWORLD_FREE
     || snapshot->fallbackReasons != 0 || width != 960 || height != 640)
    {
        SDL_Log("Diorama survey capture rejected: state or framebuffer is not stable");
        return;
    }
    facing = SurveyFacingName(snapshot->playerFacingDirection);
    SurveyImagePath(imagePath, sizeof(imagePath), snapshot, sSurveyRun, sNames[sSurveyView]);
    snprintf(metadataPath, sizeof(metadataPath), "%s.json", imagePath);
    stride = (size_t)width * 4;
    pixels = malloc(stride * height);
    flipped = malloc(stride * height);
    if (pixels == NULL || flipped == NULL)
    {
        SDL_Log("Diorama survey capture: out of memory");
        free(pixels);
        free(flipped);
        return;
    }
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    for (y = 0; y < height; y++)
        memcpy(flipped + (size_t)y * stride,
               pixels + (size_t)(height - 1 - y) * stride, stride);
    surface = SDL_CreateRGBSurfaceFrom(flipped, width, height, 32, (int)stride,
                                       0x000000FF, 0x0000FF00, 0x00FF0000, 0xFF000000);
    if (surface == NULL || SDL_SaveBMP(surface, imagePath) != 0)
        SDL_Log("Diorama survey capture failed: %s", SDL_GetError());
    else
    {
        metadata = fopen(metadataPath, "w");
        if (metadata != NULL)
        {
            fprintf(metadata,
                    "{\"schemaVersion\":1,\"mapGroup\":%u,\"mapNum\":%u,"
                    "\"layoutId\":%u,\"x\":%d,\"y\":%d,\"facing\":\"%s\","
                    "\"view\":\"%s\",\"width\":%d,\"height\":%d,"
                    "\"captureRun\":%u,\"sceneKind\":%u,\"fallbackReasons\":%u,"
                    "\"snapshotSequence\":%llu,\"mapGeneration\":%u,"
                    "\"mapEditGeneration\":%u,\"paletteGeneration\":%u,"
                    "\"objPaletteGeneration\":%u,\"animationGeneration\":%u}\n",
                    snapshot->mapGroup, snapshot->mapNum, snapshot->mapLayoutId,
                    snapshot->playerMapX, snapshot->playerMapY, facing, sNames[sSurveyView],
                    width, height, sSurveyRun, snapshot->sceneKind, snapshot->fallbackReasons,
                    (unsigned long long)snapshot->sequence,
                    snapshot->mapGeneration, snapshot->mapEditGeneration,
                    snapshot->paletteGeneration, snapshot->objPaletteGeneration,
                    snapshot->tilesetAnimationGeneration);
            fclose(metadata);
            SDL_Log("Diorama survey capture: %s", imagePath);
        }
        else
            SDL_Log("Diorama survey metadata could not be written");
    }
    if (surface != NULL)
        SDL_FreeSurface(surface);
    free(pixels);
    free(flipped);
}
static u8 sUiTransientHoldFrames;

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
    const struct DioramaObjectMetrics *objectMetrics = DioramaGLObjects_GetMetrics();
    struct DioramaMapProfile profile;

    memset(sDebugPixels, 0, sizeof(sDebugPixels));
    FillRect(2, 2, 74, 102, RGBA(10, 14, 18, 220));
    DrawValue(5, 5, "GEN:", snapshot->mapGeneration);
    DrawValue(5, 14, "CH:", metrics->activeChunks);
    DrawValue(5, 23, "VIS:", metrics->visibleChunks);
    DrawValue(5, 32, "CUL:", metrics->culledChunks);
    DrawValue(5, 41, "REB:", metrics->rebuiltChunks);
    DrawValue(5, 50, "TRI:", metrics->triangles);
    DrawValue(5, 59, "DRA:", metrics->drawCalls);
    DrawValue(5, 68, "OBJ:", objectMetrics->visibleObjects);
    DrawValue(5, 77, "CAC:", objectMetrics->cachedFrames);
    DrawValue(5, 86, "UPL:", objectMetrics->uploadedFrames);
    if (DioramaRules_GetMapProfile(snapshot->mapGroup, snapshot->mapNum,
                                   snapshot->mapLayoutId, &profile))
        DrawText(5, 95, profile.cameraProfile == DIORAMA_CAMERA_INTERIOR
                           ? "CAM:I" : "CAM:E", RGB(220, 230, 235));
    else
        DrawText(5, 95, "CAM:-", RGB(220, 230, 235));

    glBindTexture(GL_TEXTURE_2D, sDebugTexture.id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT,
                    GL_BGRA, GL_UNSIGNED_BYTE, sDebugPixels);
}

static bool BuildWeatherImage(const struct DioramaSceneSnapshot *snapshot)
{
    enum DioramaWeatherEffect effect = DioramaWeather_Classify(snapshot->weather);
    uint32_t phase = (uint32_t)(SDL_GetPerformanceCounter() * 60
                             / SDL_GetPerformanceFrequency());
    int i;

    memset(sWeatherPixels, 0, sizeof(sWeatherPixels));
    if (effect == DIORAMA_WEATHER_RAIN)
    {
        int dropCount = DioramaWeather_RainDropCount(snapshot->rainVisibleCount);
        for (i = 0; i < dropCount; i++)
        {
            int cycle = DISPLAY_WIDTH + 16;
            int x = (i * 73 - (int)(phase * 3 % cycle)) % cycle;
            int y = (i * 47 + phase * 7) % (DISPLAY_HEIGHT + 24) - 12;
            int length;

            if (x < 0)
                x += cycle;
            x -= 8;
            for (length = 0; length < 7; length++)
            {
                int px = x - length / 2;
                int py = y + length;

                if (px >= 0 && px < DISPLAY_WIDTH && py >= 0 && py < DISPLAY_HEIGHT)
                    sWeatherPixels[py * DISPLAY_WIDTH + px] = RGBA(185, 215, 235, 150);
            }
        }
    }
    else if (effect == DIORAMA_WEATHER_ASH)
    {
        for (i = 0; i < 24; i++)
        {
            int cycleX = DISPLAY_WIDTH + 12;
            int x = (i * 67 + phase * 2) % cycleX - 6;
            int y = (i * 41 + phase * 2) % (DISPLAY_HEIGHT + 12) - 6;
            int size = (i % 3 == 0) ? 2 : 1;
            int px;
            int py;

            for (py = y; py < y + size; py++)
                for (px = x; px < x + size; px++)
                    if (px >= 0 && px < DISPLAY_WIDTH
                     && py >= 0 && py < DISPLAY_HEIGHT)
                        sWeatherPixels[py * DISPLAY_WIDTH + px]
                            = RGBA(205, 198, 190, 190);
        }
    }
    else if (effect == DIORAMA_WEATHER_FOG)
    {
        int baseAlpha = DioramaWeather_FogAlpha(snapshot->weatherBlendEVA);
        int y;
        int x;

        for (y = 0; y < DISPLAY_HEIGHT; y++)
        {
            int band = (y + snapshot->fogScrollOffset / 8) % 48;
            int modulation = band < 24 ? band : 48 - band;
            int alpha = baseAlpha + modulation * baseAlpha / 96;

            for (x = 0; x < DISPLAY_WIDTH; x++)
                sWeatherPixels[y * DISPLAY_WIDTH + x] = RGBA(210, 220, 222, alpha);
        }
    }
    else
    {
        return false;
    }

    glBindTexture(GL_TEXTURE_2D, sWeatherTexture.id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT,
                    GL_BGRA, GL_UNSIGNED_BYTE, sWeatherPixels);
    return true;
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
    "uniform float opacity;\n"
    "void main() { vec4 sampleColor = texture(image, uv); color = vec4(sampleColor.rgb, sampleColor.a * opacity); }\n";

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
                         bool blend, float opacity)
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
    dglUniform1f(sOpacityUniform, opacity);
    dglBindBuffer(GL_ARRAY_BUFFER, sVertexBuffer);
    dglBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

static void UploadAtlasSlots(const struct DioramaTexture *texture, const u32 *pixels,
                             const u8 *updatedMetatiles)
{
    int row;

    glBindTexture(GL_TEXTURE_2D, texture->id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, DIORAMA_ATLAS_WIDTH);
    for (row = 0; row < DIORAMA_ATLAS_ROWS; row++)
    {
        int column = 0;

        while (column < DIORAMA_ATLAS_COLUMNS)
        {
            int first;
            int count;
            int x;
            int y;

            while (column < DIORAMA_ATLAS_COLUMNS
                && !(updatedMetatiles[(row * DIORAMA_ATLAS_COLUMNS + column) / 8]
                   & (1 << ((row * DIORAMA_ATLAS_COLUMNS + column) % 8))))
                column++;
            first = column;
            while (column < DIORAMA_ATLAS_COLUMNS
                && (updatedMetatiles[(row * DIORAMA_ATLAS_COLUMNS + column) / 8]
                  & (1 << ((row * DIORAMA_ATLAS_COLUMNS + column) % 8))))
                column++;
            count = column - first;
            if (count == 0)
                continue;
            x = first * DIORAMA_ATLAS_STRIDE;
            y = row * DIORAMA_ATLAS_STRIDE;
            glTexSubImage2D(GL_TEXTURE_2D, 0, x, y,
                            count * DIORAMA_ATLAS_STRIDE, DIORAMA_ATLAS_STRIDE,
                            GL_BGRA, GL_UNSIGNED_BYTE,
                            pixels + y * DIORAMA_ATLAS_WIDTH + x);
        }
    }
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
}

static void EnsureAtlas(const struct DioramaSceneSnapshot *snapshot)
{
    bool reset = snapshot->mapGeneration != sAtlasMapGeneration
              || snapshot->paletteGeneration != sAtlasPaletteGeneration
              || !sHasAtlasTileGraphics;
    bool animationChanged = snapshot->tilesetAnimationGeneration != sAtlasAnimationGeneration;
    bool changed;
    unsigned cellCount = snapshot->visibleCellCount;
    unsigned i;

    if (reset)
    {
        DioramaAtlas_Clear(sAtlasPixels, sPresentMetatiles);
        memset(sBaseAtlasPixels, 0, sizeof(sBaseAtlasPixels));
        memset(sForegroundAtlasPixels, 0, sizeof(sForegroundAtlasPixels));
        sAtlasMapGeneration = snapshot->mapGeneration;
        sAtlasPaletteGeneration = snapshot->paletteGeneration;
        sAtlasAnimationGeneration = snapshot->tilesetAnimationGeneration;
    }
    memset(sDirtyAtlasTiles, 0, sizeof(sDirtyAtlasTiles));
    memset(sForcedMetatiles, 0, sizeof(sForcedMetatiles));
    memset(sVisibleMetatiles, 0, sizeof(sVisibleMetatiles));
    memset(sUpdatedMetatiles, 0, sizeof(sUpdatedMetatiles));
    if (!reset && animationChanged)
    {
        for (i = 0; i < DIORAMA_TILE_COUNT; i++)
            if (memcmp(snapshot->tileGraphics + i * DIORAMA_TILE_BYTES,
                       sAtlasTileGraphics + i * DIORAMA_TILE_BYTES,
                       DIORAMA_TILE_BYTES) != 0)
                sDirtyAtlasTiles[i / 8] |= 1 << (i % 8);
    }
    if (cellCount > DIORAMA_MAX_VISIBLE_CELLS)
        cellCount = DIORAMA_MAX_VISIBLE_CELLS;
    DioramaRules_ResolveGrid(snapshot, sAtlasResolvedCells);
    for (i = 0; i < DIORAMA_TILE_COUNT; i++)
    {
        sCutoutBaseMetatiles[i] = 0xFFFF;
        if (reset)
            sPreviousCutoutBaseMetatiles[i] = 0xFFFF;
    }
    for (i = 0; i < cellCount; i++)
    {
        uint16_t metatileId = snapshot->cells[i].metatileId;
        uint16_t baseMetatileId = sAtlasResolvedCells[i].baseMetatileId;

        if (metatileId >= DIORAMA_TILE_COUNT)
            continue;
        sVisibleMetatiles[metatileId / 8] |= 1 << (metatileId % 8);
        if (baseMetatileId >= DIORAMA_TILE_COUNT)
            continue;
        if (sCutoutBaseMetatiles[metatileId] == 0xFFFF)
            sCutoutBaseMetatiles[metatileId] = baseMetatileId;
        else if (sCutoutBaseMetatiles[metatileId] != baseMetatileId)
            sCutoutBaseMetatiles[metatileId] = 0xFFFE;
    }
    for (i = 0; i < DIORAMA_TILE_COUNT; i++)
        if ((sVisibleMetatiles[i / 8] & (1 << (i % 8)))
         && sCutoutBaseMetatiles[i] != sPreviousCutoutBaseMetatiles[i])
            sForcedMetatiles[i / 8] |= 1 << (i % 8);
    changed = DioramaAtlas_UpdateDirty(snapshot, sCutoutBaseMetatiles,
                                       reset ? NULL : sDirtyAtlasTiles,
                                       reset ? NULL : sForcedMetatiles,
                                       sAtlasPixels, sBaseAtlasPixels,
                                       sForegroundAtlasPixels, sPresentMetatiles,
                                       sUpdatedMetatiles);
    if (reset)
    {
        glBindTexture(GL_TEXTURE_2D, sAtlasTexture.id);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, DIORAMA_ATLAS_WIDTH, DIORAMA_ATLAS_HEIGHT,
                        GL_BGRA, GL_UNSIGNED_BYTE, sAtlasPixels);
        glBindTexture(GL_TEXTURE_2D, sBaseAtlasTexture.id);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, DIORAMA_ATLAS_WIDTH, DIORAMA_ATLAS_HEIGHT,
                        GL_BGRA, GL_UNSIGNED_BYTE, sBaseAtlasPixels);
        glBindTexture(GL_TEXTURE_2D, sForegroundAtlasTexture.id);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, DIORAMA_ATLAS_WIDTH, DIORAMA_ATLAS_HEIGHT,
                        GL_BGRA, GL_UNSIGNED_BYTE, sForegroundAtlasPixels);
    }
    else if (changed)
    {
        UploadAtlasSlots(&sAtlasTexture, sAtlasPixels, sUpdatedMetatiles);
        UploadAtlasSlots(&sBaseAtlasTexture, sBaseAtlasPixels, sUpdatedMetatiles);
        UploadAtlasSlots(&sForegroundAtlasTexture, sForegroundAtlasPixels,
                         sUpdatedMetatiles);
    }
    memcpy(sAtlasTileGraphics, snapshot->tileGraphics, sizeof(sAtlasTileGraphics));
    for (i = 0; i < DIORAMA_TILE_COUNT; i++)
        if (sVisibleMetatiles[i / 8] & (1 << (i % 8)))
            sPreviousCutoutBaseMetatiles[i] = sCutoutBaseMetatiles[i];
    sHasAtlasTileGraphics = true;
    sAtlasAnimationGeneration = snapshot->tilesetAnimationGeneration;
}

static void GetCameraPosition(const struct DioramaSceneSnapshot *snapshot,
                              const struct DioramaSceneSnapshot *previousSnapshot,
                              bool hasPreviousSnapshot, float frameAlpha,
                              float *cameraX, float *cameraZ)
{
    float currentX = snapshot->cameraMapX
                   + (snapshot->cameraSubpixelX + snapshot->cameraPanX) / 16.0f;
    float currentZ = -(snapshot->cameraMapY
                     + (snapshot->cameraSubpixelY + snapshot->cameraPanY) / 16.0f);

    *cameraX = currentX;
    *cameraZ = currentZ;
    if (hasPreviousSnapshot
     && snapshot->sequence == previousSnapshot->sequence + 1
     && snapshot->mapGeneration == previousSnapshot->mapGeneration
     && snapshot->sceneKind == DIORAMA_SCENE_OVERWORLD_FREE
     && previousSnapshot->sceneKind == DIORAMA_SCENE_OVERWORLD_FREE)
    {
        float previousX = previousSnapshot->cameraMapX
                        + (previousSnapshot->cameraSubpixelX
                        + previousSnapshot->cameraPanX) / 16.0f;
        float previousZ = -(previousSnapshot->cameraMapY
                          + (previousSnapshot->cameraSubpixelY
                          + previousSnapshot->cameraPanY) / 16.0f);
        float deltaX = currentX - previousX;
        float deltaZ = currentZ - previousZ;

        if (deltaX * deltaX + deltaZ * deltaZ
         <= CAMERA_TELEPORT_LIMIT * CAMERA_TELEPORT_LIMIT)
        {
            *cameraX = previousX + deltaX * frameAlpha;
            *cameraZ = previousZ + deltaZ * frameAlpha;
        }
    }
}

static void GetCameraBase(const struct DioramaSceneSnapshot *snapshot,
                          float *pitch, float *focalLength)
{
    struct DioramaMapProfile profile;

    *pitch = CAMERA_DEFAULT_PITCH;
    *focalLength = CAMERA_DEFAULT_FOCAL_LENGTH;
    if (DioramaRules_GetMapProfile(snapshot->mapGroup, snapshot->mapNum,
                                   snapshot->mapLayoutId, &profile))
    {
        *pitch = profile.cameraPitch;
        *focalLength = profile.cameraFocalLength;
    }
}

static void GetCameraSettings(const struct DioramaSceneSnapshot *snapshot,
                              float *pitch, float *focalLength)
{
    GetCameraBase(snapshot, pitch, focalLength);
    if (sSurveyView > 0)
    {
        static const float sSurveyPitches[] = {
            0.0f, 0.26179939f, 0.61086524f, 0.87266463f, 1.30899694f,
        };

        *pitch = sSurveyPitches[sSurveyView];
    }
    else
        *pitch += sCameraPitchOffset;
    *focalLength += sCameraFocalLengthOffset;
    if (*pitch < CAMERA_MIN_PITCH) *pitch = CAMERA_MIN_PITCH;
    if (*pitch > CAMERA_MAX_PITCH) *pitch = CAMERA_MAX_PITCH;
    if (*focalLength < CAMERA_MIN_FOCAL_LENGTH) *focalLength = CAMERA_MIN_FOCAL_LENGTH;
    if (*focalLength > CAMERA_MAX_FOCAL_LENGTH) *focalLength = CAMERA_MAX_FOCAL_LENGTH;
}

static void SnapOpacity(float opacity)
{
    sTwoDOpacity = opacity;
    sFadeStartOpacity = opacity;
    sFadeTargetOpacity = opacity;
    sFadeStartCounter = SDL_GetPerformanceCounter();
}

static void SetOpacityTarget(float opacity)
{
    if (opacity == sFadeTargetOpacity)
        return;
    sFadeStartOpacity = sTwoDOpacity;
    sFadeTargetOpacity = opacity;
    sFadeStartCounter = SDL_GetPerformanceCounter();
}

static void UpdateOpacity(void)
{
    double elapsed;
    float progress;

    if (sTwoDOpacity == sFadeTargetOpacity)
        return;
    elapsed = (double)(SDL_GetPerformanceCounter() - sFadeStartCounter)
            / SDL_GetPerformanceFrequency();
    progress = elapsed >= SOURCE_FADE_SECONDS ? 1.0f : elapsed / SOURCE_FADE_SECONDS;
    sTwoDOpacity = sFadeStartOpacity
                 + (sFadeTargetOpacity - sFadeStartOpacity) * progress;
}

static void AdoptLatestSnapshot(void)
{
    if (!DioramaSnapshotExchange_CopyLatest(&sIncomingSceneSnapshot))
        return;
    if (!sHasSceneSnapshot)
    {
        sSceneSnapshot = sIncomingSceneSnapshot;
        sPreviousSceneSnapshot = sIncomingSceneSnapshot;
        sHasSceneSnapshot = true;
        sCurrent3DReady = false;
    }
    else if (sIncomingSceneSnapshot.sequence != sSceneSnapshot.sequence)
    {
        sPreviousSceneSnapshot = sSceneSnapshot;
        sSceneSnapshot = sIncomingSceneSnapshot;
        sHasPreviousSceneSnapshot = true;
        sCurrent3DReady = false;
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
    sObjectsAvailable = DioramaGLObjects_Init();
    if (!sObjectsAvailable)
        SDL_Log("Diorama object renderer unavailable; retaining the 2D OpenGL fallback");

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
    sOpacityUniform = dglGetUniformLocation(sProgram, "opacity");
    if (sOpacityUniform < 0)
        return false;

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
    glGenTextures(1, &sUiTexture.id);
    sUiTexture.width = DISPLAY_WIDTH;
    sUiTexture.height = DISPLAY_HEIGHT;
    ConfigureTexture(sUiTexture.id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, DISPLAY_WIDTH, DISPLAY_HEIGHT, 0,
                 GL_BGRA, GL_UNSIGNED_BYTE, NULL);
    glGenTextures(1, &sWeatherTexture.id);
    sWeatherTexture.width = DISPLAY_WIDTH;
    sWeatherTexture.height = DISPLAY_HEIGHT;
    ConfigureTexture(sWeatherTexture.id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, DISPLAY_WIDTH, DISPLAY_HEIGHT, 0,
                 GL_BGRA, GL_UNSIGNED_BYTE, NULL);
    glGenTextures(1, &sAtlasTexture.id);
    sAtlasTexture.width = DIORAMA_ATLAS_WIDTH;
    sAtlasTexture.height = DIORAMA_ATLAS_HEIGHT;
    ConfigureTexture(sAtlasTexture.id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, DIORAMA_ATLAS_WIDTH, DIORAMA_ATLAS_HEIGHT, 0,
                 GL_BGRA, GL_UNSIGNED_BYTE, NULL);
    glGenTextures(1, &sBaseAtlasTexture.id);
    sBaseAtlasTexture.width = DIORAMA_ATLAS_WIDTH;
    sBaseAtlasTexture.height = DIORAMA_ATLAS_HEIGHT;
    ConfigureTexture(sBaseAtlasTexture.id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, DIORAMA_ATLAS_WIDTH, DIORAMA_ATLAS_HEIGHT, 0,
                 GL_BGRA, GL_UNSIGNED_BYTE, NULL);
    glGenTextures(1, &sForegroundAtlasTexture.id);
    sForegroundAtlasTexture.width = DIORAMA_ATLAS_WIDTH;
    sForegroundAtlasTexture.height = DIORAMA_ATLAS_HEIGHT;
    ConfigureTexture(sForegroundAtlasTexture.id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, DIORAMA_ATLAS_WIDTH, DIORAMA_ATLAS_HEIGHT, 0,
                 GL_BGRA, GL_UNSIGNED_BYTE, NULL);
    DioramaAtlas_Clear(sAtlasPixels, sPresentMetatiles);
    memset(sBaseAtlasPixels, 0, sizeof(sBaseAtlasPixels));
    memset(sForegroundAtlasPixels, 0, sizeof(sForegroundAtlasPixels));
    sAtlasMapGeneration = 0;
    sAtlasPaletteGeneration = 0;
    sAtlasAnimationGeneration = 0;
    sHasAtlasTileGraphics = false;
    sHasSceneSnapshot = false;
    sHasPreviousSceneSnapshot = false;
    sHasRenderedSceneSnapshot = false;
    sHasPreviousRenderedSceneSnapshot = false;
    sCurrent3DReady = false;
    sProcessedSequence = 0;
    sUiTransientHoldFrames = 0;
    SnapOpacity(1.0f);
    sTerrainDebug = false;
    sCameraPitchOffset = 0.0f;
    sCameraFocalLengthOffset = 0.0f;
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
    AdoptLatestSnapshot();
    glBindTexture(GL_TEXTURE_2D, sFrameTexture.id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT,
                    GL_BGRA, GL_UNSIGNED_BYTE, argb8888);
    DioramaUI_RenderBgOverlay(
        DISPLAY_WIDTH, DISPLAY_HEIGHT,
        sHasSceneSnapshot ? sSceneSnapshot.uiRects : NULL,
        sHasSceneSnapshot ? sSceneSnapshot.uiRectCount : 0,
        sHasSceneSnapshot ? sSceneSnapshot.uiBgControl : 0,
        sHasSceneSnapshot ? sSceneSnapshot.uiBgHOffset : 0,
        sHasSceneSnapshot ? sSceneSnapshot.uiBgVOffset : 0,
        sHasSceneSnapshot ? sSceneSnapshot.uiBgTileGraphics : NULL,
        sHasSceneSnapshot ? sSceneSnapshot.uiBgTilemap : NULL,
        sHasSceneSnapshot ? sSceneSnapshot.fadedPalette : NULL,
        sUiPixels);
    glBindTexture(GL_TEXTURE_2D, sUiTexture.id);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT,
                    GL_BGRA, GL_UNSIGNED_BYTE, sUiPixels);
}

void DioramaGL_Present(u8 background, bool border, bool integerScale, float frameAlpha)
{
    int outputWidth;
    int outputHeight;
    int gameWidth;
    int gameHeight;
    int gameX;
    int gameY;
    const struct DioramaTexture *gameTexture = &sFrameTexture;
    bool drawTerrain = false;
    bool current3D = false;
    bool softFallback = false;
    bool newSequence = sHasSceneSnapshot && sSceneSnapshot.sequence != sProcessedSequence;
    enum DioramaPresentationDecision presentationDecision;
    float cameraX;
    float cameraZ;

    if (sSurveyView >= 0)
        frameAlpha = 1.0f;

    if (sRenderMode == DIORAMA_RENDER_AUTO
     && sTerrainAvailable
     && sObjectsAvailable
     && sHasSceneSnapshot
     && DioramaRules_IsMapSupported(sSceneSnapshot.mapGroup, sSceneSnapshot.mapNum,
                                    sSceneSnapshot.mapLayoutId)
     && DioramaSnapshot_CanRenderFlatMap(&sSceneSnapshot))
    {
        if (!sCurrent3DReady)
        {
            EnsureAtlas(&sSceneSnapshot);
            sCurrent3DReady = DioramaGLTerrain_Sync(&sSceneSnapshot, sAtlasResolvedCells)
                           && DioramaGLObjects_Sync(&sSceneSnapshot, sAtlasResolvedCells);
        }
        current3D = sCurrent3DReady;
    }
    else if (sRenderMode == DIORAMA_RENDER_DEBUG
          && sHasSceneSnapshot
          && DioramaSnapshot_CanRenderGrid(&sSceneSnapshot))
        gameTexture = &sDebugTexture;

    presentationDecision = DioramaTransition_Classify(
        sHasSceneSnapshot ? &sSceneSnapshot : NULL, current3D,
        &sRenderedSceneSnapshot, sHasRenderedSceneSnapshot);
    if (presentationDecision == DIORAMA_PRESENT_3D)
    {
        sUiTransientHoldFrames = 0;
        bool mapDiscontinuity = !sHasRenderedSceneSnapshot
                             || !DioramaTransition_IsSameMap(&sSceneSnapshot,
                                                             &sRenderedSceneSnapshot);

        if (!sHasRenderedSceneSnapshot
         || sRenderedSceneSnapshot.sequence != sSceneSnapshot.sequence)
        {
            if (sHasRenderedSceneSnapshot)
            {
                sPreviousRenderedSceneSnapshot = sRenderedSceneSnapshot;
                sHasPreviousRenderedSceneSnapshot = true;
            }
            sRenderedSceneSnapshot = sSceneSnapshot;
            sHasRenderedSceneSnapshot = true;
        }
        if (newSequence)
        {
            if (mapDiscontinuity)
                SnapOpacity(1.0f);
            SetOpacityTarget(0.0f);
        }
        drawTerrain = true;
    }
    else if (presentationDecision == DIORAMA_PRESENT_HOLD_3D
          && sHasRenderedSceneSnapshot)
    {
        if (newSequence && sUiTransientHoldFrames < 0xFF)
            sUiTransientHoldFrames++;
        if (sUiTransientHoldFrames <= UI_TRANSIENT_HOLD_FRAMES)
        {
            SetOpacityTarget(0.0f);
            drawTerrain = true;
        }
        else
        {
            SnapOpacity(1.0f);
        }
    }
    else if (presentationDecision == DIORAMA_PRESENT_SOFT_2D)
    {
        sUiTransientHoldFrames = 0;
        softFallback = true;
        if (newSequence)
            SetOpacityTarget(1.0f);
        drawTerrain = sTwoDOpacity < 1.0f;
    }
    else if (newSequence || !sHasSceneSnapshot)
    {
        sUiTransientHoldFrames = 0;
        SnapOpacity(1.0f);
    }
    if (newSequence)
        sProcessedSequence = sSceneSnapshot.sequence;
    if (sSurveyView >= 0)
        SnapOpacity(sSurveyView == 0 ? 1.0f : 0.0f);
    UpdateOpacity();
    if (softFallback && sTwoDOpacity < 1.0f)
        drawTerrain = true;

    SDL_GL_GetDrawableSize(sWindow, &outputWidth, &outputHeight);
    if (outputWidth <= 0 || outputHeight <= 0)
        return;
    glViewport(0, 0, outputWidth, outputHeight);
    glStencilMask(0xFF);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    dglUseProgram(sProgram);
    dglBindVertexArray(sVertexArray);

    if (sSurveyView < 0 && background < sBackgroundCount
     && sBackgroundTextures[background].id != 0)
        DrawTexture(&sBackgroundTextures[background], outputWidth, outputHeight,
                    0, 0, outputWidth, outputHeight,
                     0, 0, sBackgroundTextures[background].width,
                     sBackgroundTextures[background].height, true, 1.0f);

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
    if (sSurveyView >= 0)
    {
        gameWidth = outputWidth;
        gameHeight = outputHeight;
        gameX = 0;
        gameY = 0;
    }
    else
    {
        gameX = (outputWidth - gameWidth) / 2;
        gameY = (outputHeight - gameHeight) / 2;
    }
    if (drawTerrain)
    {
        float cameraPitch;
        float cameraFocalLength;
        float aspectCorrection = (float)DISPLAY_WIDTH * outputHeight
                               / ((float)DISPLAY_HEIGHT * outputWidth);

        GetCameraPosition(&sRenderedSceneSnapshot, &sPreviousRenderedSceneSnapshot,
                          sHasPreviousRenderedSceneSnapshot,
                          current3D ? frameAlpha : 1.0f, &cameraX, &cameraZ);
        GetCameraSettings(&sRenderedSceneSnapshot, &cameraPitch, &cameraFocalLength);
        glEnable(GL_SCISSOR_TEST);
        glScissor(0, 0, outputWidth, outputHeight);
        glClearColor(0.035f, 0.055f, 0.07f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glViewport(0, 0, outputWidth, outputHeight);
        DioramaGLTerrain_Draw(sAtlasTexture.id, sBaseAtlasTexture.id,
                               sForegroundAtlasTexture.id, cameraX, cameraZ,
                               cameraPitch, cameraFocalLength,
                               aspectCorrection, sTerrainDebug);
        DioramaGLObjects_Draw(current3D ? frameAlpha : 1.0f,
                               cameraX, cameraZ, cameraPitch,
                               cameraFocalLength, aspectCorrection);
        glDisable(GL_SCISSOR_TEST);
        glViewport(0, 0, outputWidth, outputHeight);
        dglUseProgram(sProgram);
        dglBindVertexArray(sVertexArray);
        if (sSurveyView < 0 && BuildWeatherImage(&sRenderedSceneSnapshot))
            DrawTexture(&sWeatherTexture, outputWidth, outputHeight,
                        0, 0, outputWidth, outputHeight,
                        0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, true, 1.0f);
        if (sTerrainDebug)
        {
            BuildDebugImage(&sRenderedSceneSnapshot);
            DrawTexture(&sDebugTexture, outputWidth, outputHeight,
                        gameX, gameY, gameWidth, gameHeight,
                        0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, true, 1.0f);
        }
        if (sTwoDOpacity > 0.0f)
            DrawTexture(&sFrameTexture, outputWidth, outputHeight,
                        gameX, gameY, gameWidth, gameHeight,
                        0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, true, sTwoDOpacity);
        if (current3D && sSceneSnapshot.uiRectCount > 0)
            DrawTexture(&sUiTexture, outputWidth, outputHeight,
                        gameX, gameY, gameWidth, gameHeight,
                        0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, true, 1.0f);
    }
    else
        DrawTexture(gameTexture, outputWidth, outputHeight,
                    gameX, gameY, gameWidth, gameHeight,
                    0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, false, 1.0f);

    if (sSurveyView < 0 && !drawTerrain && border && sBorderTexture.id != 0)
    {
        int innerWidth = gameWidth - 2;
        int innerHeight = gameHeight - 2;
        DrawTexture(&sBorderTexture, outputWidth, outputHeight,
                    gameX + 1 - innerWidth * 19 / 961,
                    gameY + 1 - innerHeight * 20 / 643,
                     innerWidth * 1000 / 961,
                     innerHeight * 683 / 643,
                     141, 18, 1000, 683, true, 1.0f);
    }
    if (sSurveyCaptureRequested)
    {
        const struct DioramaSceneSnapshot *captureSnapshot = sSurveyView == 0
                                                           ? &sSceneSnapshot
                                                           : &sRenderedSceneSnapshot;
        bool stateMatchesPixels = sSurveyView == 0
                               || (presentationDecision == DIORAMA_PRESENT_3D
                                && sHasRenderedSceneSnapshot
                                && sRenderedSceneSnapshot.sequence == sSceneSnapshot.sequence);

        CaptureSurveyFrame(outputWidth, outputHeight, captureSnapshot, stateMatchesPixels);
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

void DioramaGL_ToggleEnabled(void)
{
    if (sSurveyView >= 0)
    {
        DioramaGL_EndSurvey();
        return;
    }
    sRenderMode = sRenderMode == DIORAMA_RENDER_CLASSIC_2D
                ? DIORAMA_RENDER_AUTO
                : DIORAMA_RENDER_CLASSIC_2D;
}

void DioramaGL_CycleSurveyView(void)
{
    static const char *const sNames[] = {"flat", "v15", "v35", "v50", "v75"};

    if (sSurveyView < 0)
    {
        Uint32 flags = SDL_GetWindowFlags(sWindow);

        sSurveySavedRenderMode = sRenderMode;
        sSurveySavedFocalLengthOffset = sCameraFocalLengthOffset;
        sSurveySavedTerrainDebug = sTerrainDebug;
        sSurveySavedFullscreen = (flags & (SDL_WINDOW_FULLSCREEN
                                         | SDL_WINDOW_FULLSCREEN_DESKTOP)) != 0;
        if (sSurveySavedFullscreen)
            SDL_SetWindowFullscreen(sWindow, 0);
        SDL_GetWindowSize(sWindow, &sSurveySavedWindowWidth, &sSurveySavedWindowHeight);
        SDL_GetWindowPosition(sWindow, &sSurveySavedWindowX, &sSurveySavedWindowY);
        sSurveyRun = sHasSceneSnapshot ? NextSurveyRun(&sSceneSnapshot) : 1;
    }
    sSurveyView = (sSurveyView + 1) % 5;
    sRenderMode = sSurveyView == 0 ? DIORAMA_RENDER_CLASSIC_2D : DIORAMA_RENDER_AUTO;
    sCameraFocalLengthOffset = 0.0f;
    sTerrainDebug = false;
    SDL_SetWindowFullscreen(sWindow, 0);
    SDL_SetWindowSize(sWindow, 960, 640);
    SDL_Log("Diorama survey view: %s", sNames[sSurveyView]);
}

void DioramaGL_EndSurvey(void)
{
    if (sSurveyView < 0)
        return;
    sSurveyView = -1;
    sSurveyCaptureRequested = false;
    sRenderMode = sSurveySavedRenderMode;
    sCameraFocalLengthOffset = sSurveySavedFocalLengthOffset;
    sTerrainDebug = sSurveySavedTerrainDebug;
    SDL_SetWindowSize(sWindow, sSurveySavedWindowWidth, sSurveySavedWindowHeight);
    SDL_SetWindowPosition(sWindow, sSurveySavedWindowX, sSurveySavedWindowY);
    if (sSurveySavedFullscreen)
        SDL_SetWindowFullscreen(sWindow, SDL_WINDOW_FULLSCREEN_DESKTOP);
}

void DioramaGL_RequestSurveyCapture(void)
{
    if (sSurveyView >= 0)
        sSurveyCaptureRequested = true;
}

void DioramaGL_AdjustCameraZoom(int steps)
{
    float pitch;
    float base;
    float current;
    float adjusted;

    if (sSurveyView >= 0)
        return;
    if (sHasRenderedSceneSnapshot)
    {
        GetCameraSettings(&sRenderedSceneSnapshot, &pitch, &current);
        GetCameraBase(&sRenderedSceneSnapshot, &pitch, &base);
    }
    else
    {
        current = CAMERA_DEFAULT_FOCAL_LENGTH + sCameraFocalLengthOffset;
        base = CAMERA_DEFAULT_FOCAL_LENGTH;
    }
    adjusted = current + steps * CAMERA_ZOOM_STEP;
    if (adjusted < CAMERA_MIN_FOCAL_LENGTH) adjusted = CAMERA_MIN_FOCAL_LENGTH;
    if (adjusted > CAMERA_MAX_FOCAL_LENGTH) adjusted = CAMERA_MAX_FOCAL_LENGTH;
    sCameraFocalLengthOffset = adjusted - base;
}

void DioramaGL_AdjustCameraPitch(int steps)
{
    float current;
    float base;
    float focalLength;
    float adjusted;

    if (sSurveyView >= 0)
        DioramaGL_EndSurvey();
    if (sHasRenderedSceneSnapshot)
    {
        GetCameraSettings(&sRenderedSceneSnapshot, &current, &focalLength);
        GetCameraBase(&sRenderedSceneSnapshot, &base, &focalLength);
    }
    else
    {
        current = CAMERA_DEFAULT_PITCH + sCameraPitchOffset;
        base = CAMERA_DEFAULT_PITCH;
    }
    adjusted = current + steps * CAMERA_PITCH_STEP;
    if (adjusted < CAMERA_MIN_PITCH) adjusted = CAMERA_MIN_PITCH;
    if (adjusted > CAMERA_MAX_PITCH) adjusted = CAMERA_MAX_PITCH;
    sCameraPitchOffset = adjusted - base;
}

void DioramaGL_Shutdown(void)
{
    DioramaGLObjects_Shutdown();
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
    if (sUiTexture.id != 0)
        glDeleteTextures(1, &sUiTexture.id);
    if (sWeatherTexture.id != 0)
        glDeleteTextures(1, &sWeatherTexture.id);
    if (sAtlasTexture.id != 0)
        glDeleteTextures(1, &sAtlasTexture.id);
    if (sBaseAtlasTexture.id != 0)
        glDeleteTextures(1, &sBaseAtlasTexture.id);
    if (sForegroundAtlasTexture.id != 0)
        glDeleteTextures(1, &sForegroundAtlasTexture.id);
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
