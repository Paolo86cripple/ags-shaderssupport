#include "retroarch_source_bridge.h"

#include <GL/gl.h>
#include <GL/glext.h>
#include <SDL2/SDL.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
using GenFramebuffersFn = void (*)(GLsizei, GLuint *);
using DeleteFramebuffersFn = void (*)(GLsizei, const GLuint *);
using BindFramebufferFn = void (*)(GLenum, GLuint);
using FramebufferTexture2DFn = void (*)(GLenum, GLenum, GLenum, GLuint, GLint);
using CheckFramebufferStatusFn = GLenum (*)(GLenum);

GenFramebuffersFn p_gen_framebuffers = nullptr;
DeleteFramebuffersFn p_delete_framebuffers = nullptr;
BindFramebufferFn p_bind_framebuffer = nullptr;
FramebufferTexture2DFn p_framebuffer_texture_2d = nullptr;
CheckFramebufferStatusFn p_check_framebuffer_status = nullptr;

GLuint g_bridge_texture = 0;
GLuint g_read_fbo = 0;
int g_logical_width = 0;
int g_logical_height = 0;

bool debug_enabled() {
    const char *value = std::getenv("AGS_SHADER_DEBUG");
    return value && value[0] && std::strcmp(value, "0") != 0;
}

void *gl_proc(const char *core_name, const char *ext_name = nullptr) {
    void *proc = SDL_GL_GetProcAddress(core_name);
    if (!proc && ext_name) proc = SDL_GL_GetProcAddress(ext_name);
    return proc;
}

bool resolve_fbo_functions() {
    if (p_gen_framebuffers && p_delete_framebuffers && p_bind_framebuffer &&
        p_framebuffer_texture_2d && p_check_framebuffer_status)
        return true;

    p_gen_framebuffers = reinterpret_cast<GenFramebuffersFn>(
        gl_proc("glGenFramebuffers", "glGenFramebuffersEXT"));
    p_delete_framebuffers = reinterpret_cast<DeleteFramebuffersFn>(
        gl_proc("glDeleteFramebuffers", "glDeleteFramebuffersEXT"));
    p_bind_framebuffer = reinterpret_cast<BindFramebufferFn>(
        gl_proc("glBindFramebuffer", "glBindFramebufferEXT"));
    p_framebuffer_texture_2d = reinterpret_cast<FramebufferTexture2DFn>(
        gl_proc("glFramebufferTexture2D", "glFramebufferTexture2DEXT"));
    p_check_framebuffer_status = reinterpret_cast<CheckFramebufferStatusFn>(
        gl_proc("glCheckFramebufferStatus", "glCheckFramebufferStatusEXT"));

    return p_gen_framebuffers && p_delete_framebuffers && p_bind_framebuffer &&
           p_framebuffer_texture_2d && p_check_framebuffer_status;
}

void forget_texture(GLuint texture) {
    if (!texture) return;

    std::vector<ags_shader_ra_gl2::TextureMeta> &all = ags_shader_ra_gl2::texture_meta();
    all.erase(std::remove_if(all.begin(), all.end(),
                             [texture](const ags_shader_ra_gl2::TextureMeta &meta) {
                                 return meta.texture == texture;
                             }),
              all.end());

    GLuint *bindings = ags_shader_ra_gl2::tracked_bindings();
    for (int i = 0; i < ags_shader_ra_gl2::MaxTrackedTextureUnits; ++i)
        if (bindings[i] == texture) bindings[i] = 0;
}

void destroy_bridge() {
    if (!SDL_GL_GetCurrentContext()) {
        g_bridge_texture = 0;
        g_read_fbo = 0;
        g_logical_width = 0;
        g_logical_height = 0;
        return;
    }

    if (g_read_fbo && resolve_fbo_functions())
        p_delete_framebuffers(1, &g_read_fbo);
    g_read_fbo = 0;

    if (g_bridge_texture) {
        forget_texture(g_bridge_texture);
        ::glDeleteTextures(1, &g_bridge_texture);
    }
    g_bridge_texture = 0;
    g_logical_width = 0;
    g_logical_height = 0;
}
}

void ags_ra_source_bridge_release() {
    destroy_bridge();
}

bool ags_ra_source_bridge_prepare(unsigned input_texture,
                                  int logical_width,
                                  int logical_height,
                                  unsigned &output_texture,
                                  int &backing_width,
                                  int &backing_height) {
    output_texture = input_texture;
    backing_width = logical_width;
    backing_height = logical_height;

    if (!input_texture || logical_width <= 0 || logical_height <= 0)
        return false;
    if (!resolve_fbo_functions()) return false;

    GLint old_active = GL_TEXTURE0;
    GLint old_texture = 0;
    GLint old_fbo = 0;
    ::glGetIntegerv(GL_ACTIVE_TEXTURE, &old_active);
    ::glActiveTexture(GL_TEXTURE0);
    ::glGetIntegerv(GL_TEXTURE_BINDING_2D, &old_texture);
    ::glGetIntegerv(GL_FRAMEBUFFER_BINDING, &old_fbo);

    const bool recreate = !g_bridge_texture || !g_read_fbo ||
                          g_logical_width != logical_width ||
                          g_logical_height != logical_height;
    if (recreate) {
        destroy_bridge();

        g_logical_width = logical_width;
        g_logical_height = logical_height;

        ::glGenTextures(1, &g_bridge_texture);
        glBindTexture(GL_TEXTURE_2D, g_bridge_texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        // retroarch_gl2_compat.h expands this logical allocation to the same
        // power-of-two backing convention used by intermediate render targets.
        glTexImage2D(GL_TEXTURE_2D,
                     0,
                     GL_RGBA,
                     logical_width,
                     logical_height,
                     0,
                     GL_RGBA,
                     GL_UNSIGNED_BYTE,
                     nullptr);

        p_gen_framebuffers(1, &g_read_fbo);
    }

    while (::glGetError() != GL_NO_ERROR) {}

    p_bind_framebuffer(GL_FRAMEBUFFER, g_read_fbo);
    p_framebuffer_texture_2d(GL_FRAMEBUFFER,
                             GL_COLOR_ATTACHMENT0,
                             GL_TEXTURE_2D,
                             static_cast<GLuint>(input_texture),
                             0);
    bool ok = p_check_framebuffer_status(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;

    if (ok) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, g_bridge_texture);
        ::glCopyTexSubImage2D(GL_TEXTURE_2D,
                              0,
                              0,
                              0,
                              0,
                              0,
                              logical_width,
                              logical_height);
        ok = ::glGetError() == GL_NO_ERROR;
    }

    p_bind_framebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(old_fbo));
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(old_texture));
    glActiveTexture(static_cast<GLenum>(old_active));

    if (!ok) {
        if (debug_enabled())
            std::fprintf(stderr,
                         "AGS shader: RetroArch source bridge copy failed; using external texture directly\n");
        return false;
    }

    ags_shader_ra_gl2::TextureMeta *meta =
        ags_shader_ra_gl2::find_texture(g_bridge_texture);
    if (meta) {
        backing_width = meta->texture_width;
        backing_height = meta->texture_height;
    }

    if (debug_enabled() && recreate) {
        std::fprintf(stderr,
                     "AGS shader: RetroArch source bridge logical=%dx%d backing=%dx%d\n",
                     logical_width,
                     logical_height,
                     backing_width,
                     backing_height);
    }

    output_texture = g_bridge_texture;
    return true;
}
