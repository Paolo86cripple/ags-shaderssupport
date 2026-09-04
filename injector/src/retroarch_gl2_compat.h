#pragma once

/*
 * AGS Shader Injector - RetroArch GL2 compatibility/renderchain adapter
 *
 * Adapted from RetroArch gfx/drivers/gl2.c and
 * gfx/drivers_shader/shader_glsl.c.
 *
 * RetroArch - A frontend for libretro.
 * Copyright (C) 2010-2014 Hans-Kristian Arntzen
 * Copyright (C) 2011-2017 Daniel De Matteis
 * Copyright (C) 2012-2015 Michael Lelli
 * Copyright (C) 2016-2019 Brad Parker
 *
 * AGS adaptation Copyright (C) 2026 Paolo86cripple and contributors.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option)
 * any later version.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <GL/gl.h>
#include <GL/glext.h>
#include <SDL2/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

/* gl_vertex_compat.h is force-included before this file. Replace only the GL
 * entry points for which this adapter provides a stricter RetroArch-style
 * implementation. Shader-source compatibility remains owned by that header. */
#ifdef glBindTexture
#undef glBindTexture
#endif
#ifdef glDrawArrays
#undef glDrawArrays
#endif

namespace ags_shader_ra_gl2 {

constexpr int MaxTrackedTextureUnits = 64;

struct TextureMeta {
    GLuint texture = 0;
    GLint image_width = 0;
    GLint image_height = 0;
    GLint texture_width = 0;
    GLint texture_height = 0;
    GLint min_filter = -1;
    GLint mag_filter = -1;
    GLint wrap_s = -1;
    GLint wrap_t = -1;
};

struct UniformMeta {
    GLuint program = 0;
    GLint location = -1;
    std::string name;
};

struct ProgramStamp {
    GLuint program = 0;
    unsigned long long frame = 0;
};

struct FrameState {
    bool active = false;
    bool ending = false;
    bool source_seen = false;
    GLint final_viewport[4] = {0, 0, 0, 0};
    float original_aspect = 1.0f;
    unsigned long long serial = 0;
    Uint64 last_counter = 0;
    GLint frame_time_delta_us = 16667;
};

inline std::vector<TextureMeta> &texture_meta() {
    static std::vector<TextureMeta> value;
    return value;
}

inline std::vector<UniformMeta> &uniform_meta() {
    static std::vector<UniformMeta> value;
    return value;
}

inline std::vector<ProgramStamp> &program_stamps() {
    static std::vector<ProgramStamp> value;
    return value;
}

inline FrameState &frame_state() {
    static FrameState value;
    return value;
}

inline GLint &active_texture_unit() {
    static GLint value = 0;
    return value;
}

inline GLuint *tracked_bindings() {
    static GLuint value[MaxTrackedTextureUnits] = {0};
    return value;
}

inline GLint next_pow2(GLint value) {
    if (value <= 1) return 1;
    unsigned v = static_cast<unsigned>(value - 1);
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    return static_cast<GLint>(v + 1);
}

inline TextureMeta *find_texture(GLuint texture) {
    if (!texture) return nullptr;
    std::vector<TextureMeta> &all = texture_meta();
    for (TextureMeta &item : all)
        if (item.texture == texture) return &item;
    return nullptr;
}

inline TextureMeta &ensure_texture(GLuint texture) {
    TextureMeta *existing = find_texture(texture);
    if (existing) return *existing;
    TextureMeta fresh;
    fresh.texture = texture;
    texture_meta().push_back(fresh);
    return texture_meta().back();
}

inline const UniformMeta *find_uniform(GLuint program, GLint location) {
    const std::vector<UniformMeta> &all = uniform_meta();
    for (const UniformMeta &item : all)
        if (item.program == program && item.location == location) return &item;
    return nullptr;
}

inline const UniformMeta *find_uniform(GLuint program, const char *name) {
    const std::vector<UniformMeta> &all = uniform_meta();
    for (const UniformMeta &item : all)
        if (item.program == program && item.name == name) return &item;
    return nullptr;
}

inline bool is_pipeline_program(GLuint program) {
    if (!program) return false;
    const std::vector<UniformMeta> &all = uniform_meta();
    for (const UniformMeta &item : all)
        if (item.program == program) return true;
    return false;
}

inline GLint get_uniform_location(GLuint program, const char *name) {
    if (!name) return -1;
    const UniformMeta *cached = find_uniform(program, name);
    if (cached) return cached->location;

    const GLint location = ::glGetUniformLocation(program, name);
    UniformMeta item;
    item.program = program;
    item.location = location;
    item.name = name;
    uniform_meta().push_back(item);
    return location;
}

inline bool ends_with(const std::string &value, const char *suffix) {
    const std::size_t n = std::strlen(suffix);
    return value.size() >= n &&
           value.compare(value.size() - n, n, suffix) == 0;
}

inline GLuint actual_bound_texture(GLint unit) {
    if (unit < 0 || unit >= MaxTrackedTextureUnits) return 0;
    if (tracked_bindings()[unit]) return tracked_bindings()[unit];

    GLint old_active = GL_TEXTURE0;
    GLint texture = 0;
    ::glGetIntegerv(GL_ACTIVE_TEXTURE, &old_active);
    ::glActiveTexture(GL_TEXTURE0 + unit);
    ::glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture);
    ::glActiveTexture(static_cast<GLenum>(old_active));
    return static_cast<GLuint>(texture);
}

