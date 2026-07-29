#ifdef ENABLE_DIORAMA

#include <math.h>
#include <stddef.h>
#include <string.h>
#include <SDL2/SDL.h>

#include "diorama/gl_loader.h"
#include "diorama/gl_object_renderer.h"
#include "diorama/sprite_frame.h"

#define SPRITE_TEXTURE_CACHE_SIZE 48
#define OBJECT_RENDER_ITEM_CAPACITY (DIORAMA_MAX_OBJECTS + 1)

struct ObjectVertex
{
    float x;
    float y;
    float z;
    float u;
    float v;
};

struct SpriteTextureCacheEntry
{
    bool occupied;
    struct DioramaSpriteFrameKey key;
    GLuint texture;
    uint8_t width;
    uint8_t height;
    uint64_t lastUsedSequence;
};

struct ObjectRenderItem
{
    struct DioramaObjectSnapshot object;
    struct DioramaSpritePose previousPose;
    struct DioramaSpritePose currentPose;
    GLuint texture;
    GLuint reflectionTexture;
    uint8_t width;
    uint8_t height;
    bool interpolate;
    bool attachedToPlayer;
    int16_t previousAttachedOffsetX;
    int16_t previousAttachedOffsetY;
    int16_t attachedOffsetX;
    int16_t attachedOffsetY;
};

static struct SpriteTextureCacheEntry sTextureCache[SPRITE_TEXTURE_CACHE_SIZE];
static struct ObjectRenderItem sItems[OBJECT_RENDER_ITEM_CAPACITY];
static struct DioramaSceneSnapshot sPreviousSnapshot;
static uint32_t sDecodePixels[DIORAMA_SPRITE_MAX_PIXELS];
static struct DioramaObjectMetrics sMetrics;
static uint64_t sSequence;
static uint32_t sMapGeneration;
static uint8_t sItemCount;
static bool sHasPreviousSnapshot;
static bool sReady;
static GLuint sProgram;
static GLuint sVertexArray;
static GLuint sVertexBuffer;
static GLint sCameraLocation;
static GLint sCameraPitchLocation;
static GLint sFocalLengthLocation;
static GLint sImageLocation;
static GLint sDrawModeLocation;
static GLint sAnimationTimeLocation;

static const char sObjectVertexShader[] =
    "#version 330 core\n"
    "layout(location = 0) in vec3 position;\n"
    "layout(location = 1) in vec2 texCoord;\n"
    "uniform vec2 cameraPosition;\n"
    "uniform float cameraPitch;\n"
    "uniform float focalLength;\n"
    "out vec2 uv;\n"
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
    "  gl_Position = vec4(clipX, clipY, clipZ - 0.0008 * depth, depth);\n"
    "  uv = texCoord;\n"
    "}\n";

static const char sObjectFragmentShader[] =
    "#version 330 core\n"
    "in vec2 uv;\n"
    "out vec4 color;\n"
    "uniform sampler2D image;\n"
    "uniform int drawMode;\n"
    "uniform float animationTime;\n"
    "void main() {\n"
    "  if (drawMode == 1) {\n"
    "    vec2 point = uv * 2.0 - 1.0;\n"
    "    float distanceSquared = dot(point, point);\n"
    "    if (distanceSquared >= 1.0) discard;\n"
    "    color = vec4(0.02, 0.025, 0.03, 0.32 * (1.0 - distanceSquared));\n"
    "  } else {\n"
    "    vec2 sampleUv = uv;\n"
    "    if (drawMode == 2) sampleUv.x += sin(animationTime * 5.0 + uv.y * 14.0) * 0.012;\n"
    "    vec4 texel = texture(image, sampleUv);\n"
    "    if (texel.a < 0.5) discard;\n"
    "    color = texel;\n"
    "  }\n"
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
        SDL_Log("Diorama object shader compile failed: %s", log);
        dglDeleteShader(shader);
        return 0;
    }
    return shader;
}

