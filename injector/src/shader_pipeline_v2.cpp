#include "shader_pipeline_v2.h"

#include <GL/gl.h>
#include <GL/glext.h>
#include <SDL2/SDL.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

using GenFramebuffersProc = void (*)(GLsizei, GLuint *);
using DeleteFramebuffersProc = void (*)(GLsizei, const GLuint *);
using BindFramebufferProc = void (*)(GLenum, GLuint);
using FramebufferTexture2DProc = void (*)(GLenum, GLenum, GLenum, GLuint, GLint);
using CheckFramebufferStatusProc = GLenum (*)(GLenum);

GenFramebuffersProc pGenFramebuffers = nullptr;
DeleteFramebuffersProc pDeleteFramebuffers = nullptr;
BindFramebufferProc pBindFramebuffer = nullptr;
FramebufferTexture2DProc pFramebufferTexture2D = nullptr;
CheckFramebufferStatusProc pCheckFramebufferStatus = nullptr;

const char *kVertex =
    "#version 120\n"
    "attribute vec2 aPosition;\n"
    "attribute vec2 aTexCoord;\n"
    "varying vec2 vTexCoord;\n"
    "void main() {\n"
    "    vTexCoord = aTexCoord;\n"
    "    gl_Position = vec4(aPosition, 0.0, 1.0);\n"
    "}\n";

struct Vertex {
    float x, y, u, v;
};

const Vertex kQuad[] = {
    { -1.f, -1.f, 0.f, 0.f },
    {  1.f, -1.f, 1.f, 0.f },
    { -1.f,  1.f, 0.f, 1.f },
    {  1.f,  1.f, 1.f, 1.f }
};

std::string trim(std::string s)
{
    const size_t first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos)
        return {};
    const size_t last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1);
}