inline void query_texture_size(GLuint texture, GLint &width, GLint &height) {
    width = 0;
    height = 0;
    if (!texture) return;

    TextureMeta *meta = find_texture(texture);
    if (meta && meta->texture_width > 0 && meta->texture_height > 0) {
        width = meta->texture_width;
        height = meta->texture_height;
        return;
    }

    GLint old_active = GL_TEXTURE0;
    GLint old_binding = 0;
    ::glGetIntegerv(GL_ACTIVE_TEXTURE, &old_active);
    ::glActiveTexture(GL_TEXTURE0);
    ::glGetIntegerv(GL_TEXTURE_BINDING_2D, &old_binding);
    ::glBindTexture(GL_TEXTURE_2D, texture);
    ::glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &width);
    ::glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &height);
    ::glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(old_binding));
    ::glActiveTexture(static_cast<GLenum>(old_active));
}

inline void active_texture(GLenum texture) {
    ::glActiveTexture(texture);
    const GLint unit = static_cast<GLint>(texture - GL_TEXTURE0);
    if (unit >= 0 && unit < MaxTrackedTextureUnits)
        active_texture_unit() = unit;
}

inline void begin_frame_if_needed() {
    FrameState &state = frame_state();
    if (state.active) return;

    ::glGetIntegerv(GL_VIEWPORT, state.final_viewport);
    state.active = true;
    state.ending = false;
    state.source_seen = false;
    state.original_aspect = state.final_viewport[3] > 0
        ? static_cast<float>(state.final_viewport[2]) /
          static_cast<float>(state.final_viewport[3])
        : 1.0f;
    ++state.serial;

    const Uint64 now = SDL_GetPerformanceCounter();
    const Uint64 frequency = SDL_GetPerformanceFrequency();
    if (state.last_counter && frequency) {
        const Uint64 delta = now - state.last_counter;
        const double micros = static_cast<double>(delta) * 1000000.0 /
                              static_cast<double>(frequency);
        state.frame_time_delta_us = static_cast<GLint>(
            std::max(1.0, std::min(micros, 1000000.0)));
    }
    state.last_counter = now;
}

inline void viewport(GLint x, GLint y, GLsizei width, GLsizei height) {
    begin_frame_if_needed();
    ::glViewport(x, y, width, height);

    FrameState &state = frame_state();
    if (state.ending) {
        state.active = false;
        state.ending = false;
    }
}

inline void use_program(GLuint program) {
    FrameState &state = frame_state();
    if (state.active && !is_pipeline_program(program))
        state.ending = true;
    ::glUseProgram(program);
}

