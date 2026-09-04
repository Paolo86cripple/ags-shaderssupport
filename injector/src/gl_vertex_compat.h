#pragma once

#include <GL/gl.h>
#include <GL/glext.h>

#include <cstring>
#include <string>
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
    if (index == 0) return;
    ::glVertexAttrib4fv(index, values);
}

inline const char *capability_defines() {
    return
        "#ifndef _HAS_ORIGINALASPECT_UNIFORMS\n"
        "#define _HAS_ORIGINALASPECT_UNIFORMS\n"
        "#endif\n"
        "#ifndef _HAS_FRAMETIME_UNIFORMS\n"
        "#define _HAS_FRAMETIME_UNIFORMS\n"
        "#endif\n"
        "#ifndef _HAS_SENSOR_UNIFORMS\n"
        "#define _HAS_SENSOR_UNIFORMS\n"
        "#endif\n";
}

inline std::string fragment_hlsl_type_aliases(const std::string &source) {
    // Some historical Libretro GLSL ports place their HLSL-style type alias
    // block only inside the VERTEX branch, then use float2/float3/float4 from
    // the FRAGMENT branch as well. Only opt in when the source itself declares
    // that convention; ordinary GLSL shaders remain untouched.
    const bool fragment_stage = source.find("#define FRAGMENT") != std::string::npos;
    const bool declares_hlsl_types = source.find("#define float2 vec2") != std::string::npos;
    if (!fragment_stage || !declares_hlsl_types) return {};

    return
        "#ifndef float2\n#define float2 vec2\n#endif\n"
        "#ifndef float3\n#define float3 vec3\n#endif\n"
        "#ifndef float4\n#define float4 vec4\n#endif\n"
        "#ifndef int2\n#define int2 ivec2\n#endif\n"
        "#ifndef int3\n#define int3 ivec3\n#endif\n"
        "#ifndef int4\n#define int4 ivec4\n#endif\n"
        "#ifndef bool2\n#define bool2 bvec2\n#endif\n"
        "#ifndef bool3\n#define bool3 bvec3\n#endif\n"
        "#ifndef bool4\n#define bool4 bvec4\n#endif\n"
        "#ifndef float2x2\n#define float2x2 mat2x2\n#endif\n"
        "#ifndef float2x3\n#define float2x3 mat2x3\n#endif\n"
        "#ifndef float3x3\n#define float3x3 mat3x3\n#endif\n"
        "#ifndef float4x4\n#define float4x4 mat4x4\n#endif\n"
        "#ifndef float4x2\n#define float4x2 mat4x2\n#endif\n";
}

struct ShaderSourceState {
    GLuint shader = 0;
    std::string source;
    bool explicit_version = false;
};

inline std::vector<ShaderSourceState> &shader_sources() {
    static std::vector<ShaderSourceState> states;
    return states;
}

inline ShaderSourceState *find_shader_source(GLuint shader) {
    std::vector<ShaderSourceState> &states = shader_sources();
    for (ShaderSourceState &state : states)
        if (state.shader == shader) return &state;
    return nullptr;
}

inline std::string compatibility_defines(const std::string &source) {
    return std::string(capability_defines()) + fragment_hlsl_type_aliases(source);
}

inline std::string with_capabilities(const std::string &source,
                                     const char *version_prefix = nullptr,
                                     bool enable_420pack = false) {
    std::string patched = source;
    const std::string compat = compatibility_defines(source);
    const std::size_t version = patched.find("#version");
    if (version != std::string::npos) {
        const std::size_t end = patched.find('\n', version);
        std::string insert;
        if (enable_420pack)
            insert += "#extension GL_ARB_shading_language_420pack : enable\n";
        insert += compat;
        if (end != std::string::npos)
            patched.insert(end + 1, insert);
        else
            patched += std::string("\n") + insert;
        return patched;
    }

    std::string prefix;
    if (version_prefix) prefix += version_prefix;
    if (enable_420pack)
        prefix += "#extension GL_ARB_shading_language_420pack : enable\n";
    prefix += compat;
    return prefix + patched;
}

inline void submit_shader_source(GLuint shader, const std::string &source) {
    const GLchar *patched = source.c_str();
    ::glShaderSource(shader, 1, &patched, nullptr);
}

