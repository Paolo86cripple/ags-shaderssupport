#include "shader_pipeline_v4.h"

#include <SDL2/SDL.h>
#include <GL/gl.h>

#include <cstdio>
#include <string>

namespace {
struct StateProbe {
    bool enabled = false;
    GLuint sentinel_texture = 0;
    GLint max_units = 0;
    GLint expected_active = GL_TEXTURE0;
    GLint expected_texture0 = 0;
    GLint expected_texture3 = 0;
    GLboolean blend = GL_FALSE;
    GLboolean depth = GL_FALSE;
    GLboolean cull = GL_FALSE;
    GLboolean scissor = GL_FALSE;
    GLint attrib_enabled[3] = {0, 0, 0};
    void *attrib0_pointer = nullptr;
};

const GLfloat kProbeVertices[8] = {
    -0.5f, -0.5f,
     0.5f, -0.5f,
    -0.5f,  0.5f,
     0.5f,  0.5f
};

void setup_state_probe(StateProbe &probe) {
    probe.enabled = true;
    glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &probe.max_units);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &probe.expected_texture0);

    if (probe.max_units >= 4) {
        glGenTextures(1, &probe.sentinel_texture);
        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D, probe.sentinel_texture);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &probe.expected_texture3);
        probe.expected_active = GL_TEXTURE3;
    }
    else {
        glActiveTexture(GL_TEXTURE0);
        probe.expected_active = GL_TEXTURE0;
    }

    glEnable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, kProbeVertices);
    glDisableVertexAttribArray(1);
    glVertexAttrib4f(1, 0.25f, 0.5f, 0.75f, 1.0f);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 0, kProbeVertices);

    probe.blend = glIsEnabled(GL_BLEND);
    probe.depth = glIsEnabled(GL_DEPTH_TEST);
    probe.cull = glIsEnabled(GL_CULL_FACE);
    probe.scissor = glIsEnabled(GL_SCISSOR_TEST);
    for (GLuint i = 0; i < 3; ++i)
        glGetVertexAttribiv(i, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &probe.attrib_enabled[i]);
    glGetVertexAttribPointerv(0, GL_VERTEX_ATTRIB_ARRAY_POINTER, &probe.attrib0_pointer);
}

bool verify_state_probe(const StateProbe &probe, const char *where) {
    if (!probe.enabled) return true;

    bool ok = true;
    GLint active = 0;
    glGetIntegerv(GL_ACTIVE_TEXTURE, &active);
    if (active != probe.expected_active) ok = false;

    glActiveTexture(GL_TEXTURE0);
    GLint texture0 = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture0);
    if (texture0 != probe.expected_texture0) ok = false;

    if (probe.max_units >= 4) {
        glActiveTexture(GL_TEXTURE3);
        GLint texture3 = 0;
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture3);
        if (texture3 != probe.expected_texture3) ok = false;
    }
    glActiveTexture(static_cast<GLenum>(probe.expected_active));

    if (glIsEnabled(GL_BLEND) != probe.blend) ok = false;
    if (glIsEnabled(GL_DEPTH_TEST) != probe.depth) ok = false;
    if (glIsEnabled(GL_CULL_FACE) != probe.cull) ok = false;
    if (glIsEnabled(GL_SCISSOR_TEST) != probe.scissor) ok = false;

    for (GLuint i = 0; i < 3; ++i) {
        GLint enabled = 0;
        glGetVertexAttribiv(i, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &enabled);
        if (enabled != probe.attrib_enabled[i]) ok = false;
    }

    void *pointer = nullptr;
    glGetVertexAttribPointerv(0, GL_VERTEX_ATTRIB_ARRAY_POINTER, &pointer);
    if (pointer != probe.attrib0_pointer) ok = false;

    if (!ok)
        std::fprintf(stderr, "shader test: OpenGL state changed after %s\n", where);
    return ok;
}
}