inline bool program_globals_already_set(GLuint program) {
    const unsigned long long serial = frame_state().serial;
    std::vector<ProgramStamp> &all = program_stamps();
    for (ProgramStamp &item : all) {
        if (item.program != program) continue;
        if (item.frame == serial) return true;
        item.frame = serial;
        return false;
    }
    ProgramStamp fresh;
    fresh.program = program;
    fresh.frame = serial;
    all.push_back(fresh);
    return false;
}

inline void set_uniform_1i(GLuint program, const char *name, GLint value) {
    const GLint location = get_uniform_location(program, name);
    if (location >= 0) ::glUniform1i(location, value);
}

inline void set_uniform_1f(GLuint program, const char *name, GLfloat value) {
    const GLint location = get_uniform_location(program, name);
    if (location >= 0) ::glUniform1f(location, value);
}

inline void set_uniform_2f(GLuint program, const char *name, GLfloat x, GLfloat y) {
    const GLint location = get_uniform_location(program, name);
    if (location >= 0) ::glUniform2f(location, x, y);
}

inline void set_uniform_3f(GLuint program,
                           const char *name,
                           GLfloat x,
                           GLfloat y,
                           GLfloat z) {
    const GLint location = get_uniform_location(program, name);
    if (location >= 0) ::glUniform3f(location, x, y, z);
}

inline void apply_retroarch_globals(GLuint program) {
    if (!program || program_globals_already_set(program)) return;

    const FrameState &state = frame_state();
    set_uniform_2f(program,
                   "FinalViewportSize",
                   static_cast<GLfloat>(state.final_viewport[2]),
                   static_cast<GLfloat>(state.final_viewport[3]));
    set_uniform_1i(program, "FrameTimeDelta", state.frame_time_delta_us);
    set_uniform_1f(program, "OriginalFPS", 60.0f);
    set_uniform_1i(program, "Rotation", 0);
    set_uniform_1f(program, "OriginalAspect", state.original_aspect);
    set_uniform_1f(program, "OriginalAspectRotated", state.original_aspect);
    set_uniform_3f(program, "Gyroscope", 0.0f, 0.0f, 0.0f);
    set_uniform_3f(program, "Accelerometer", 0.0f, 0.0f, 0.0f);
    set_uniform_3f(program, "AccelerometerRest", 0.0f, 0.0f, 0.0f);
}

inline void bind_texture(GLenum target, GLuint texture) {
    ::glBindTexture(target, texture);
    if (target != GL_TEXTURE_2D) return;

    const GLint unit = active_texture_unit();
    if (unit >= 0 && unit < MaxTrackedTextureUnits)
        tracked_bindings()[unit] = texture;

    if (!texture) return;

    FrameState &state = frame_state();
    if (state.active && unit == 0 && !state.source_seen && !find_texture(texture)) {
        GLint width = 0;
        GLint height = 0;
        query_texture_size(texture, width, height);
        if (width > 0 && height > 0) {
            state.original_aspect = static_cast<float>(width) /
                                    static_cast<float>(height);
            state.source_seen = true;
        }
    }

    GLint current_program = 0;
    ::glGetIntegerv(GL_CURRENT_PROGRAM, &current_program);
    if (current_program > 0)
        apply_retroarch_globals(static_cast<GLuint>(current_program));
}

inline void tex_parameter_i(GLenum target, GLenum pname, GLint param) {
    if (target != GL_TEXTURE_2D) {
        ::glTexParameteri(target, pname, param);
        return;
    }

    const GLint unit = active_texture_unit();
    const GLuint texture = unit >= 0 && unit < MaxTrackedTextureUnits
        ? tracked_bindings()[unit]
        : 0;
    if (!texture) {
        ::glTexParameteri(target, pname, param);
        return;
    }

    TextureMeta &meta = ensure_texture(texture);
    GLint *cached = nullptr;
    switch (pname) {
        case GL_TEXTURE_MIN_FILTER: cached = &meta.min_filter; break;
        case GL_TEXTURE_MAG_FILTER: cached = &meta.mag_filter; break;
        case GL_TEXTURE_WRAP_S: cached = &meta.wrap_s; break;
        case GL_TEXTURE_WRAP_T: cached = &meta.wrap_t; break;
        default: break;
    }

    if (cached && *cached == param) return;
    ::glTexParameteri(target, pname, param);
    if (cached) *cached = param;
}

