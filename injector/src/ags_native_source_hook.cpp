#define _GNU_SOURCE 1

/*
 * AGS Shader Injector - native AGS OpenGL source discovery
 *
 * The host-side strategy is inspired by ScummVM's OpenGL LibRetroPipeline:
 * shader processing starts from the engine's logical game-resolution target
 * instead of the already-scaled window backbuffer. This implementation is
 * standalone and observes AGS' own OpenGL FBO traffic. Current AGS desktop
 * builds use GLAD's built-in libGL/glXGetProcAddressARB loader, while other
 * hosts may obtain entry points through SDL_GL_GetProcAddress; both paths are
 * covered here.
 *
 * ScummVM - Graphic Adventure Engine
 * ScummVM is the legal property of its developers, whose names are too
 * numerous to list here. Please refer to ScummVM's COPYRIGHT file.
 *
 * AGS adaptation Copyright (C) 2026 Paolo86cripple and contributors.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "ags_native_source_hook.h"

#include <GL/gl.h>
#include <GL/glext.h>
#include <SDL2/SDL.h>
#include <dlfcn.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#ifndef GL_DRAW_FRAMEBUFFER
#define GL_DRAW_FRAMEBUFFER 0x8CA9
#endif
#ifndef GL_READ_FRAMEBUFFER
#define GL_READ_FRAMEBUFFER 0x8CA8
#endif

namespace {
using DlsymFn = void *(*)(void *, const char *);
using GetProcAddressFn = void *(*)(const char *);
using GlxGetProcAddressFn = void *(*)(const unsigned char *);
using BindFramebufferFn = void (*)(GLenum, GLuint);
using FramebufferTexture2DFn = void (*)(GLenum, GLenum, GLenum, GLuint, GLint);
using ViewportFn = void (*)(GLint, GLint, GLsizei, GLsizei);
using DeleteFramebuffersFn = void (*)(GLsizei, const GLuint *);
using DeleteTexturesFn = void (*)(GLsizei, const GLuint *);

DlsymFn g_real_dlsym = nullptr;
GetProcAddressFn g_real_get_proc = nullptr;
GlxGetProcAddressFn g_real_glx_get_proc = nullptr;
BindFramebufferFn g_real_bind_framebuffer = nullptr;
FramebufferTexture2DFn g_real_framebuffer_texture_2d = nullptr;
ViewportFn g_real_viewport = nullptr;
DeleteFramebuffersFn g_real_delete_framebuffers = nullptr;
DeleteTexturesFn g_real_delete_textures = nullptr;

struct FboState {
    GLuint fbo = 0;
    GLuint color_texture = 0;
    int viewport_width = 0;
    int viewport_height = 0;
    unsigned long long last_used = 0;
};

std::vector<FboState> g_fbos;
GLuint g_draw_fbo = 0;
GLuint g_read_fbo = 0;
GLuint g_candidate_fbo = 0;
unsigned long long g_serial = 0;
bool g_pipeline_active = false;

DlsymFn resolve_real_dlsym() {
    if (!g_real_dlsym) {
#if defined(__GLIBC__)
        g_real_dlsym = reinterpret_cast<DlsymFn>(
            dlvsym(RTLD_NEXT, "dlsym", "GLIBC_2.2.5"));
#endif
    }
    return g_real_dlsym;
}

void resolve_real_get_proc() {
    if (g_real_get_proc) return;
    DlsymFn real_dlsym = resolve_real_dlsym();
    if (!real_dlsym) return;
    g_real_get_proc = reinterpret_cast<GetProcAddressFn>(
        real_dlsym(RTLD_NEXT, "SDL_GL_GetProcAddress"));
}

FboState *find_fbo(GLuint fbo) {
    for (FboState &state : g_fbos)
        if (state.fbo == fbo) return &state;
    return nullptr;
}

FboState &ensure_fbo(GLuint fbo) {
    FboState *state = find_fbo(fbo);
    if (state) return *state;
    FboState fresh;
    fresh.fbo = fbo;
    g_fbos.push_back(fresh);
    return g_fbos.back();
}

bool target_affects_draw(GLenum target) {
    return target == GL_FRAMEBUFFER || target == GL_DRAW_FRAMEBUFFER;
}

bool target_affects_read(GLenum target) {
    return target == GL_FRAMEBUFFER || target == GL_READ_FRAMEBUFFER;
}

bool native_source_enabled() {
    const char *value = std::getenv("AGS_SHADER_NATIVE_SOURCE");
    if (!value || !value[0]) return false;

    std::string mode(value);
    std::transform(mode.begin(), mode.end(), mode.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return mode == "1" || mode == "true" || mode == "on" || mode == "auto";
}

void query_texture_size(GLuint texture, int &width, int &height) {
    width = 0;
    height = 0;
    if (!texture || !SDL_GL_GetCurrentContext()) return;

    GLint old_active = GL_TEXTURE0;
    GLint old_texture = 0;
    glGetIntegerv(GL_ACTIVE_TEXTURE, &old_active);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &old_texture);
    glBindTexture(GL_TEXTURE_2D, texture);

    GLint w = 0;
    GLint h = 0;
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &w);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &h);

    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(old_texture));
    glActiveTexture(static_cast<GLenum>(old_active));
    width = w;
    height = h;
}

extern "C" void ags_shader_gl_bind_framebuffer(GLenum target, GLuint fbo) {
    if (g_real_bind_framebuffer)
        g_real_bind_framebuffer(target, fbo);

    if (g_pipeline_active) return;

    if (target_affects_draw(target)) {
        const GLuint previous = g_draw_fbo;
        g_draw_fbo = fbo;
        if (fbo) {
            FboState &state = ensure_fbo(fbo);
            state.last_used = ++g_serial;
        }
        else if (previous) {
            // AGS finishes its logical/native render target and binds the real
            // screen framebuffer before presenting that texture.
            g_candidate_fbo = previous;
        }
    }

    if (target_affects_read(target))
        g_read_fbo = fbo;
}

extern "C" void ags_shader_gl_framebuffer_texture_2d(GLenum target,
                                                       GLenum attachment,
                                                       GLenum textarget,
                                                       GLuint texture,
                                                       GLint level) {
    if (g_real_framebuffer_texture_2d)
        g_real_framebuffer_texture_2d(target, attachment, textarget, texture, level);

    if (g_pipeline_active || attachment != GL_COLOR_ATTACHMENT0 ||
        textarget != GL_TEXTURE_2D || level != 0)
        return;

    GLuint fbo = 0;
    if (target_affects_draw(target)) fbo = g_draw_fbo;
    else if (target_affects_read(target)) fbo = g_read_fbo;
    if (!fbo) return;

    FboState &state = ensure_fbo(fbo);
    state.color_texture = texture;
    state.last_used = ++g_serial;
}

extern "C" void ags_shader_gl_viewport(GLint x,
                                        GLint y,
                                        GLsizei width,
                                        GLsizei height) {
    if (g_real_viewport)
        g_real_viewport(x, y, width, height);

    if (g_pipeline_active || !g_draw_fbo || width <= 0 || height <= 0) return;
    FboState &state = ensure_fbo(g_draw_fbo);
    state.viewport_width = width;
    state.viewport_height = height;
    state.last_used = ++g_serial;
}

extern "C" void ags_shader_gl_delete_framebuffers(GLsizei count, const GLuint *fbos) {
    if (g_real_delete_framebuffers)
        g_real_delete_framebuffers(count, fbos);
    if (!fbos || count <= 0) return;

    for (GLsizei i = 0; i < count; ++i) {
        const GLuint dead = fbos[i];
        g_fbos.erase(std::remove_if(g_fbos.begin(), g_fbos.end(),
                                   [dead](const FboState &state) {
                                       return state.fbo == dead;
                                   }),
                     g_fbos.end());
        if (g_draw_fbo == dead) g_draw_fbo = 0;
        if (g_read_fbo == dead) g_read_fbo = 0;
        if (g_candidate_fbo == dead) g_candidate_fbo = 0;
    }
}

extern "C" void ags_shader_gl_delete_textures(GLsizei count, const GLuint *textures) {
    if (g_real_delete_textures)
        g_real_delete_textures(count, textures);
    if (!textures || count <= 0) return;

    for (FboState &state : g_fbos) {
        for (GLsizei i = 0; i < count; ++i) {
            if (state.color_texture == textures[i])
                state.color_texture = 0;
        }
    }
}

void *wrapped_proc(const char *name, void *real_proc) {
    // Preserve loader fallback behavior exactly. Missing core spellings stay
    // null so GLAD/SDL may retry EXT/OES aliases.
    if (!name || !real_proc) return real_proc;

    if (std::strcmp(name, "glBindFramebuffer") == 0 ||
        std::strcmp(name, "glBindFramebufferEXT") == 0) {
        g_real_bind_framebuffer = reinterpret_cast<BindFramebufferFn>(real_proc);
        return reinterpret_cast<void *>(&ags_shader_gl_bind_framebuffer);
    }
    if (std::strcmp(name, "glFramebufferTexture2D") == 0 ||
        std::strcmp(name, "glFramebufferTexture2DEXT") == 0) {
        g_real_framebuffer_texture_2d = reinterpret_cast<FramebufferTexture2DFn>(real_proc);
        return reinterpret_cast<void *>(&ags_shader_gl_framebuffer_texture_2d);
    }
    if (std::strcmp(name, "glViewport") == 0) {
        g_real_viewport = reinterpret_cast<ViewportFn>(real_proc);
        return reinterpret_cast<void *>(&ags_shader_gl_viewport);
    }
    if (std::strcmp(name, "glDeleteFramebuffers") == 0 ||
        std::strcmp(name, "glDeleteFramebuffersEXT") == 0) {
        g_real_delete_framebuffers = reinterpret_cast<DeleteFramebuffersFn>(real_proc);
        return reinterpret_cast<void *>(&ags_shader_gl_delete_framebuffers);
    }
    if (std::strcmp(name, "glDeleteTextures") == 0) {
        g_real_delete_textures = reinterpret_cast<DeleteTexturesFn>(real_proc);
        return reinterpret_cast<void *>(&ags_shader_gl_delete_textures);
    }
    return real_proc;
}

extern "C" void *ags_shader_glx_get_proc_address(const unsigned char *proc) {
    if (!g_real_glx_get_proc || !proc) return nullptr;
    void *real_proc = g_real_glx_get_proc(proc);
    return wrapped_proc(reinterpret_cast<const char *>(proc), real_proc);
}
} // namespace

/* GLAD's Linux loader calls dlsym(libGL, "glXGetProcAddressARB") and then uses
 * that returned resolver for every OpenGL entry point. LD_PRELOAD cannot
 * override a symbol obtained from an explicit libGL handle by itself, so
 * interpose dlsym narrowly: all symbols pass through unchanged except the GLX
 * resolver and the same GL functions we already observe through SDL. */