static bool CreateProgram(void)
{
    GLuint vertexShader = CompileShader(GL_VERTEX_SHADER, sObjectVertexShader);
    GLuint fragmentShader = CompileShader(GL_FRAGMENT_SHADER, sObjectFragmentShader);
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
        SDL_Log("Diorama object shader link failed: %s", log);
        return false;
    }
    sCameraLocation = dglGetUniformLocation(sProgram, "cameraPosition");
    sCameraPitchLocation = dglGetUniformLocation(sProgram, "cameraPitch");
    sFocalLengthLocation = dglGetUniformLocation(sProgram, "focalLength");
    sImageLocation = dglGetUniformLocation(sProgram, "image");
    sDrawModeLocation = dglGetUniformLocation(sProgram, "drawMode");
    sAnimationTimeLocation = dglGetUniformLocation(sProgram, "animationTime");
    return sCameraLocation >= 0 && sCameraPitchLocation >= 0
        && sFocalLengthLocation >= 0 && sImageLocation >= 0
        && sDrawModeLocation >= 0 && sAnimationTimeLocation >= 0;
}

static void ClearTextureCache(void)
{
    int i;

    for (i = 0; i < SPRITE_TEXTURE_CACHE_SIZE; i++)
    {
        if (sTextureCache[i].texture != 0)
            glDeleteTextures(1, &sTextureCache[i].texture);
        memset(&sTextureCache[i], 0, sizeof(sTextureCache[i]));
    }
}

static struct SpriteTextureCacheEntry *FindTexture(const struct DioramaSpriteFrameKey *key)
{
    int i;

    for (i = 0; i < SPRITE_TEXTURE_CACHE_SIZE; i++)
        if (sTextureCache[i].occupied
         && DioramaSprite_FrameKeyEqual(&sTextureCache[i].key, key))
            return &sTextureCache[i];
    return NULL;
}

static struct SpriteTextureCacheEntry *AllocateTexture(void)
{
    struct SpriteTextureCacheEntry *oldest = &sTextureCache[0];
    int i;

    for (i = 0; i < SPRITE_TEXTURE_CACHE_SIZE; i++)
    {
        if (!sTextureCache[i].occupied)
            return &sTextureCache[i];
        if (sTextureCache[i].lastUsedSequence < oldest->lastUsedSequence)
            oldest = &sTextureCache[i];
    }
    if (oldest->texture != 0)
        glDeleteTextures(1, &oldest->texture);
    memset(oldest, 0, sizeof(*oldest));
    return oldest;
}

static struct SpriteTextureCacheEntry *GetTexture(const struct DioramaSceneSnapshot *snapshot,
                                                   const struct DioramaObjectSnapshot *object)
{
    struct DioramaSpriteFrameKey key;
    struct SpriteTextureCacheEntry *entry;
    uint8_t width;
    uint8_t height;

    DioramaSprite_MakeFrameKey(snapshot, object, &key);
    entry = FindTexture(&key);
    if (entry != NULL)
    {
        entry->lastUsedSequence = snapshot->sequence;
        return entry;
    }
    if (!DioramaSprite_DecodeFrame(snapshot, object, sDecodePixels,
                                    DIORAMA_SPRITE_MAX_PIXELS, &width, &height))
        return NULL;
    entry = AllocateTexture();
    glGenTextures(1, &entry->texture);
    if (entry->texture == 0)
        return NULL;
    glBindTexture(GL_TEXTURE_2D, entry->texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0,
                 GL_BGRA, GL_UNSIGNED_BYTE, sDecodePixels);
    entry->occupied = true;
    entry->key = key;
    entry->width = width;
    entry->height = height;
    entry->lastUsedSequence = snapshot->sequence;
    sMetrics.uploadedFrames++;
    return entry;
}

static const struct DioramaObjectSnapshot *FindPreviousObject(
    const struct DioramaObjectSnapshot *object)
{
    int i;

    if (!sHasPreviousSnapshot)
        return NULL;
    for (i = 0; i < sPreviousSnapshot.objectCount; i++)
    {
        const struct DioramaObjectSnapshot *candidate = &sPreviousSnapshot.objects[i];

        if (candidate->localId == object->localId
         && !((candidate->flags ^ object->flags) & DIORAMA_OBJECT_PLAYER))
            return candidate;
    }
    return NULL;
}

bool DioramaGLObjects_Init(void)
{
    memset(sTextureCache, 0, sizeof(sTextureCache));
    memset(sItems, 0, sizeof(sItems));
    memset(&sMetrics, 0, sizeof(sMetrics));
    sSequence = 0;
    sMapGeneration = 0;
    sItemCount = 0;
    sHasPreviousSnapshot = false;
    sReady = false;
    if (!CreateProgram())
        return false;
    dglGenVertexArrays(1, &sVertexArray);
    dglGenBuffers(1, &sVertexBuffer);
    dglBindVertexArray(sVertexArray);
    dglBindBuffer(GL_ARRAY_BUFFER, sVertexBuffer);
    dglEnableVertexAttribArray(0);
    dglVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(struct ObjectVertex), NULL);
    dglEnableVertexAttribArray(1);
    dglVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(struct ObjectVertex),
                           (void *)offsetof(struct ObjectVertex, u));
    return true;
}