int main(int argc, char **argv) {
    const char *shader = argc > 1 ? argv[1] : "shaders/identity.glsl";
    const std::string mode = argc > 2 ? argv[2] : "";
    const bool expect_invert = mode == "invert";
    const bool temporal_prev = mode == "prev";
    const bool temporal_feedback = mode == "feedback";
    const bool state_test = mode == "state";

    if (SDL_Init(SDL_INIT_VIDEO) != 0) return 2;
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    SDL_Window *window = SDL_CreateWindow("ags-shader-test",
                                          0, 0, 64, 64,
                                          SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    if (!window) {
        std::fprintf(stderr, "shader test: %s\n", SDL_GetError());
        SDL_Quit();
        return 3;
    }

    SDL_GLContext context = SDL_GL_CreateContext(window);
    if (!context) {
        std::fprintf(stderr, "shader test: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 4;
    }

    const char *version = reinterpret_cast<const char *>(glGetString(GL_VERSION));
    const char *renderer = reinterpret_cast<const char *>(glGetString(GL_RENDERER));
    std::fprintf(stderr,
                 "shader test: OpenGL %s / %s\n",
                 version ? version : "unknown",
                 renderer ? renderer : "unknown");

    GLuint source_texture = 0;
    glGenTextures(1, &source_texture);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, source_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    const unsigned char pixel_a[4] = {32, 64, 128, 255};
    glTexImage2D(GL_TEXTURE_2D,
                 0,
                 GL_RGBA,
                 1,
                 1,
                 0,
                 GL_RGBA,
                 GL_UNSIGNED_BYTE,
                 pixel_a);

    GLenum gl_error = glGetError();
    if (gl_error != GL_NO_ERROR) {
        std::fprintf(stderr, "shader test: texture setup GL error 0x%x\n", gl_error);
        return 5;
    }

    StateProbe probe;
    if (state_test) setup_state_probe(probe);

    ShaderPipelineV4 pipeline;
    std::string error;
    if (!pipeline.load(shader, error)) {
        std::fprintf(stderr, "shader test: load failed: %s\n", error.c_str());
        return 6;
    }
    if (!verify_state_probe(probe, "load")) return 10;

    std::fprintf(stderr, "shader test: pipeline loaded\n");
    pipeline.apply(source_texture, 1, 1, 64, 64);
    glFinish();
    if (!verify_state_probe(probe, "apply")) return 11;

    if (temporal_prev || temporal_feedback) {
        const unsigned char pixel_b[4] = {10, 20, 30, 255};
        GLint old_active = GL_TEXTURE0;
        glGetIntegerv(GL_ACTIVE_TEXTURE, &old_active);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, source_texture);
        glTexSubImage2D(GL_TEXTURE_2D,
                        0,
                        0,
                        0,
                        1,
                        1,
                        GL_RGBA,
                        GL_UNSIGNED_BYTE,
                        pixel_b);
        glActiveTexture(static_cast<GLenum>(old_active));

        pipeline.apply(source_texture, 1, 1, 64, 64);
        glFinish();
        if (!verify_state_probe(probe, "temporal apply")) return 12;
    }

    gl_error = glGetError();
    if (gl_error != GL_NO_ERROR) {
        std::fprintf(stderr, "shader test: pipeline GL error 0x%x\n", gl_error);
        return 7;
    }

    unsigned char result[4] = {0, 0, 0, 0};
    glReadPixels(32, 32, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, result);
    gl_error = glGetError();
    if (gl_error != GL_NO_ERROR) {
        std::fprintf(stderr, "shader test: readback GL error 0x%x\n", gl_error);
        return 8;
    }

    unsigned char expected[4] = {32, 64, 128, 255};
    if (expect_invert || temporal_feedback) {
        expected[0] = 223;
        expected[1] = 191;
        expected[2] = 127;
    }

    const bool ok =
        result[0] == expected[0] &&
        result[1] == expected[1] &&
        result[2] == expected[2] &&
        result[3] == expected[3];

    if (ok) {
        std::fprintf(stderr,
                     "shader test: PASS (%u,%u,%u,%u)\n",
                     result[0], result[1], result[2], result[3]);
    }
    else {
        std::fprintf(stderr,
                     "shader test: FAIL got (%u,%u,%u,%u), expected (%u,%u,%u,%u)\n",
                     result[0], result[1], result[2], result[3],
                     expected[0], expected[1], expected[2], expected[3]);
    }

    pipeline.clear();
    if (probe.sentinel_texture) glDeleteTextures(1, &probe.sentinel_texture);
    glDeleteTextures(1, &source_texture);
    SDL_GL_DeleteContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return ok ? 0 : 9;
}
