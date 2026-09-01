#include "shader_pipeline_v2.h"
#include <SDL2/SDL.h>
#include <GL/gl.h>
#include <cstdio>
#include <cstdlib>

int main(int argc, char **argv)
{
    const char *shader = argc > 1 ? argv[1] : "shaders/invert.glsl";
    if (SDL_Init(SDL_INIT_VIDEO) != 0) return 2;
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
    SDL_Window *window = SDL_CreateWindow("ags-shader-test", 0, 0, 64, 64, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    if (!window) { SDL_Quit(); return 3; }
    SDL_GLContext context = SDL_GL_CreateContext(window);
    if (!context) { SDL_DestroyWindow(window); SDL_Quit(); return 4; }

    GLuint source_texture = 0, source_fbo = 0;
    glGenTextures(1, &source_texture);
    glBindTexture(GL_TEXTURE_2D, source_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    unsigned char pixel[4] = { 32, 64, 128, 255 };
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    glGenFramebuffers(1, &source_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, source_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, source_texture, 0);

    ShaderPipelineV2 pipeline;
    std::string error;
    if (!pipeline.load(shader, error)) { std::fprintf(stderr, "%s\n", error.c_str()); return 5; }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    pipeline.apply(source_texture, 1, 1, 64, 64);
    glFinish();

    glDeleteFramebuffers(1, &source_fbo);
    glDeleteTextures(1, &source_texture);
    SDL_GL_DeleteContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