std::string lower(std::string s)
{
    for (char &c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool suffix(const std::string &s, const char *ext)
{
    const size_t n = std::strlen(ext);
    return s.size() >= n && lower(s.substr(s.size() - n)) == lower(ext);
}

std::string parent_dir(const std::string &p)
{
    const size_t n = p.find_last_of("/\\");
    return n == std::string::npos ? "." : p.substr(0, n);
}

std::string join_path(const std::string &dir, std::string path)
{
    path = trim(path);
    if (!path.empty() && path.front() == '/')
        return path;
    if (path.size() > 1 && path[1] == ':')
        return path;
    return dir + "/" + path;
}

std::string unquote(std::string s)
{
    s = trim(s);
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"')
        s = s.substr(1, s.size() - 2);
    return s;
}

bool parse_bool_value(const std::string &s, bool fallback)
{
    const std::string v = lower(unquote(s));
    if (v == "true" || v == "1") return true;
    if (v == "false" || v == "0") return false;
    return fallback;
}

float parse_float_value(const std::string &s, float fallback)
{
    char *end = nullptr;
    const std::string v = unquote(s);
    const float value = std::strtof(v.c_str(), &end);
    return end && *end == '\0' ? value : fallback;
}

int parse_int_value(const std::string &s, int fallback)
{
    char *end = nullptr;
    const std::string v = unquote(s);
    const long value = std::strtol(v.c_str(), &end, 10);
    return end && *end == '\0' ? static_cast<int>(value) : fallback;
}

bool load_fbo_functions(std::string &error)
{
    if (pGenFramebuffers && pDeleteFramebuffers && pBindFramebuffer &&
        pFramebufferTexture2D && pCheckFramebufferStatus)
        return true;

    pGenFramebuffers = reinterpret_cast<GenFramebuffersProc>(SDL_GL_GetProcAddress("glGenFramebuffersEXT"));
    pDeleteFramebuffers = reinterpret_cast<DeleteFramebuffersProc>(SDL_GL_GetProcAddress("glDeleteFramebuffersEXT"));
    pBindFramebuffer = reinterpret_cast<BindFramebufferProc>(SDL_GL_GetProcAddress("glBindFramebufferEXT"));
    pFramebufferTexture2D = reinterpret_cast<FramebufferTexture2DProc>(SDL_GL_GetProcAddress("glFramebufferTexture2DEXT"));
    pCheckFramebufferStatus = reinterpret_cast<CheckFramebufferStatusProc>(SDL_GL_GetProcAddress("glCheckFramebufferStatusEXT"));

    if (!pGenFramebuffers || !pDeleteFramebuffers || !pBindFramebuffer ||
        !pFramebufferTexture2D || !pCheckFramebufferStatus)
    {
        error = "required EXT_framebuffer_object functions are unavailable";
        return false;
    }
    return true;
}

std::string make_combined_stage(const std::string &source, const char *stage_define)
{
    // This follows the compatibility idea used by current ScummVM OpenGL shaders:
    // the same source may contain VERTEX/FRAGMENT sections and is compiled twice.
    std::string defines;
    defines += "#define ";
    defines += stage_define;
    defines += "\n";
    defines += "#if __VERSION__ < 130\n#define in varying\n#define out varying\n#define texture texture2D\n#endif\n";

    const size_t version = source.find("#version");
    if (version != std::string::npos)
    {
        const size_t end = source.find('\n', version);
        if (end != std::string::npos)
            return source.substr(0, end + 1) + defines + source.substr(end + 1);
    }
    return defines + source;
}

bool is_combined(const std::string &source)
{
    return source.find("defined(VERTEX)") != std::string::npos ||
           source.find("defined(FRAGMENT)") != std::string::npos;
}

void log_gl_error(const char *where)
{
    const GLenum error = glGetError();
    if (error != GL_NO_ERROR)
        std::fprintf(stderr, "AGS shader: OpenGL error 0x%04x after %s\n", error, where);
}

}

ShaderPipelineV2::~ShaderPipelineV2()
{
    clear();
}

void ShaderPipelineV2::destroy_target(Target &target)
{
    if (target.fbo && pDeleteFramebuffers)
        pDeleteFramebuffers(1, &target.fbo);
    if (target.texture)
        glDeleteTextures(1, &target.texture);
    target = Target();
}

void ShaderPipelineV2::clear()
{
    for (auto &pass : _passes)
    {
        if (pass.program)
            glDeleteProgram(pass.program);
    }
    _passes.clear();

    if (pDeleteFramebuffers)
    {
        destroy_target(_targets[0]);
        destroy_target(_targets[1]);
    }
    else
    {
        _targets[0] = Target();
        _targets[1] = Target();
    }
    _frame_count = 0;
}

bool ShaderPipelineV2::has_suffix(const std::string &s, const std::string &ext)
{
    return suffix(s, ext.c_str());
}

std::string ShaderPipelineV2::parent_dir(const std::string &s)
{
    return ::parent_dir(s);
}

std::string ShaderPipelineV2::join_path(const std::string &a, const std::string &b)
{
    return ::join_path(a, b);
}

std::string ShaderPipelineV2::trim(std::string s)
{
    return ::trim(s);
}

std::string ShaderPipelineV2::lower(std::string s)
{
    return ::lower(s);
}

bool ShaderPipelineV2::parse_bool(const std::string &s, bool d)
{
    return parse_bool_value(s, d);
}

float ShaderPipelineV2::parse_float(const std::string &s, float d)
{
    return parse_float_value(s, d);
}

int ShaderPipelineV2::parse_int(const std::string &s, int d)
{
    return parse_int_value(s, d);
}

std::string ShaderPipelineV2::make_stage_source(const std::string &s, const char *n)
{
    return make_combined_stage(s, n);
}

bool ShaderPipelineV2::is_combined_shader(const std::string &s)
{
    return is_combined(s);
}

int ShaderPipelineV2::resolve_dimension(ScaleType type, float scale,
                                        int source, int viewport)
{
    switch (type)
    {
    case ScaleType::Viewport:
        return std::max(1, static_cast<int>(std::lround(viewport * scale)));
    case ScaleType::Absolute:
        return std::max(1, static_cast<int>(std::lround(scale)));
    default:
        return std::max(1, static_cast<int>(std::lround(source * scale)));
    }
}

bool ShaderPipelineV2::load_text(const std::string &path, std::string &text,
                                 std::string &error) const
{
    std::ifstream file(path.c_str(), std::ios::binary);
    if (!file)
    {
        error = "cannot read shader: " + path;
        return false;
    }
    std::ostringstream stream;
    stream << file.rdbuf();
    text = stream.str();
    if (text.empty())
    {
        error = "empty shader: " + path;
        return false;
    }
    return true;
}

bool ShaderPipelineV2::compile_shader(unsigned type, const std::string &source,
                                      unsigned &shader, std::string &error) const
{
    shader = glCreateShader(static_cast<GLenum>(type));
    if (!shader)
    {
        error = "glCreateShader failed";
        return false;
    }

    const char *text = source.c_str();
    glShaderSource(shader, 1, &text, nullptr);
    glCompileShader(shader);

    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (ok == GL_TRUE)
        return true;

    GLint size = 0;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &size);
    std::vector<char> log(static_cast<size_t>(std::max(size, 1)));
    if (size > 0)
        glGetShaderInfoLog(shader, size, nullptr, log.data());
    error = log.data();
    glDeleteShader(shader);
    shader = 0;
    return false;
}