extern "C" void *dlsym(void *handle, const char *symbol) {
    DlsymFn real_dlsym = resolve_real_dlsym();
    if (!real_dlsym) return nullptr;

    void *real_proc = real_dlsym(handle, symbol);
    if (!symbol || !real_proc) return real_proc;

    if (std::strcmp(symbol, "glXGetProcAddressARB") == 0 ||
        std::strcmp(symbol, "glXGetProcAddress") == 0) {
        g_real_glx_get_proc = reinterpret_cast<GlxGetProcAddressFn>(real_proc);
        return reinterpret_cast<void *>(&ags_shader_glx_get_proc_address);
    }

    return wrapped_proc(symbol, real_proc);
}

extern "C" void *SDL_GL_GetProcAddress(const char *proc) {
    resolve_real_get_proc();
    if (!g_real_get_proc) return nullptr;
    void *real_proc = g_real_get_proc(proc);
    return wrapped_proc(proc, real_proc);
}

bool ags_native_source_acquire(int output_width,
                               int output_height,
                               AgsNativeSource &source) {
    source = AgsNativeSource();
    if (!native_source_enabled()) return false;

    const GLuint candidate = g_candidate_fbo;
    // Consume exactly once. Injector FBO traffic is ignored while its pipeline
    // is active, and the next AGS frame will publish a fresh candidate.
    g_candidate_fbo = 0;
    if (!candidate) return false;

    FboState *state = find_fbo(candidate);
    if (!state || !state->color_texture ||
        state->viewport_width <= 0 || state->viewport_height <= 0)
        return false;

    int texture_width = 0;
    int texture_height = 0;
    query_texture_size(state->color_texture, texture_width, texture_height);
    if (texture_width <= 0 || texture_height <= 0) return false;

    source.texture = state->color_texture;
    source.fbo = state->fbo;
    source.width = state->viewport_width;
    source.height = state->viewport_height;
    source.texture_width = texture_width;
    source.texture_height = texture_height;

    (void)output_width;
    (void)output_height;
    return true;
}

void ags_native_source_set_pipeline_active(bool active) {
    g_pipeline_active = active;
}
