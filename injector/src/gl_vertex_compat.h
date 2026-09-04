#pragma once

#include <GL/gl.h>
#include <GL/glext.h>

#include <cstring>
#include <vector>

namespace ags_shader_gl_compat {

inline void get_vertex_attrib_fv(GLuint index, GLenum pname, GLfloat *params) {
    if (index == 0 && pname == GL_CURRENT_VERTEX_ATTRIB) {
        if (params) {
            params[0] = 0.f;
            params[1] = 0.f;
            params[2] = 0.f;
            params[3] = 1.f;
        }
        return;
    }
    ::glGetVertexAttribfv(index, pname, params);
}

inline void vertex_attrib4fv(GLuint index, const GLfloat *values) {
    // In the legacy compatibility profile, generic attribute 0 has no
    // meaningful current value to preserve. Array state is restored separately.
    if (index == 0) return;
    ::glVertexAttrib4fv(index, values);
}

struct AuxAttribState {
    GLuint index = 0;
    GLint enabled = GL_FALSE;
    GLint size = 4;
    GLint type = GL_FLOAT;
    GLint normalized = GL_FALSE;
    GLint stride = 0;
    GLint buffer = 0;
    void *pointer = nullptr;
};

inline bool ends_with_texcoord(const char *name) {
    if (!name) return false;
    const std::size_t length = std::strlen(name);
    static const char suffix[] = "TexCoord";
    const std::size_t suffix_length = sizeof(suffix) - 1;
    return length > suffix_length &&
           std::strcmp(name + length - suffix_length, suffix) == 0;
}

inline void save_aux_attrib(GLuint index, AuxAttribState &state) {
    state.index = index;
    ::glGetVertexAttribiv(index, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &state.enabled);
    ::glGetVertexAttribiv(index, GL_VERTEX_ATTRIB_ARRAY_SIZE, &state.size);
    ::glGetVertexAttribiv(index, GL_VERTEX_ATTRIB_ARRAY_TYPE, &state.type);
    ::glGetVertexAttribiv(index, GL_VERTEX_ATTRIB_ARRAY_NORMALIZED, &state.normalized);
    ::glGetVertexAttribiv(index, GL_VERTEX_ATTRIB_ARRAY_STRIDE, &state.stride);
    ::glGetVertexAttribiv(index, GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING, &state.buffer);
    ::glGetVertexAttribPointerv(index, GL_VERTEX_ATTRIB_ARRAY_POINTER, &state.pointer);
}

inline void restore_aux_attrib(const AuxAttribState &state) {
    ::glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(state.buffer));
    if (state.enabled) {
        ::glVertexAttribPointer(state.index,
                                state.size,
                                static_cast<GLenum>(state.type),
                                static_cast<GLboolean>(state.normalized),
                                state.stride,
                                state.pointer);
        ::glEnableVertexAttribArray(state.index);
    }
    else {
        ::glDisableVertexAttribArray(state.index);
    }
}

inline void draw_arrays(GLenum mode, GLint first, GLsizei count) {
    GLint program = 0;
    ::glGetIntegerv(GL_CURRENT_PROGRAM, &program);
    if (program <= 0) {
        ::glDrawArrays(mode, first, count);
        return;
    }

    GLint attribute_count = 0;
    GLint max_name_length = 0;
    ::glGetProgramiv(static_cast<GLuint>(program), GL_ACTIVE_ATTRIBUTES, &attribute_count);
    ::glGetProgramiv(static_cast<GLuint>(program), GL_ACTIVE_ATTRIBUTE_MAX_LENGTH, &max_name_length);
    if (attribute_count <= 0 || max_name_length <= 1) {
        ::glDrawArrays(mode, first, count);
        return;
    }

    static const GLfloat quad_texcoords[8] = {
        0.f, 0.f,
        1.f, 0.f,
        0.f, 1.f,
        1.f, 1.f
    };

    GLint old_array_buffer = 0;
    ::glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &old_array_buffer);

    std::vector<char> name(static_cast<std::size_t>(max_name_length), '\0');
    std::vector<AuxAttribState> saved;
    saved.reserve(static_cast<std::size_t>(attribute_count));

    for (GLint i = 0; i < attribute_count; ++i) {
        GLsizei length = 0;
        GLint size = 0;
        GLenum type = 0;
        ::glGetActiveAttrib(static_cast<GLuint>(program),
                            static_cast<GLuint>(i),
                            max_name_length,
                            &length,
                            &size,
                            &type,
                            name.data());
        if (length <= 0) continue;
        name[static_cast<std::size_t>(length)] = '\0';

        // VertexCoord is position data and TexCoord/aTexCoord are already
        // provided by ShaderPipelineV4.  All other classic RetroArch semantic
        // coordinates (OrigTexCoord, PassNTexCoord, PassPrevNTexCoord,
        // PrevNTexCoord, FeedbackTexCoord, LUTTexCoord and alias TexCoords)
        // use the same normalized quad coordinates.
        if (!ends_with_texcoord(name.data()) ||
            std::strcmp(name.data(), "TexCoord") == 0 ||
            std::strcmp(name.data(), "aTexCoord") == 0)
            continue;

        const GLint location = ::glGetAttribLocation(static_cast<GLuint>(program), name.data());
        if (location < 0) continue;

        bool already_saved = false;
        for (const AuxAttribState &state : saved) {
            if (state.index == static_cast<GLuint>(location)) {
                already_saved = true;
                break;
            }
        }
        if (already_saved) continue;

        AuxAttribState state;
        save_aux_attrib(static_cast<GLuint>(location), state);
        saved.push_back(state);

        ::glBindBuffer(GL_ARRAY_BUFFER, 0);
        ::glEnableVertexAttribArray(static_cast<GLuint>(location));
        ::glVertexAttribPointer(static_cast<GLuint>(location),
                                2,
                                GL_FLOAT,
                                GL_FALSE,
                                0,
                                quad_texcoords);
    }

    ::glDrawArrays(mode, first, count);

    for (std::vector<AuxAttribState>::reverse_iterator it = saved.rbegin();
         it != saved.rend(); ++it)
        restore_aux_attrib(*it);
    ::glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(old_array_buffer));
}

} // namespace ags_shader_gl_compat

#define glGetVertexAttribfv ags_shader_gl_compat::get_vertex_attrib_fv
#define glVertexAttrib4fv ags_shader_gl_compat::vertex_attrib4fv
#define glDrawArrays ags_shader_gl_compat::draw_arrays
