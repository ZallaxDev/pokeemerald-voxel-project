#ifdef ENABLE_DIORAMA

#include <stdbool.h>
#include <SDL2/SDL.h>

#include "diorama/gl_loader.h"

#define DIORAMA_GL_FUNCTIONS(X) \
    X(ActiveTexture, PFNGLACTIVETEXTUREPROC) \
    X(AttachShader, PFNGLATTACHSHADERPROC) \
    X(BindBuffer, PFNGLBINDBUFFERPROC) \
    X(BindVertexArray, PFNGLBINDVERTEXARRAYPROC) \
    X(BufferData, PFNGLBUFFERDATAPROC) \
    X(CompileShader, PFNGLCOMPILESHADERPROC) \
    X(CreateProgram, PFNGLCREATEPROGRAMPROC) \
    X(CreateShader, PFNGLCREATESHADERPROC) \
    X(DeleteBuffers, PFNGLDELETEBUFFERSPROC) \
    X(DeleteProgram, PFNGLDELETEPROGRAMPROC) \
    X(DeleteShader, PFNGLDELETESHADERPROC) \
    X(DeleteVertexArrays, PFNGLDELETEVERTEXARRAYSPROC) \
    X(EnableVertexAttribArray, PFNGLENABLEVERTEXATTRIBARRAYPROC) \
    X(GenBuffers, PFNGLGENBUFFERSPROC) \
    X(GenVertexArrays, PFNGLGENVERTEXARRAYSPROC) \
    X(GetProgramInfoLog, PFNGLGETPROGRAMINFOLOGPROC) \
    X(GetProgramiv, PFNGLGETPROGRAMIVPROC) \
    X(GetShaderInfoLog, PFNGLGETSHADERINFOLOGPROC) \
    X(GetShaderiv, PFNGLGETSHADERIVPROC) \
    X(LinkProgram, PFNGLLINKPROGRAMPROC) \
    X(ShaderSource, PFNGLSHADERSOURCEPROC) \
    X(UseProgram, PFNGLUSEPROGRAMPROC) \
    X(VertexAttribPointer, PFNGLVERTEXATTRIBPOINTERPROC)

#define DEFINE_GL_FUNCTION(name, type) type dgl##name;
DIORAMA_GL_FUNCTIONS(DEFINE_GL_FUNCTION)

bool DioramaGL_LoadFunctions(void)
{
#define LOAD_GL_FUNCTION(name, type) \
    dgl##name = (type)SDL_GL_GetProcAddress("gl" #name); \
    if (dgl##name == NULL) \
    { \
        SDL_Log("OpenGL function gl%s is unavailable", #name); \
        return false; \
    }
    DIORAMA_GL_FUNCTIONS(LOAD_GL_FUNCTION)
#undef LOAD_GL_FUNCTION
    return true;
}

#endif