inline void tex_image_2d(GLenum target,
                         GLint level,
                         GLint internal_format,
                         GLsizei width,
                         GLsizei height,
                         GLint border,
                         GLenum format,
                         GLenum type,
                         const void *pixels) {
    if (target != GL_TEXTURE_2D || level != 0 || pixels || width <= 0 || height <= 0) {
        ::glTexImage2D(target,
                       level,
                       internal_format,
                       width,
                       height,
                       border,
                       format,
                       type,
                       pixels);
        return;
    }

    GLint max_size = 0;
    ::glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_size);
    GLint texture_width = next_pow2(width);
    GLint texture_height = next_pow2(height);
    if (max_size > 0) {
        texture_width = std::min(texture_width, max_size);
        texture_height = std::min(texture_height, max_size);
    }

    GLint ra_internal_format = internal_format;
#ifdef GL_RGBA32F
#ifdef GL_RGBA16F_ARB
    if (internal_format == GL_RGBA16F_ARB) ra_internal_format = GL_RGBA32F;
#endif
#endif
#ifdef GL_RGBA8
    if (internal_format == GL_RGBA) ra_internal_format = GL_RGBA8;
#endif

    ::glTexImage2D(target,
                   level,
                   ra_internal_format,
                   texture_width,
                   texture_height,
                   border,
                   format,
                   type,
                   pixels);

    const GLint unit = active_texture_unit();
    const GLuint texture = unit >= 0 && unit < MaxTrackedTextureUnits
        ? tracked_bindings()[unit]
        : 0;
    if (texture) {
        TextureMeta &meta = ensure_texture(texture);
        meta.image_width = width;
        meta.image_height = height;
        meta.texture_width = texture_width;
        meta.texture_height = texture_height;
    }
}

inline TextureMeta *meta_matching_image_size(GLfloat x, GLfloat y) {
    const GLint width = static_cast<GLint>(std::lround(x));
    const GLint height = static_cast<GLint>(std::lround(y));
    std::vector<TextureMeta> &all = texture_meta();
    for (TextureMeta &meta : all) {
        if (meta.image_width == width && meta.image_height == height)
            return &meta;
    }
    return nullptr;
}

inline void uniform_2f(GLint location, GLfloat x, GLfloat y) {
    GLint current_program = 0;
    ::glGetIntegerv(GL_CURRENT_PROGRAM, &current_program);
    const UniformMeta *uniform = current_program > 0
        ? find_uniform(static_cast<GLuint>(current_program), location)
        : nullptr;

    if (uniform && ends_with(uniform->name, "TextureSize")) {
        TextureMeta *meta = nullptr;
        if (uniform->name == "TextureSize")
            meta = find_texture(actual_bound_texture(0));
        if (!meta)
            meta = meta_matching_image_size(x, y);
        if (meta && meta->texture_width > 0 && meta->texture_height > 0) {
            x = static_cast<GLfloat>(meta->texture_width);
            y = static_cast<GLfloat>(meta->texture_height);
        }
    }
    else if (uniform && uniform->name == "FinalViewportSize") {
        const FrameState &state = frame_state();
        x = static_cast<GLfloat>(state.final_viewport[2]);
        y = static_cast<GLfloat>(state.final_viewport[3]);
    }

    ::glUniform2f(location, x, y);
}

inline void override_frame_texture_sizes(GLuint program,
                                         const std::string &sampler_name,
                                         GLuint texture) {
    TextureMeta *meta = find_texture(texture);
    if (!meta || meta->image_width <= 0 || meta->image_height <= 0) return;

    std::string base = sampler_name.substr(0, sampler_name.size() - 7);
    const GLint input_size = get_uniform_location(program, (base + "InputSize").c_str());
    if (input_size >= 0)
        ::glUniform2f(input_size,
                      static_cast<GLfloat>(meta->image_width),
                      static_cast<GLfloat>(meta->image_height));

    const GLint texture_size = get_uniform_location(program, (base + "TextureSize").c_str());
    if (texture_size >= 0)
        ::glUniform2f(texture_size,
                      static_cast<GLfloat>(meta->texture_width),
                      static_cast<GLfloat>(meta->texture_height));
}

