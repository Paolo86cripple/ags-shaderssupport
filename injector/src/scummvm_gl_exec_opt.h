#pragma once

/*
 * AGS Shader Injector - ScummVM-inspired OpenGL execution cache
 *
 * The execution strategy in this file is adapted from ScummVM's OpenGL
 * LibRetroPipeline: discover static pass attribute/sampler plumbing once when a
 * GL program is first rendered, then reuse that plan instead of repeating GL
 * program introspection on every pass of every frame.
 *
 * ScummVM - Graphic Adventure Engine
 * ScummVM is the legal property of its developers, whose names are too
 * numerous to list here. Please refer to ScummVM's COPYRIGHT file.
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

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

/* retroarch_gl2_compat.h is force-included immediately before this file. Its
 * public draw wrapper does the right RetroArch coordinate math, but discovers
 * auxiliary attributes on every draw. Replace only that final draw wrapper;
 * all other RetroArch compatibility entry points remain untouched. */
#ifdef glDrawArrays
#undef glDrawArrays
#endif
#ifdef glDeleteProgram
#undef glDeleteProgram
#endif

namespace ags_shader_scummvm_opt {

struct AuxAttribute {
    GLuint location = 0;
    GLint sampler_location = -1;
    bool unit_texcoord = false;
};

struct ProgramPlan {
    GLuint program = 0;
    std::vector<AuxAttribute> attributes;
};

struct AuxCoordStorage {
    GLfloat values[8];
};

inline std::vector<ProgramPlan> &program_plans() {
    static std::vector<ProgramPlan> value;
    return value;
}

inline ProgramPlan *find_plan(GLuint program) {
    for (ProgramPlan &plan : program_plans())
        if (plan.program == program) return &plan;
    return nullptr;
}

inline bool ends_with_texcoord(const char *name) {
    if (!name) return false;
    static const char suffix[] = "TexCoord";
    const std::size_t suffix_length = sizeof(suffix) - 1;
    const std::size_t length = std::strlen(name);
    return length > suffix_length &&
           std::strcmp(name + length - suffix_length, suffix) == 0;
}

inline ProgramPlan &build_plan(GLuint program) {
    ProgramPlan fresh;
    fresh.program = program;

    GLint attribute_count = 0;
    GLint max_name_length = 0;
    ::glGetProgramiv(program, GL_ACTIVE_ATTRIBUTES, &attribute_count);
    ::glGetProgramiv(program, GL_ACTIVE_ATTRIBUTE_MAX_LENGTH, &max_name_length);

    if (attribute_count > 0 && max_name_length > 1) {
        std::vector<char> name(static_cast<std::size_t>(max_name_length), '\0');
        fresh.attributes.reserve(static_cast<std::size_t>(attribute_count));

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
            for (const AuxAttribute &entry : fresh.attributes) {
                if (entry.location == static_cast<GLuint>(location)) {
                    duplicate = true;
                    break;
                }
            }
            if (duplicate) continue;

            AuxAttribute entry;
            entry.location = static_cast<GLuint>(location);
            entry.unit_texcoord = std::strcmp(name.data(), "LUTTexCoord") == 0;

            if (!entry.unit_texcoord) {
                std::string sampler(name.data());
                sampler.replace(sampler.size() - 8, 8, "Texture");
                entry.sampler_location =
                    ags_shader_ra_gl2::get_uniform_location(program, sampler.c_str());
            }
            fresh.attributes.push_back(entry);
        }
    }

    program_plans().push_back(fresh);
    return program_plans().back();
}

inline ProgramPlan &plan_for(GLuint program) {
    ProgramPlan *existing = find_plan(program);
    return existing ? *existing : build_plan(program);
}

inline void texture_ratio(const ProgramPlan &plan,
                          const AuxAttribute &attribute,
                          GLfloat &xamt,
                          GLfloat &yamt) {
    xamt = 1.0f;
    yamt = 1.0f;
    if (attribute.unit_texcoord || attribute.sampler_location < 0) return;

    GLint unit = 0;
    ::glGetUniformiv(plan.program, attribute.sampler_location, &unit);
    const GLuint texture = ags_shader_ra_gl2::actual_bound_texture(unit);
    ags_shader_ra_gl2::TextureMeta *meta =
        ags_shader_ra_gl2::find_texture(texture);
    if (!meta || meta->texture_width <= 0 || meta->texture_height <= 0) return;

    xamt = static_cast<GLfloat>(meta->image_width) /
           static_cast<GLfloat>(meta->texture_width);
    yamt = static_cast<GLfloat>(meta->image_height) /
           static_cast<GLfloat>(meta->texture_height);
}

inline void draw_arrays(GLenum mode, GLint first, GLsizei count) {
    GLint current_program = 0;
    ::glGetIntegerv(GL_CURRENT_PROGRAM, &current_program);
    if (current_program <= 0) {
        ::glDrawArrays(mode, first, count);
        return;
    }

    ProgramPlan &plan = plan_for(static_cast<GLuint>(current_program));
    if (plan.attributes.empty()) {
        ::glDrawArrays(mode, first, count);
        return;
    }

    GLint old_array_buffer = 0;
    ::glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &old_array_buffer);

    std::vector<ags_shader_gl_compat::AuxAttribState> saved;
    std::vector<AuxCoordStorage> coords;
    saved.reserve(plan.attributes.size());
    coords.reserve(plan.attributes.size());

    for (const AuxAttribute &attribute : plan.attributes) {
        ags_shader_gl_compat::AuxAttribState state;
        ags_shader_gl_compat::save_aux_attrib(attribute.location, state);
        saved.push_back(state);

        GLfloat xamt = 1.0f;
        GLfloat yamt = 1.0f;
        texture_ratio(plan, attribute, xamt, yamt);

        AuxCoordStorage storage;
        storage.values[0] = 0.0f; storage.values[1] = 0.0f;
        storage.values[2] = xamt; storage.values[3] = 0.0f;
        storage.values[4] = 0.0f; storage.values[5] = yamt;
        storage.values[6] = xamt; storage.values[7] = yamt;
        coords.push_back(storage);

        ::glBindBuffer(GL_ARRAY_BUFFER, 0);
        ::glEnableVertexAttribArray(attribute.location);
        ::glVertexAttribPointer(attribute.location,
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

inline void delete_program(GLuint program) {
    std::vector<ProgramPlan> &plans = program_plans();
    plans.erase(std::remove_if(plans.begin(), plans.end(),
                               [program](const ProgramPlan &plan) {
                                   return plan.program == program;
                               }),
                plans.end());

    std::vector<ags_shader_ra_gl2::UniformMeta> &uniforms =
        ags_shader_ra_gl2::uniform_meta();
    uniforms.erase(std::remove_if(uniforms.begin(), uniforms.end(),
                                  [program](const ags_shader_ra_gl2::UniformMeta &item) {
                                      return item.program == program;
                                  }),
                   uniforms.end());

    std::vector<ags_shader_ra_gl2::ProgramStamp> &stamps =
        ags_shader_ra_gl2::program_stamps();
    stamps.erase(std::remove_if(stamps.begin(), stamps.end(),
                                [program](const ags_shader_ra_gl2::ProgramStamp &item) {
                                    return item.program == program;
                                }),
                 stamps.end());

    ::glDeleteProgram(program);
}

} // namespace ags_shader_scummvm_opt

#define glDrawArrays ags_shader_scummvm_opt::draw_arrays
#define glDeleteProgram ags_shader_scummvm_opt::delete_program
