#include "shader_pipeline_v4.h"

#include <SDL2/SDL.h>
#include <GL/gl.h>

#include <cstdio>
#include <string>

int main(int argc, char **argv) {
    if (argc < 2) {
        std::fprintf(stderr, "shader load test: missing preset path\n");
        return 2;
    }

    if (SDL_Init(SDL_INIT_VIDEO) != 0) return 3;
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);

    SDL_Window *window = SDL_CreateWindow("ags-shader-load-test",
                                          0, 0, 64, 64,
                                          SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    if (!window) {
        std::fprintf(stderr, "shader load test: %s\n", SDL_GetError());
        SDL_Quit();
        return 4;
    }

    SDL_GLContext context = SDL_GL_CreateContext(window);
    if (!context) {
        std::fprintf(stderr, "shader load test: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 5;
    }

    const char *version = reinterpret_cast<const char *>(glGetString(GL_VERSION));
    const char *renderer = reinterpret_cast<const char *>(glGetString(GL_RENDERER));
    std::fprintf(stderr,
                 "shader load test: OpenGL %s / %s\n",
                 version ? version : "unknown",
                 renderer ? renderer : "unknown");

    ShaderPipelineV4 pipeline;
    std::string error;
    if (!pipeline.load(argv[1], error)) {
        std::fprintf(stderr, "shader load test: FAIL: %s\n", error.c_str());
        SDL_GL_DeleteContext(context);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 6;
    }

    std::fprintf(stderr, "shader load test: PASS '%s'\n", argv[1]);
    pipeline.clear();
    SDL_GL_DeleteContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
