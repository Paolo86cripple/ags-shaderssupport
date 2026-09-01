#include "shader_pipeline_v2.h"
#include <SDL2/SDL.h>
#include <GL/gl.h>
#include <cstdio>
#include <cstdlib>
#include <csignal>
#include <execinfo.h>
#include <string>

static void segv_handler(int signal)
{
    void *frames[64];
    const int count = backtrace(frames, 64);
    std::fprintf(stderr, "shader test: caught signal %d\n", signal);
    backtrace_symbols_fd(frames, count, fileno(stderr));
    std::_Exit(128 + signal);
}

static int fail(SDL_Window *window, SDL_GLContext context, int code, const char *message)
{
    std::fprintf(stderr, "shader test: %s\n", message);
    if (context)
        SDL_GL_DeleteContext(context);
    if (window)
        SDL_DestroyWindow(window);
    SDL_Quit();
    return code;
}

int main(int argc, char **argv)
{
    std::signal(SIGSEGV, segv_handler);
    std::signal(SIGABRT, segv_handler);

    const char *shader = argc > 1 ? argv[1] : "shaders/identity.glsl";
    const bool expect_invert = argc > 2 && std::string(argv[2]) == "invert";

    if (SDL_Init(SDL_INIT_VIDEO) != 0)
        return 2;

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    SDL_Window *window = SDL_CreateWindow("ags-shader-test", 0, 0, 64, 64,
        SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    if (!window)
        return fail(nullptr, nullptr, 3, SDL_GetError());

    SDL_GLContext context = SDL_GL_CreateContext(window);
    if (!context)
        return fail(window, nullptr, 4, SDL_GetError());

    if (SDL_GL_MakeCurrent(window, context) != 0)
        return fail(window, context, 5, SDL_GetError());

    const char *version = reinterpret_cast<const char *>(glGetString(GL_VERSION));
    const char *renderer = reinterpret_cast<const char *>(glGetString(GL_RENDERER));
    if (!version || !renderer)
        return fail(window, context, 6, "OpenGL context has no GL_VERSION/GL_RENDERER");

    std::fprintf(stderr, "shader test: OpenGL %s / %s\n", version, renderer);
    std::fprintf(stderr, "shader test: before texture setup\n");

    GLuint source_texture = 0;
    glGenTextures(1, &source_texture);
    if (!source_texture)
        return fail(window, context, 7, "glGenTextures returned 0");

    glBindTexture(GL_TEXTURE_2D, source_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    const unsigned char pixel[4] = { 32, 64, 128, 255 };
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, pixel);

    GLenum error = glGetError();
    if (error != GL_NO_ERROR)
    {
        char message[64];
        std::snprintf(message, sizeof(message), "texture setup OpenGL error 0x%x", error);
        return fail(window, context, 8, message);
    }

    std::fprintf(stderr, "shader test: before pipeline construction\n");
    ShaderPipelineV2 pipeline;
    std::string load_error;
    std::fprintf(stderr, "shader test: before pipeline load\n");
    if (!pipeline.load(shader, load_error))
    {
        return fail(window, context, 9, load_error.c_str());
    }
    std::fprintf(stderr, "shader test: after pipeline load\n");

    std::fprintf(stderr, "shader test: before pipeline apply\n");
    pipeline.apply(source_texture, 1, 1, 64, 64);
    std::fprintf(stderr, "shader test: after pipeline apply\n");
    glFinish();

    error = glGetError();
    if (error != GL_NO_ERROR)
    {
        char message[64];
        std::snprintf(message, sizeof(message), "pipeline OpenGL error 0x%x", error);
        return fail(window, context, 10, message);
    }

    unsigned char result[4] = { 0, 0, 0, 0 };
    glReadBuffer(GL_BACK);
    glReadPixels(32, 32, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, result);

    error = glGetError();
    if (error != GL_NO_ERROR)
    {
        char message[64];
        std::snprintf(message, sizeof(message), "readback OpenGL error 0x%x", error);
        return fail(window, context, 11, message);
    }

    const unsigned char expected[4] = {
        static_cast<unsigned char>(expect_invert ? 223 : 32),
        static_cast<unsigned char>(expect_invert ? 191 : 64),
        static_cast<unsigned char>(expect_invert ? 127 : 128),
        255
    };

    const bool ok = result[0] == expected[0] && result[1] == expected[1] &&
                    result[2] == expected[2] && result[3] == expected[3];

    if (!ok)
    {
        std::fprintf(stderr,
            "shader output mismatch: got %u,%u,%u,%u expected %u,%u,%u,%u\n",
            result[0], result[1], result[2], result[3],
            expected[0], expected[1], expected[2], expected[3]);
    }
    else
    {
        std::fprintf(stderr, "shader test: PASS (%u,%u,%u,%u)\n",
            result[0], result[1], result[2], result[3]);
    }

    glDeleteTextures(1, &source_texture);
    SDL_GL_DeleteContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return ok ? 0 : 12;
}