bool ShaderPipelineV2::create_program(const std::string &vertex_source,
                                      const std::string &fragment_source,
                                      unsigned &program, std::string &error) const
{
    unsigned vs = 0, fs = 0;
    if (!compile_shader(GL_VERTEX_SHADER, vertex_source, vs, error))
        return false;
    if (!compile_shader(GL_FRAGMENT_SHADER, fragment_source, fs, error))
    {
        glDeleteShader(vs);
        return false;
    }

    program = glCreateProgram();
    if (!program)
    {
        glDeleteShader(vs);
        glDeleteShader(fs);
        error = "glCreateProgram failed";
        return false;
    }

    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glBindAttribLocation(program, 0, "aPosition");
    glBindAttribLocation(program, 1, "aTexCoord");
    glLinkProgram(program);

    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint linked = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (linked == GL_TRUE)
        return true;

    GLint size = 0;
    glGetProgramiv(program, GL_INFO_LOG_LENGTH, &size);
    std::vector<char> log(static_cast<size_t>(std::max(size, 1)));
    if (size > 0)
        glGetProgramInfoLog(program, size, nullptr, log.data());
    error = log.data();
    glDeleteProgram(program);
    program = 0;
    return false;
}

bool ShaderPipelineV2::add_pass(const std::string &path, const Pass *preset,
                                std::string &error)
{
    std::string source;
    if (!load_text(path, source, error))
        return false;

    Pass pass;
    if (preset)
        pass = *preset;
    pass.source_path = path;

    std::string vertex_source;
    std::string fragment_source;
    if (is_combined(source))
    {
        vertex_source = make_combined_stage(source, "VERTEX");
        fragment_source = make_combined_stage(source, "FRAGMENT");
    }
    else
    {
        vertex_source = kVertex;
        fragment_source = source;
    }

    if (!create_program(vertex_source, fragment_source, pass.program, error))
        return false;

    pass.texture = glGetUniformLocation(pass.program, "uTexture");
    if (pass.texture < 0)
        pass.texture = glGetUniformLocation(pass.program, "Texture");

    pass.input_size = glGetUniformLocation(pass.program, "uInputSize");
    pass.texture_size = glGetUniformLocation(pass.program, "TextureSize");
    pass.output_size = glGetUniformLocation(pass.program, "uOutputSize");
    if (pass.output_size < 0)
        pass.output_size = glGetUniformLocation(pass.program, "OutputSize");

    pass.frame_count = glGetUniformLocation(pass.program, "uFrameCount");
    if (pass.frame_count < 0)
        pass.frame_count = glGetUniformLocation(pass.program, "FrameCount");
    pass.frame_direction = glGetUniformLocation(pass.program, "FrameDirection");
    pass.original_size = glGetUniformLocation(pass.program, "uOriginalSize");
    pass.texel_size = glGetUniformLocation(pass.program, "uTexelSize");
    pass.time = glGetUniformLocation(pass.program, "uTime");

    _passes.push_back(pass);
    return true;
}