inline void uniform_1i(GLint location, GLint value) {
    ::glUniform1i(location, value);

    GLint current_program = 0;
    ::glGetIntegerv(GL_CURRENT_PROGRAM, &current_program);
    if (current_program <= 0) return;

    const GLuint program = static_cast<GLuint>(current_program);
    const UniformMeta *uniform = find_uniform(program, location);
    if (!uniform || !ends_with(uniform->name, "Texture")) return;
    if (value < 0 || value >= MaxTrackedTextureUnits) return;

    const GLuint texture = actual_bound_texture(value);
    if (texture)
        override_frame_texture_sizes(program, uniform->name, texture);
}

inline void uniform_1f(GLint location, GLfloat value) {
    ::glUniform1f(location, value);
}

inline void uniform_3f(GLint location, GLfloat x, GLfloat y, GLfloat z) {
    ::glUniform3f(location, x, y, z);
}

inline void vertex_attrib_pointer(GLuint index,
                                  GLint size,
                                  GLenum type,
                                  GLboolean normalized,
                                  GLsizei stride,
                                  const void *pointer) {
    if (index == 1 && size == 2 && type == GL_FLOAT) {
        TextureMeta *meta = find_texture(actual_bound_texture(0));
        if (meta && meta->texture_width > 0 && meta->texture_height > 0) {
            static GLfloat texcoords[8];
            const GLfloat xamt = static_cast<GLfloat>(meta->image_width) /
                                 static_cast<GLfloat>(meta->texture_width);
            const GLfloat yamt = static_cast<GLfloat>(meta->image_height) /
                                 static_cast<GLfloat>(meta->texture_height);
            texcoords[0] = 0.0f; texcoords[1] = 0.0f;
            texcoords[2] = xamt; texcoords[3] = 0.0f;
            texcoords[4] = 0.0f; texcoords[5] = yamt;
            texcoords[6] = xamt; texcoords[7] = yamt;
            ::glVertexAttribPointer(index,
                                    2,
                                    GL_FLOAT,
                                    normalized,
                                    0,
                                    texcoords);
            return;
        }
    }

    ::glVertexAttribPointer(index, size, type, normalized, stride, pointer);
}

inline bool ends_with_texcoord(const char *name) {
    if (!name) return false;
    const std::size_t length = std::strlen(name);
    static const char suffix[] = "TexCoord";
    const std::size_t suffix_length = sizeof(suffix) - 1;
    return length > suffix_length &&
           std::strcmp(name + length - suffix_length, suffix) == 0;
}

inline void texture_ratio_for_attribute(GLuint program,
                                        const char *attribute_name,
                                        GLfloat &xamt,
                                        GLfloat &yamt) {
    xamt = 1.0f;
    yamt = 1.0f;
    if (!attribute_name || std::strcmp(attribute_name, "LUTTexCoord") == 0)
        return;

    std::string sampler(attribute_name);
    if (!ends_with(sampler, "TexCoord")) return;
    sampler.replace(sampler.size() - 8, 8, "Texture");

    const GLint sampler_location = get_uniform_location(program, sampler.c_str());
    if (sampler_location < 0) return;

    GLint unit = 0;
    ::glGetUniformiv(program, sampler_location, &unit);
    const GLuint texture = actual_bound_texture(unit);
    TextureMeta *meta = find_texture(texture);
    if (!meta || meta->texture_width <= 0 || meta->texture_height <= 0) return;

    xamt = static_cast<GLfloat>(meta->image_width) /
           static_cast<GLfloat>(meta->texture_width);
    yamt = static_cast<GLfloat>(meta->image_height) /
           static_cast<GLfloat>(meta->texture_height);
}

struct AuxCoordStorage {
    GLfloat values[8];
};