void DioramaGLObjects_Reset(void)
{
    ClearTextureCache();
    memset(sItems, 0, sizeof(sItems));
    memset(&sMetrics, 0, sizeof(sMetrics));
    sSequence = 0;
    sItemCount = 0;
    sHasPreviousSnapshot = false;
    sReady = false;
}

bool DioramaGLObjects_Sync(const struct DioramaSceneSnapshot *snapshot)
{
    struct ObjectRenderItem nextItems[OBJECT_RENDER_ITEM_CAPACITY];
    uint8_t nextItemCount = 0;
    bool playerFound = false;
    int playerItem = -1;
    int i;

    if (snapshot->sequence == sSequence)
        return sReady;
    sMetrics.uploadedFrames = 0;
    if (sMapGeneration != 0 && snapshot->mapGeneration != sMapGeneration)
        ClearTextureCache();
    sMapGeneration = snapshot->mapGeneration;

    for (i = 0; i < snapshot->objectCount; i++)
    {
        const struct DioramaObjectSnapshot *object = &snapshot->objects[i];
        const struct DioramaObjectSnapshot *previousObject;
        struct SpriteTextureCacheEntry *texture;
        struct ObjectRenderItem *item;

        if (!object->active || (object->flags & (DIORAMA_OBJECT_INVISIBLE | DIORAMA_OBJECT_OFFSCREEN)))
            continue;
        if (!DioramaSprite_IsSupported(object) || nextItemCount >= DIORAMA_MAX_OBJECTS)
            return false;
        texture = GetTexture(snapshot, object);
        if (texture == NULL)
            return false;
        item = &nextItems[nextItemCount++];
        memset(item, 0, sizeof(*item));
        item->object = *object;
        item->texture = texture->texture;
        item->width = texture->width;
        item->height = texture->height;
        if ((object->flags & DIORAMA_OBJECT_REFLECTION) && !object->reflectionHidden)
        {
            struct DioramaObjectSnapshot reflection = *object;

            reflection.paletteNum = object->reflectionPaletteNum;
            reflection.vFlip = !object->vFlip;
            texture = GetTexture(snapshot, &reflection);
            if (texture == NULL)
                return false;
            item->reflectionTexture = texture->texture;
        }
        if (!DioramaSprite_BuildPose(snapshot, object, &item->currentPose))
            return false;
        item->previousPose = item->currentPose;
        previousObject = FindPreviousObject(object);
        if (previousObject != NULL
         && DioramaSprite_CanInterpolate(&sPreviousSnapshot, previousObject, snapshot, object))
        {
            DioramaSprite_BuildPose(&sPreviousSnapshot, previousObject, &item->previousPose);
            item->interpolate = true;
        }
        if (object->flags & DIORAMA_OBJECT_PLAYER)
        {
            playerFound = true;
            playerItem = nextItemCount - 1;
        }
    }
    if (!playerFound)
        return false;
    if (snapshot->surfBlobValid
     && !(snapshot->surfBlob.flags & DIORAMA_OBJECT_INVISIBLE)
     && nextItemCount < OBJECT_RENDER_ITEM_CAPACITY)
    {
        const struct DioramaObjectSnapshot *blob = &snapshot->surfBlob;
        struct SpriteTextureCacheEntry *texture;
        struct ObjectRenderItem *item;

        if (!DioramaSprite_IsSupported(blob))
            return false;
        texture = GetTexture(snapshot, blob);
        if (texture == NULL)
            return false;
        item = &nextItems[nextItemCount++];
        memset(item, 0, sizeof(*item));
        item->object = *blob;
        item->texture = texture->texture;
        item->width = texture->width;
        item->height = texture->height;
        item->previousPose = nextItems[playerItem].previousPose;
        item->currentPose = nextItems[playerItem].currentPose;
        item->interpolate = nextItems[playerItem].interpolate;
        item->attachedToPlayer = true;
        item->previousAttachedOffsetX = snapshot->surfBlobOffsetX;
        item->previousAttachedOffsetY = snapshot->surfBlobOffsetY;
        item->attachedOffsetX = snapshot->surfBlobOffsetX;
        item->attachedOffsetY = snapshot->surfBlobOffsetY;
        if (item->interpolate && sPreviousSnapshot.surfBlobValid)
        {
            item->previousAttachedOffsetX = sPreviousSnapshot.surfBlobOffsetX;
            item->previousAttachedOffsetY = sPreviousSnapshot.surfBlobOffsetY;
        }
    }

    memcpy(sItems, nextItems, nextItemCount * sizeof(*sItems));
    sItemCount = nextItemCount;
    sMetrics.visibleObjects = sItemCount;
    sMetrics.cachedFrames = 0;
    for (i = 0; i < SPRITE_TEXTURE_CACHE_SIZE; i++)
        if (sTextureCache[i].occupied)
            sMetrics.cachedFrames++;
    sPreviousSnapshot = *snapshot;
    sHasPreviousSnapshot = true;
    sSequence = snapshot->sequence;
    sReady = true;
    return true;
}