inline void shader_source(GLuint shader,
                          GLsizei count,
                          const GLchar *const *strings,
                          const GLint *lengths) {
    std::string source;
    for (GLsizei i = 0; i < count; ++i) {
        if (!strings || !strings[i]) continue;
        if (lengths && lengths[i] >= 0)
            source.append(strings[i], static_cast<std::size_t>(lengths[i]));
        else
            source.append(strings[i]);
    }

    ShaderSourceState *state = find_shader_source(shader);
    if (!state) {
        ShaderSourceState fresh;
        fresh.shader = shader;
        shader_sources().push_back(fresh);
        state = &shader_sources().back();
    }
    state->source = source;
    state->explicit_version = source.find("#version") != std::string::npos;

    submit_shader_source(shader, with_capabilities(source));
}

inline void compile_shader(GLuint shader) {
    ::glCompileShader(shader);

    GLint ok = GL_FALSE;
    ::glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (ok == GL_TRUE) return;

    ShaderSourceState *state = find_shader_source(shader);
    if (!state || state->explicit_version) return;

    submit_shader_source(shader,
                         with_capabilities(state->source, "#version 130\n"));
    ::glCompileShader(shader);
    ::glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (ok == GL_TRUE) return;

    submit_shader_source(shader,
                         with_capabilities(state->source,
                                           "#version 130\n",
                                           true));
    ::glCompileShader(shader);
}

inline void delete_shader(GLuint shader) {
    std::vector<ShaderSourceState> &states = shader_sources();
    for (std::vector<ShaderSourceState>::iterator it = states.begin(); it != states.end(); ++it) {
        if (it->shader == shader) {
            states.erase(it);
            break;
        }
    }
    ::glDeleteShader(shader);
}

inline void set_uniform_1i(GLuint program, const char *name, GLint value) {
    const GLint location = ::glGetUniformLocation(program, name);
    if (location >= 0) ::glUniform1i(location, value);
}

inline void set_uniform_1f(GLuint program, const char *name, GLfloat value) {
    const GLint location = ::glGetUniformLocation(program, name);
    if (location >= 0) ::glUniform1f(location, value);
}

inline void set_uniform_2f(GLuint program, const char *name, GLfloat x, GLfloat y) {
    const GLint location = ::glGetUniformLocation(program, name);
    if (location >= 0) ::glUniform2f(location, x, y);
}

inline void set_uniform_3f(GLuint program,
                           const char *name,
                           GLfloat x,
                           GLfloat y,
                           GLfloat z) {
    const GLint location = ::glGetUniformLocation(program, name);
    if (location >= 0) ::glUniform3f(location, x, y, z);
}

inline void bind_texture(GLenum target, GLuint texture) {
    ::glBindTexture(target, texture);
    if (target != GL_TEXTURE_2D || texture == 0) return;

    GLint current_program = 0;
    ::glGetIntegerv(GL_CURRENT_PROGRAM, &current_program);
    if (current_program <= 0) return;
    const GLuint program = static_cast<GLuint>(current_program);

    GLint viewport[4] = {0, 0, 0, 0};
    ::glGetIntegerv(GL_VIEWPORT, viewport);
    set_uniform_2f(program,
                   "FinalViewportSize",
                   static_cast<GLfloat>(viewport[2]),
                   static_cast<GLfloat>(viewport[3]));

    set_uniform_1i(program, "FrameTimeDelta", 16667);
    set_uniform_1f(program, "OriginalFPS", 60.f);
    set_uniform_1i(program, "Rotation", 0);

    GLint width = 0;
    GLint height = 0;
    ::glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &width);
    ::glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &height);
    const GLfloat aspect = height > 0
        ? static_cast<GLfloat>(width) / static_cast<GLfloat>(height)
        : 1.f;
    set_uniform_1f(program, "OriginalAspect", aspect);
    set_uniform_1f(program, "OriginalAspectRotated", aspect);

    set_uniform_3f(program, "Gyroscope", 0.f, 0.f, 0.f);
    set_uniform_3f(program, "Accelerometer", 0.f, 0.f, 0.f);
    set_uniform_3f(program, "AccelerometerRest", 0.f, 0.f, 0.f);
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
#define glShaderSource ags_shader_gl_compat::shader_source
#define glCompileShader ags_shader_gl_compat::compile_shader
#define glDeleteShader ags_shader_gl_compat::delete_shader
#define glBindTexture ags_shader_gl_compat::bind_texture
#define glDrawArrays ags_shader_gl_compat::draw_arrays
