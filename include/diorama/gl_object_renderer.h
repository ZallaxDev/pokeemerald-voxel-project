#ifndef GUARD_DIORAMA_GL_OBJECT_RENDERER_H
#define GUARD_DIORAMA_GL_OBJECT_RENDERER_H

#include <stdbool.h>
#include <stdint.h>

#include "diorama/scene_snapshot.h"

struct DioramaObjectMetrics
{
    uint16_t visibleObjects;
    uint16_t cachedFrames;
    uint16_t uploadedFrames;
    uint16_t drawCalls;
};

bool DioramaGLObjects_Init(void);
void DioramaGLObjects_Reset(void);
bool DioramaGLObjects_Sync(const struct DioramaSceneSnapshot *snapshot);
void DioramaGLObjects_Draw(float frameAlpha, float cameraX, float cameraZ);
const struct DioramaObjectMetrics *DioramaGLObjects_GetMetrics(void);
void DioramaGLObjects_Shutdown(void);

#endif