static void UploadAndDraw(const struct ObjectVertex vertices[6])
{
    dglBindBuffer(GL_ARRAY_BUFFER, sVertexBuffer);
    dglBufferData(GL_ARRAY_BUFFER, 6 * sizeof(*vertices), vertices, GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    sMetrics.drawCalls++;
}

static void DrawShadow(const struct ObjectRenderItem *item, struct DioramaSpritePose pose)
{
    float radiusX = 0.28f + item->object.shadowSize * 0.06f;
    float radiusZ = 0.16f + item->object.shadowSize * 0.035f;
    float y = pose.groundY + 0.012f;
    const struct ObjectVertex vertices[6] = {
        {pose.x - radiusX, y, pose.z - radiusZ, 0, 0},
        {pose.x + radiusX, y, pose.z - radiusZ, 1, 0},
        {pose.x + radiusX, y, pose.z + radiusZ, 1, 1},
        {pose.x - radiusX, y, pose.z - radiusZ, 0, 0},
        {pose.x + radiusX, y, pose.z + radiusZ, 1, 1},
        {pose.x - radiusX, y, pose.z + radiusZ, 0, 1},
    };

    UploadAndDraw(vertices);
}

static void DrawBillboard(const struct ObjectRenderItem *item, struct DioramaSpritePose pose,
                          float pitchSin, float pitchCos)
{
    float halfWidth = item->width / 32.0f;
    float height = item->height / 16.0f;
    float topY = pose.y + height * pitchCos;
    float topZ = pose.z + height * pitchSin;
    const struct ObjectVertex vertices[6] = {
        {pose.x - halfWidth, pose.y, pose.z, 0, 1},
        {pose.x + halfWidth, pose.y, pose.z, 1, 1},
        {pose.x + halfWidth, topY, topZ, 1, 0},
        {pose.x - halfWidth, pose.y, pose.z, 0, 1},
        {pose.x + halfWidth, topY, topZ, 1, 0},
        {pose.x - halfWidth, topY, topZ, 0, 0},
    };

    glBindTexture(GL_TEXTURE_2D, item->texture);
    UploadAndDraw(vertices);
}

static void DrawReflection(const struct ObjectRenderItem *item,
                           struct DioramaSpritePose pose,
                           float pitchSin, float pitchCos)
{
    float halfWidth = item->width / 32.0f;
    float height = item->height / 16.0f;
    float bottomY = pose.y - height * pitchCos;
    float bottomZ = pose.z - height * pitchSin;
    const struct ObjectVertex vertices[6] = {
        {pose.x - halfWidth, pose.y, pose.z, 0, 0},
        {pose.x + halfWidth, pose.y, pose.z, 1, 0},
        {pose.x + halfWidth, bottomY, bottomZ, 1, 1},
        {pose.x - halfWidth, pose.y, pose.z, 0, 0},
        {pose.x + halfWidth, bottomY, bottomZ, 1, 1},
        {pose.x - halfWidth, bottomY, bottomZ, 0, 1},
    };

    glBindTexture(GL_TEXTURE_2D, item->reflectionTexture);
    UploadAndDraw(vertices);
}

void DioramaGLObjects_Draw(float frameAlpha, float cameraX, float cameraZ,
                           float cameraPitch, float focalLength)
{
    struct DioramaSpritePose poses[OBJECT_RENDER_ITEM_CAPACITY];
    uint8_t order[OBJECT_RENDER_ITEM_CAPACITY];
    float pitchSin = sinf(cameraPitch);
    float pitchCos = cosf(cameraPitch);
    int i;

    if (!sReady)
        return;
    if (frameAlpha < 0.0f) frameAlpha = 0.0f;
    if (frameAlpha > 1.0f) frameAlpha = 1.0f;
    for (i = 0; i < sItemCount; i++)
    {
        poses[i] = sItems[i].interpolate
            ? DioramaSprite_InterpolatePose(sItems[i].previousPose, sItems[i].currentPose, frameAlpha)
            : sItems[i].currentPose;
        if (sItems[i].attachedToPlayer)
        {
            float offsetX = sItems[i].previousAttachedOffsetX
                          + (sItems[i].attachedOffsetX - sItems[i].previousAttachedOffsetX)
                          * frameAlpha;
            float offsetY = sItems[i].previousAttachedOffsetY
                          + (sItems[i].attachedOffsetY - sItems[i].previousAttachedOffsetY)
                          * frameAlpha;

            poses[i] = DioramaSprite_ApplyScreenOffset(poses[i], offsetX, offsetY,
                                                       cameraPitch);
        }
        order[i] = i;
    }
    for (i = 1; i < sItemCount; i++)
    {
        uint8_t value = order[i];
        int position = i;

        while (position > 0)
        {
            const struct ObjectRenderItem *left = &sItems[order[position - 1]];
            const struct ObjectRenderItem *right = &sItems[value];
            if (DioramaSprite_CompareDepth(&poses[order[position - 1]], left->object.priority,
                     left->object.subpriority, left->object.oamOrder, &poses[value],
                     right->object.priority, right->object.subpriority,
                     right->object.oamOrder, cameraZ, cameraPitch) <= 0)
                break;
            order[position] = order[position - 1];
            position--;
        }
        order[position] = value;
    }

    sMetrics.drawCalls = 0;
    dglUseProgram(sProgram);
    dglBindVertexArray(sVertexArray);
    dglUniform2f(sCameraLocation, cameraX, cameraZ);
    dglUniform1f(sCameraPitchLocation, cameraPitch);
    dglUniform1f(sFocalLengthLocation, focalLength);
    dglUniform1i(sImageLocation, 0);
    dglUniform1f(sAnimationTimeLocation, (float)fmod(
        (double)SDL_GetPerformanceCounter() / SDL_GetPerformanceFrequency(), 120.0));
    dglActiveTexture(GL_TEXTURE0);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    dglUniform1i(sDrawModeLocation, 2);
    glEnable(GL_STENCIL_TEST);
    glStencilMask(0x00);
    glStencilFunc(GL_EQUAL, 1, 0xFF);
    glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
    for (i = 0; i < sItemCount; i++)
    {
        int index = order[i];

        if (sItems[index].reflectionTexture != 0)
        {
            struct DioramaSpritePose reflectionPose = DioramaSprite_ApplyScreenOffset(
                poses[index], 0, sItems[index].object.reflectionOffsetY, cameraPitch);

            DrawReflection(&sItems[index], reflectionPose, pitchSin, pitchCos);
        }
    }
    glDisable(GL_STENCIL_TEST);
    dglUniform1i(sDrawModeLocation, 1);
    for (i = 0; i < sItemCount; i++)
    {
        int index = order[i];
        if ((sItems[index].object.flags & DIORAMA_OBJECT_SHADOW)
         && !sItems[index].attachedToPlayer)
            DrawShadow(&sItems[index], poses[index]);
    }

    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    dglUniform1i(sDrawModeLocation, 0);
    for (i = 0; i < sItemCount; i++)
    {
        int index = order[i];
        DrawBillboard(&sItems[index], poses[index], pitchSin, pitchCos);
    }
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
}

const struct DioramaObjectMetrics *DioramaGLObjects_GetMetrics(void)
{
    return &sMetrics;
}

void DioramaGLObjects_Shutdown(void)
{
    ClearTextureCache();
    if (sVertexBuffer != 0)
        dglDeleteBuffers(1, &sVertexBuffer);
    if (sVertexArray != 0)
        dglDeleteVertexArrays(1, &sVertexArray);
    if (sProgram != 0)
        dglDeleteProgram(sProgram);
    sVertexBuffer = 0;
    sVertexArray = 0;
    sProgram = 0;
    sReady = false;
}

#endif
