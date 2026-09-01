#include "shader_pipeline_v2.h"

#include <GL/gl.h>
#include <SDL2/SDL.h>
#include <dlfcn.h>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace
{
using SwapWindowFn = void (*)(SDL_Window *);

SwapWindowFn g_real_swap = nullptr;
thread_local bool g_in_swap = false;
ShaderPipelineV2 g_pipeline;
GLuint g_capture_texture = 0u;
int g_capture_width = 0;
int g_capture_height = 0;
bool g_initialized = false;

void DebugLog(const char *format, ...)
{
    if (!std::getenv("AGS_SHADER_DEBUG"))
        return;

    va_list args;
    va_start(args, format);
    std::vfprintf(stderr, format, args);
    va_end(args);
    std::fputc('\n', stderr);
}

void LogGLErrors(const char *where)
{
    if (!std::getenv("AGS_SHADER_DEBUG"))
        return;

    GLenum error = GL_NO_ERROR;
    while ((error = glGetError()) != GL_NO_ERROR)
        DebugLog("AGS shader: OpenGL error 0x%x after %s", error, where);
}

void ResolveRealSwap()
{
    if (!g_real_swap)
        g_real_swap = reinterpret_cast<SwapWindowFn>(
            dlsym(RTLD_NEXT, "SDL_GL_SwapWindow"));
}

bool EnsureCaptureTexture(int width, int height)
{
    if (g_capture_texture != 0u &&
        g_capture_width == width && g_capture_height == height)
        return true;

    if (g_capture_texture != 0u)
        glDeleteTextures(1, &g_capture_texture);

    glGenTextures(1, &g_capture_texture);
    glBindTexture(GL_TEXTURE_2D, g_capture_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glBindTexture(GL_TEXTURE_2D, 0);

    g_capture_width = width;
    g_capture_height = height;
    return g_capture_texture != 0u;
}

void InitializePipeline()
{
    if (g_initialized)
        return;
    g_initialized = true;

    const char *shader_path = std::getenv("AGS_SHADER_CHAIN");
    if (!shader_path || shader_path[0] == '\0')
        shader_path = std::getenv("AGS_SHADER");

    if (!shader_path || shader_path[0] == '\0')
    {
        DebugLog("AGS shader: AGS_SHADER_CHAIN/AGS_SHADER is not set");
        return;
    }

    std::string error;
    if (!g_pipeline.load(shader_path, error))
    {
        DebugLog("AGS shader: failed to load '%s': %s",
                 shader_path, error.c_str());
        return;
    }

    DebugLog("AGS shader: loaded '%s'", shader_path);
}
}

extern "C" void SDL_GL_SwapWindow(SDL_Window *window)
{
    ResolveRealSwap();
    if (!g_real_swap)
        return;

    if (g_in_swap)
    {
        g_real_swap(window);
        return;
    }

    g_in_swap = true;
    InitializePipeline();

    if (g_pipeline.loaded())
    {
        int width = 0;
        int height = 0;
        SDL_GL_GetDrawableSize(window, &width, &height);

        if (width > 0 && height > 0 && EnsureCaptureTexture(width, height))
        {
            GLint old_read_buffer = GL_BACK;
            glGetIntegerv(GL_READ_BUFFER, &old_read_buffer);

            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glReadBuffer(GL_BACK);
            glBindTexture(GL_TEXTURE_2D, g_capture_texture);
            glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
                                0, 0, width, height);
            glBindTexture(GL_TEXTURE_2D, 0);
            LogGLErrors("framebuffer capture");

            g_pipeline.apply(g_capture_texture, width, height, width, height);
            LogGLErrors("post-processing");

            glReadBuffer(static_cast<GLenum>(old_read_buffer));
        }
    }

    g_real_swap(window);
    g_in_swap = false;
}
