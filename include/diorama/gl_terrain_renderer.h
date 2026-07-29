#ifndef GUARD_DIORAMA_GL_TERRAIN_RENDERER_H
#define GUARD_DIORAMA_GL_TERRAIN_RENDERER_H

#include <stdbool.h>
#include <stdint.h>
#include <SDL2/SDL_opengl.h>

#include "diorama/scene_snapshot.h"

struct DioramaTerrainMetrics
{
    uint16_t residentChunks;
    uint16_t activeChunks;
    uint16_t rebuiltChunks;
    uint16_t visibleChunks;
    uint16_t culledChunks;
    uint32_t vertices;
    uint32_t triangles;
    uint16_t drawCalls;
};

bool DioramaGLTerrain_Init(void);
void DioramaGLTerrain_Reset(void);
bool DioramaGLTerrain_Sync(const struct DioramaSceneSnapshot *snapshot);
void DioramaGLTerrain_Draw(GLuint atlasTexture, float cameraX, float cameraZ,
                           bool debug);
const struct DioramaTerrainMetrics *DioramaGLTerrain_GetMetrics(void);
void DioramaGLTerrain_Shutdown(void);

#endif