inline void draw_arrays(GLenum mode, GLint first, GLsizei count) {
    GLint current_program = 0;
    ::glGetIntegerv(GL_CURRENT_PROGRAM, &current_program);
    if (current_program <= 0) {
        ::glDrawArrays(mode, first, count);
        return;
    }

    const GLuint program = static_cast<GLuint>(current_program);
    GLint attribute_count = 0;
    GLint max_name_length = 0;
    ::glGetProgramiv(program, GL_ACTIVE_ATTRIBUTES, &attribute_count);
    ::glGetProgramiv(program, GL_ACTIVE_ATTRIBUTE_MAX_LENGTH, &max_name_length);
    if (attribute_count <= 0 || max_name_length <= 1) {
        ::glDrawArrays(mode, first, count);
        return;
    }

    GLint old_array_buffer = 0;
    ::glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &old_array_buffer);

    std::vector<char> name(static_cast<std::size_t>(max_name_length), '\0');
    std::vector<ags_shader_gl_compat::AuxAttribState> saved;
    std::vector<AuxCoordStorage> coords;
    saved.reserve(static_cast<std::size_t>(attribute_count));
    coords.reserve(static_cast<std::size_t>(attribute_count));

    for (GLint i = 0; i < attribute_count; ++i) {
        GLsizei length = 0;
        GLint size = 0;
        GLenum type = 0;
        ::glGetActiveAttrib(program,
                            static_cast<GLuint>(i),
                            max_name_length,
                            &length,
                            &size,
                            &type,
                            name.data());
        if (length <= 0) continue;
        name[static_cast<std::size_t>(length)] = '\0';

        if (!ends_with_texcoord(name.data()) ||
            std::strcmp(name.data(), "TexCoord") == 0 ||
            std::strcmp(name.data(), "aTexCoord") == 0)
            continue;

        const GLint location = ::glGetAttribLocation(program, name.data());
        if (location < 0) continue;

        bool duplicate = false;
        for (const ags_shader_gl_compat::AuxAttribState &state : saved) {
            if (state.index == static_cast<GLuint>(location)) {
                duplicate = true;
                break;
            }
        }
        if (duplicate) continue;

        ags_shader_gl_compat::AuxAttribState state;
        ags_shader_gl_compat::save_aux_attrib(static_cast<GLuint>(location), state);
        saved.push_back(state);

        GLfloat xamt = 1.0f;
        GLfloat yamt = 1.0f;
        texture_ratio_for_attribute(program, name.data(), xamt, yamt);

        AuxCoordStorage storage;
        storage.values[0] = 0.0f; storage.values[1] = 0.0f;
        storage.values[2] = xamt; storage.values[3] = 0.0f;
        storage.values[4] = 0.0f; storage.values[5] = yamt;
        storage.values[6] = xamt; storage.values[7] = yamt;
        coords.push_back(storage);

        ::glBindBuffer(GL_ARRAY_BUFFER, 0);
        ::glEnableVertexAttribArray(static_cast<GLuint>(location));
        ::glVertexAttribPointer(static_cast<GLuint>(location),
                                2,
                                GL_FLOAT,
                                GL_FALSE,
                                0,
                                coords.back().values);
    }

    ::glDrawArrays(mode, first, count);

    for (std::vector<ags_shader_gl_compat::AuxAttribState>::reverse_iterator it = saved.rbegin();
         it != saved.rend(); ++it)
        ags_shader_gl_compat::restore_aux_attrib(*it);
    ::glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(old_array_buffer));
}

} // namespace ags_shader_ra_gl2

#define glActiveTexture ags_shader_ra_gl2::active_texture
#define glBindTexture ags_shader_ra_gl2::bind_texture
#define glTexParameteri ags_shader_ra_gl2::tex_parameter_i
#define glTexImage2D ags_shader_ra_gl2::tex_image_2d
#define glGetUniformLocation ags_shader_ra_gl2::get_uniform_location
#define glUniform1i ags_shader_ra_gl2::uniform_1i
#define glUniform1f ags_shader_ra_gl2::uniform_1f
#define glUniform2f ags_shader_ra_gl2::uniform_2f
#define glUniform3f ags_shader_ra_gl2::uniform_3f
#define glVertexAttribPointer ags_shader_ra_gl2::vertex_attrib_pointer
#define glViewport ags_shader_ra_gl2::viewport
#define glUseProgram ags_shader_ra_gl2::use_program
#define glDrawArrays ags_shader_ra_gl2::draw_arrays