bool ShaderPipelineV2::parse_chain(const std::string &path,
                                   std::vector<std::string> &out,
                                   std::string &error) const
{
    std::string source;
    if (!load_text(path, source, error))
        return false;

    const std::string dir = parent_dir(path);
    std::istringstream stream(source);
    std::string line;
    while (std::getline(stream, line))
    {
        line = trim(line);
        if (line.empty() || line.front() == '#')
            continue;
        if (line.compare(0, 5, "pass=") == 0)
        {
            const std::string pass = unquote(line.substr(5));
            if (!pass.empty())
                out.push_back(join_path(dir, pass));
        }
    }

    if (out.empty())
    {
        error = "shader chain has no passes: " + path;
        return false;
    }
    return true;
}

bool ShaderPipelineV2::parse_glslp(const std::string &path,
                                    std::vector<Pass> &out,
                                    std::string &error) const
{
    std::string source;
    if (!load_text(path, source, error))
        return false;

    struct Entry
    {
        std::string path;
        Pass pass;
    };

    const std::string dir = parent_dir(path);
    std::istringstream stream(source);
    std::string line;
    int shader_count = -1;
    std::vector<Entry> entries;

    while (std::getline(stream, line))
    {
        line = trim(line);
        if (line.empty() || line.front() == '#')
            continue;

        const size_t eq = line.find('=');
        if (eq == std::string::npos)
            continue;

        const std::string key = trim(line.substr(0, eq));
        const std::string value = unquote(line.substr(eq + 1));

        if (key == "shaders")
        {
            shader_count = parse_int(value, -1);
            continue;
        }

        if (key.rfind("shader", 0) == 0 && key.size() > 6 &&
            std::isdigit(static_cast<unsigned char>(key[6])))
        {
            const int index = parse_int(key.substr(6), -1);
            if (index >= 0)
            {
                if (static_cast<size_t>(index) >= entries.size())
                    entries.resize(static_cast<size_t>(index) + 1);
                entries[index].path = join_path(dir, value);
            }
            continue;
        }

        size_t end_digits = key.size();
        while (end_digits > 0 && std::isdigit(static_cast<unsigned char>(key[end_digits - 1])))
            --end_digits;
        if (end_digits == key.size())
            continue;

        const int index = parse_int(key.substr(end_digits), -1);
        if (index < 0)
            continue;
        if (static_cast<size_t>(index) >= entries.size())
            entries.resize(static_cast<size_t>(index) + 1);

        const std::string base = key.substr(0, end_digits);
        if (base == "filter_linear")
            entries[index].pass.filter_linear = parse_bool(value, true);
        else if (base == "scale")
            entries[index].pass.scale_x = entries[index].pass.scale_y = parse_float(value, 1.0f);
        else if (base == "scale_x")
            entries[index].pass.scale_x = parse_float(value, 1.0f);
        else if (base == "scale_y")
            entries[index].pass.scale_y = parse_float(value, 1.0f);
        else if (base == "scale_type")
        {
            const std::string type = lower(value);
            const ScaleType scale_type = type == "viewport" ? ScaleType::Viewport :
                                          type == "absolute" ? ScaleType::Absolute :
                                          ScaleType::Source;
            entries[index].pass.scale_type_x = scale_type;
            entries[index].pass.scale_type_y = scale_type;
        }
        else if (base == "scale_type_x")
        {
            const std::string type = lower(value);
            entries[index].pass.scale_type_x = type == "viewport" ? ScaleType::Viewport :
                                               type == "absolute" ? ScaleType::Absolute :
                                               ScaleType::Source;
        }
        else if (base == "scale_type_y")
        {
            const std::string type = lower(value);
            entries[index].pass.scale_type_y = type == "viewport" ? ScaleType::Viewport :
                                               type == "absolute" ? ScaleType::Absolute :
                                               ScaleType::Source;
        }
    }

    if (shader_count < 0)
        shader_count = static_cast<int>(entries.size());
    if (shader_count <= 0)
    {
        error = "glslp has no shaders: " + path;
        return false;
    }

    out.clear();
    for (int i = 0; i < shader_count; ++i)
    {
        if (i >= static_cast<int>(entries.size()) || entries[i].path.empty())
        {
            error = "missing shader" + std::to_string(i) + " in " + path;
            return false;
        }
        entries[i].pass.source_path = entries[i].path;
        out.push_back(entries[i].pass);
    }
    return true;
}

