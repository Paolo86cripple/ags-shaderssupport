#include "shader_pipeline.h"

#include <GL/gl.h>
#include <SDL2/SDL.h>
#include <dlfcn.h>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace {
using SwapWindowFn = void (*)(SDL_Window *);

SwapWindowFn g_real_swap = nullptr;
thread_local bool g_in_swap = false;
ShaderPipeline g_pipeline;
GLuint g_capture_texture = 0u;
int g_capture_width = 0;
int g_capture_height = 0;
bool g_initialized = false;

void DebugLog(const char *format, ...) {
    if (!std::getenv("AGS_SHADER_DEBUG")) return;
    va_list args;
    va_start(args, format);
    std::vfprintf(stderr, format, args);
    va_end(args);
    std::fputc('\n', stderr);
}

void ResolveRealSwap() {
    if (!g_real_swap)
        g_real_swap = reinterpret_cast<SwapWindowFn>(dlsym(RTLD_NEXT, "SDL_GL_SwapWindow"));
}

bool EnsureCaptureTexture(int width, int height) {
    if (g_capture_texture && g_capture_width == width && g_capture_height == height)
        return true;

    if (g_capture_texture) glDeleteTextures(1, &g_capture_texture);
    g_capture_texture = 0;

    glGenTextures(1, &g_capture_texture);
    glBindTexture(GL_TEXTURE_2D, g_capture_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D,
                 0,
                 GL_RGBA,
                 width,
                 height,
                 0,
                 GL_RGBA,
                 GL_UNSIGNED_BYTE,
                 nullptr);

    if (glGetError() != GL_NO_ERROR) {
        if (g_capture_texture) glDeleteTextures(1, &g_capture_texture);
        g_capture_texture = 0;
        g_capture_width = 0;
        g_capture_height = 0;
        return false;
    }

    g_capture_width = width;
    g_capture_height = height;
    return true;
}

void InitializePipeline() {
    if (g_initialized) return;
    g_initialized = true;

    const char *shader_path = std::getenv("AGS_SHADER_CHAIN");
    if (!shader_path || !shader_path[0]) shader_path = std::getenv("AGS_SHADER");
    if (!shader_path || !shader_path[0]) {
        DebugLog("AGS shader: no shader selected");
        return;
    }

    std::string error;
    if (!g_pipeline.load(shader_path, error)) {
        DebugLog("AGS shader: failed to load '%s': %s", shader_path, error.c_str());
        return;
    }
    DebugLog("AGS shader: loaded '%s'", shader_path);
}

bool CaptureBackBuffer(int width, int height) {
    GLint old_active = GL_TEXTURE0;
    GLint old_texture0 = 0;
    GLint old_read = GL_BACK;

    glGetIntegerv(GL_ACTIVE_TEXTURE, &old_active);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &old_texture0);
    glGetIntegerv(GL_READ_BUFFER, &old_read);

    glReadBuffer(GL_BACK);
    const bool ready = EnsureCaptureTexture(width, height);
    if (ready) {
        glBindTexture(GL_TEXTURE_2D, g_capture_texture);
        glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, width, height);
    }

    const GLenum copy_error = ready ? glGetError() : GL_NO_ERROR;
    glReadBuffer(static_cast<GLenum>(old_read));
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(old_texture0));
    glActiveTexture(static_cast<GLenum>(old_active));

    if (!ready || copy_error != GL_NO_ERROR) {
        DebugLog("AGS shader: failed to capture back buffer (GL error 0x%x)", copy_error);
        return false;
    }
    return true;
}
}

extern "C" void SDL_GL_SwapWindow(SDL_Window *window) {
    ResolveRealSwap();
    if (!g_real_swap) return;
    if (g_in_swap) {
        g_real_swap(window);
        return;
    }

    g_in_swap = true;
    InitializePipeline();

    if (g_pipeline.loaded()) {
        int width = 0;
        int height = 0;
        SDL_GL_GetDrawableSize(window, &width, &height);
        if (width > 0 && height > 0 && CaptureBackBuffer(width, height))
            g_pipeline.apply(g_capture_texture, width, height, width, height);
    }

    g_real_swap(window);
    g_in_swap = false;
}