bool ShaderPipelineV2::load(const std::string &path, std::string &error)
{
    clear();

    if (has_suffix(path, ".glslp"))
    {
        std::vector<Pass> presets;
        if (!parse_glslp(path, presets, error))
            return false;
        for (const Pass &preset : presets)
        {
            if (!add_pass(preset.source_path, &preset, error))
            {
                clear();
                return false;
            }
        }
    }
    else if (has_suffix(path, ".agschain"))
    {
        std::vector<std::string> paths;
        if (!parse_chain(path, paths, error))
            return false;
        for (const std::string &shader : paths)
        {
            if (!add_pass(shader, nullptr, error))
            {
                clear();
                return false;
            }
        }
    }
    else if (!add_pass(path, nullptr, error))
    {
        clear();
        return false;
    }

    return true;
}

bool ShaderPipelineV2::ensure_target(Target &target, int width, int height,
                                     bool linear, std::string &error)
{
    if (!load_fbo_functions(error))
        return false;

    if (target.fbo && target.width == width && target.height == height)
    {
        glBindTexture(GL_TEXTURE_2D, target.texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, linear ? GL_LINEAR : GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, linear ? GL_LINEAR : GL_NEAREST);
        return true;
    }

    destroy_target(target);
    target.width = width;
    target.height = height;

    glGenTextures(1, &target.texture);
    glBindTexture(GL_TEXTURE_2D, target.texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, linear ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, linear ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    pGenFramebuffers(1, &target.fbo);
    pBindFramebuffer(GL_FRAMEBUFFER_EXT, target.fbo);
    pFramebufferTexture2D(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT,
                          GL_TEXTURE_2D, target.texture, 0);

    if (pCheckFramebufferStatus(GL_FRAMEBUFFER_EXT) != GL_FRAMEBUFFER_COMPLETE_EXT)
    {
        error = "incomplete shader framebuffer";
        destroy_target(target);
        pBindFramebuffer(GL_FRAMEBUFFER_EXT, 0);
        return false;
    }

    return true;
}

void ShaderPipelineV2::apply(unsigned input_texture, int iw, int ih,
                             int ow, int oh)
{
    if (_passes.empty() || input_texture == 0u || iw <= 0 || ih <= 0 || ow <= 0 || oh <= 0)
        return;

    GLint old_program = 0;
    GLint old_array_buffer = 0;
    GLint old_active_texture = GL_TEXTURE0;
    GLint old_texture = 0;
    GLint old_framebuffer = 0;
    GLint old_read_buffer = GL_BACK;
    GLint old_viewport[4] = {};

    glGetIntegerv(GL_CURRENT_PROGRAM, &old_program);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &old_array_buffer);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &old_active_texture);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &old_texture);
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &old_framebuffer);
    glGetIntegerv(GL_READ_BUFFER, &old_read_buffer);
    glGetIntegerv(GL_VIEWPORT, old_viewport);

    const GLboolean blend_enabled = glIsEnabled(GL_BLEND);
    const GLboolean depth_enabled = glIsEnabled(GL_DEPTH_TEST);
    const GLboolean cull_enabled = glIsEnabled(GL_CULL_FACE);
    const GLboolean scissor_enabled = glIsEnabled(GL_SCISSOR_TEST);

    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);

    unsigned texture = input_texture;
    int source_width = iw;
    int source_height = ih;

    for (size_t i = 0; i < _passes.size(); ++i)
    {
        const Pass &pass = _passes[i];
        const bool last = i + 1 == _passes.size();

        int width = ow;
        int height = oh;
        if (!last)
        {
            width = resolve_dimension(pass.scale_type_x, pass.scale_x, source_width, ow);
            height = resolve_dimension(pass.scale_type_y, pass.scale_y, source_height, oh);
        }

        GLuint framebuffer = 0u;
        if (!last)
        {
            std::string error;
            if (!ensure_target(_targets[i & 1u], width, height, pass.filter_linear, error))
            {
                std::fprintf(stderr, "AGS shader: %s\n", error.c_str());
                break;
            }
            framebuffer = _targets[i & 1u].fbo;
        }

        pBindFramebuffer(GL_FRAMEBUFFER_EXT, framebuffer);
        glViewport(0, 0, width, height);
        glUseProgram(pass.program);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texture);
        if (pass.filter_linear)
        {
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        }
        else
        {
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        }

        if (pass.texture >= 0) glUniform1i(pass.texture, 0);
        if (pass.input_size >= 0) glUniform2f(pass.input_size, static_cast<float>(source_width), static_cast<float>(source_height));
        if (pass.texture_size >= 0) glUniform2f(pass.texture_size, static_cast<float>(source_width), static_cast<float>(source_height));
        if (pass.output_size >= 0) glUniform2f(pass.output_size, static_cast<float>(width), static_cast<float>(height));
        if (pass.original_size >= 0) glUniform2f(pass.original_size, static_cast<float>(iw), static_cast<float>(ih));
        if (pass.texel_size >= 0) glUniform2f(pass.texel_size,
                                               1.0f / static_cast<float>(std::max(source_width, 1)),
                                               1.0f / static_cast<float>(std::max(source_height, 1)));
        if (pass.frame_count >= 0) glUniform1i(pass.frame_count, static_cast<GLint>(_frame_count));
        if (pass.frame_direction >= 0) glUniform1i(pass.frame_direction, 1);
        if (pass.time >= 0) glUniform1f(pass.time, static_cast<float>(SDL_GetTicks()) / 1000.0f);

        // AGS may have a VBO bound. ScummVM-style shader rendering keeps its
        // fullscreen geometry independent from the engine's vertex buffer state.
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glEnableVertexAttribArray(0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), &kQuad[0].x);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), &kQuad[0].u);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        glDisableVertexAttribArray(0);
        glDisableVertexAttribArray(1);

        log_gl_error("shader pass");

        if (!last)
        {
            texture = _targets[i & 1u].texture;
            source_width = width;
            source_height = height;
        }
    }

    glUseProgram(old_program);
    glBindBuffer(GL_ARRAY_BUFFER, old_array_buffer);
    glActiveTexture(old_active_texture);
    glBindTexture(GL_TEXTURE_2D, old_texture);
    pBindFramebuffer(GL_FRAMEBUFFER_EXT, static_cast<GLuint>(old_framebuffer));
    glReadBuffer(static_cast<GLenum>(old_read_buffer));
    glViewport(old_viewport[0], old_viewport[1], old_viewport[2], old_viewport[3]);

    if (blend_enabled) glEnable(GL_BLEND); else glDisable(GL_BLEND);
    if (depth_enabled) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (cull_enabled) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
    if (scissor_enabled) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);

    ++_frame_count;
}
