#include "shader_pipeline_v4.h"
#include "lut_manager.h"

#include <GL/gl.h>
#include <GL/glext.h>
#include <SDL2/SDL.h>

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {
using GenFbo = void (*)(GLsizei, GLuint *);
using DeleteFbo = void (*)(GLsizei, const GLuint *);
using BindFbo = void (*)(GLenum, GLuint);
using AttachTex = void (*)(GLenum, GLenum, GLenum, GLuint, GLint);
using CheckFbo = GLenum (*)(GLenum);
using GenerateMipmap = void (*)(GLenum);

GenFbo pGenFbo = nullptr;
DeleteFbo pDeleteFbo = nullptr;
BindFbo pBindFbo = nullptr;
AttachTex pAttachTex = nullptr;
CheckFbo pCheckFbo = nullptr;
GenerateMipmap pGenerateMipmap = nullptr;

constexpr int MaxSavedTextureUnits = 32;

const char *kCapabilityDefines =
    "#ifndef PARAMETER_UNIFORM\n#define PARAMETER_UNIFORM\n#endif\n"
    "#ifndef _HAS_ORIGINALASPECT_UNIFORMS\n#define _HAS_ORIGINALASPECT_UNIFORMS\n#endif\n"
    "#ifndef _HAS_FRAMETIME_UNIFORMS\n#define _HAS_FRAMETIME_UNIFORMS\n#endif\n"
    "#ifndef _HAS_SENSOR_UNIFORMS\n#define _HAS_SENSOR_UNIFORMS\n#endif\n";

const char *kVertexBody =
    "uniform mat4 MVPMatrix;\n"
    "attribute vec4 VertexCoord;\n"
    "attribute vec4 TexCoord;\n"
    "attribute vec4 COLOR;\n"
    "varying vec4 TEX0;\n"
    "varying vec4 COL0;\n"
    "varying vec2 vTexCoord;\n"
    "void main(){TEX0=TexCoord;COL0=COLOR;vTexCoord=TexCoord.xy;gl_Position=MVPMatrix*VertexCoord;}\n";

struct Vertex { float x, y, u, v; };
const Vertex kQuad[4] = {
    {-1.f,-1.f,0.f,0.f}, {1.f,-1.f,1.f,0.f},
    {-1.f,1.f,0.f,1.f}, {1.f,1.f,1.f,1.f}
};

struct AttribState {
    GLint enabled = GL_FALSE;
    GLint size = 4;
    GLint type = GL_FLOAT;
    GLint normalized = GL_FALSE;
    GLint stride = 0;
    GLint buffer = 0;
    void *pointer = nullptr;
    GLfloat current[4] = {0.f, 0.f, 0.f, 1.f};
};

struct TextureView {
    GLuint texture = 0;
    int input_width = 0;
    int input_height = 0;
    int texture_width = 0;
    int texture_height = 0;
};

struct TextureAllocator {
    int next = 1;
    int max_units = 1;
    std::vector<std::pair<GLuint, int>> assigned;

    TextureAllocator(int max_texture_units, GLuint unit0_texture)
        : max_units(max_texture_units) {
        if (unit0_texture) assigned.push_back(std::make_pair(unit0_texture, 0));
    }

    int get(GLuint texture) {
        if (!texture) return -1;
        for (const auto &entry : assigned)
            if (entry.first == texture) return entry.second;
        if (next >= max_units) return -1;
        const int unit = next++;
        assigned.push_back(std::make_pair(texture, unit));
        glActiveTexture(GL_TEXTURE0 + unit);
        glBindTexture(GL_TEXTURE_2D, texture);
        return unit;
    }
};

std::string trim(const std::string &s) {
    const size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return {};
    const size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

std::string lower(std::string s) {
    for (char &c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string unquote(std::string s) {
    s = trim(s);
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"')
        s = s.substr(1, s.size() - 2);
    return s;
}

std::string parent_dir(const std::string &p) {
    const size_t n = p.find_last_of("/\\");
    return n == std::string::npos ? "." : p.substr(0, n);
}

std::string join_path(const std::string &d, const std::string &p) {
    if (p.empty() || p.front() == '/' || (p.size() > 1 && p[1] == ':')) return p;
    return d + "/" + p;
}

bool suffix(const std::string &s, const char *ext) {
    const size_t n = std::strlen(ext);
    return s.size() >= n && lower(s.substr(s.size() - n)) == lower(ext);
}

bool combined_shader(const std::string &source) {
    const bool has_vertex = source.find("defined(VERTEX)") != std::string::npos ||
                            source.find("defined (VERTEX)") != std::string::npos ||
                            source.find("#ifdef VERTEX") != std::string::npos;
    const bool has_fragment = source.find("defined(FRAGMENT)") != std::string::npos ||
                              source.find("defined (FRAGMENT)") != std::string::npos ||
                              source.find("#ifdef FRAGMENT") != std::string::npos;
    return has_vertex && has_fragment;
}

std::string inject_preamble(const std::string &source,
                            const char *stage,
                            const std::string &alias_defines) {
    std::string preamble;
    if (stage && *stage)
        preamble += std::string("#define ") + stage + "\n";
    preamble += kCapabilityDefines;
    preamble += alias_defines;

    const size_t version = source.find("#version");
    if (version != std::string::npos) {
        const size_t end = source.find('\n', version);
        if (end != std::string::npos)
            return source.substr(0, end + 1) + preamble + source.substr(end + 1);
    }
    return preamble + source;
}

std::string fallback_vertex(const std::string &fragment_source,
                            const std::string &alias_defines) {
    std::string version = "#version 120\n";
    const size_t pos = fragment_source.find("#version");
    if (pos != std::string::npos) {
        const size_t end = fragment_source.find('\n', pos);
        if (end != std::string::npos)
            version = fragment_source.substr(pos, end - pos + 1);
    }
    return version + kCapabilityDefines + alias_defines + kVertexBody;
}

bool resolve_fbo(std::string &error) {
    if (pGenFbo && pDeleteFbo && pBindFbo && pAttachTex && pCheckFbo) return true;
    pGenFbo = reinterpret_cast<GenFbo>(SDL_GL_GetProcAddress("glGenFramebuffersEXT"));
    pDeleteFbo = reinterpret_cast<DeleteFbo>(SDL_GL_GetProcAddress("glDeleteFramebuffersEXT"));
    pBindFbo = reinterpret_cast<BindFbo>(SDL_GL_GetProcAddress("glBindFramebufferEXT"));
    pAttachTex = reinterpret_cast<AttachTex>(SDL_GL_GetProcAddress("glFramebufferTexture2DEXT"));
    pCheckFbo = reinterpret_cast<CheckFbo>(SDL_GL_GetProcAddress("glCheckFramebufferStatusEXT"));
    if (!pGenFbo || !pDeleteFbo || !pBindFbo || !pAttachTex || !pCheckFbo) {
        error = "EXT_framebuffer_object functions are unavailable";
        return false;
    }
    return true;
}

void resolve_mipmap() {
    if (pGenerateMipmap) return;
    pGenerateMipmap = reinterpret_cast<GenerateMipmap>(SDL_GL_GetProcAddress("glGenerateMipmap"));
    if (!pGenerateMipmap)
        pGenerateMipmap = reinterpret_cast<GenerateMipmap>(SDL_GL_GetProcAddress("glGenerateMipmapEXT"));
}

ShaderPipelineV4::WrapMode parse_wrap(const std::string &s) {
    const std::string v = lower(unquote(s));
    if (v == "clamp_to_edge") return ShaderPipelineV4::WrapMode::ClampToEdge;
    if (v == "repeat") return ShaderPipelineV4::WrapMode::Repeat;
    if (v == "mirrored_repeat") return ShaderPipelineV4::WrapMode::MirroredRepeat;
    return ShaderPipelineV4::WrapMode::ClampToBorder;
}

GLenum wrap_gl(ShaderPipelineV4::WrapMode mode) {
    switch (mode) {
        case ShaderPipelineV4::WrapMode::ClampToEdge: return GL_CLAMP_TO_EDGE;
        case ShaderPipelineV4::WrapMode::Repeat: return GL_REPEAT;
        case ShaderPipelineV4::WrapMode::MirroredRepeat: return GL_MIRRORED_REPEAT;
        default: return GL_CLAMP_TO_BORDER;
    }
}

int target_format(bool use_float, bool use_srgb) {
#ifdef GL_RGBA16F_ARB
    if (use_float) return GL_RGBA16F_ARB;
#endif
#ifdef GL_SRGB8_ALPHA8_EXT
    if (use_srgb) return GL_SRGB8_ALPHA8_EXT;
#endif
    return GL_RGBA;
}

ShaderPipelineV4::ScaleType parse_scale_type(const std::string &s) {
    const std::string v = lower(unquote(s));
    if (v == "viewport") return ShaderPipelineV4::ScaleType::Viewport;
    if (v == "absolute") return ShaderPipelineV4::ScaleType::Absolute;
    return ShaderPipelineV4::ScaleType::Source;
}

int scaled_dim(ShaderPipelineV4::ScaleType type, float scale, int source, int viewport) {
    if (type == ShaderPipelineV4::ScaleType::Viewport)
        return std::max(1, static_cast<int>(std::lround(viewport * scale)));
    if (type == ShaderPipelineV4::ScaleType::Absolute)
        return std::max(1, static_cast<int>(std::lround(scale)));
    return std::max(1, static_cast<int>(std::lround(source * scale)));
}

void save_attrib(GLuint index, AttribState &state) {
    glGetVertexAttribiv(index, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &state.enabled);
    glGetVertexAttribiv(index, GL_VERTEX_ATTRIB_ARRAY_SIZE, &state.size);
    glGetVertexAttribiv(index, GL_VERTEX_ATTRIB_ARRAY_TYPE, &state.type);
    glGetVertexAttribiv(index, GL_VERTEX_ATTRIB_ARRAY_NORMALIZED, &state.normalized);
    glGetVertexAttribiv(index, GL_VERTEX_ATTRIB_ARRAY_STRIDE, &state.stride);
    glGetVertexAttribiv(index, GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING, &state.buffer);
    glGetVertexAttribPointerv(index, GL_VERTEX_ATTRIB_ARRAY_POINTER, &state.pointer);
    glGetVertexAttribfv(index, GL_CURRENT_VERTEX_ATTRIB, state.current);
}

void restore_attrib(GLuint index, const AttribState &state) {
    glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(state.buffer));
    glVertexAttribPointer(index,
                          state.size,
                          static_cast<GLenum>(state.type),
                          static_cast<GLboolean>(state.normalized),
                          state.stride,
                          state.pointer);
    if (state.enabled) glEnableVertexAttribArray(index);
    else glDisableVertexAttribArray(index);
    glVertexAttrib4fv(index, state.current);
}

void bind_tex_coord_attrib(GLint location) {
    if (location < 0) return;
    glEnableVertexAttribArray(static_cast<GLuint>(location));
    glVertexAttribPointer(static_cast<GLuint>(location),
                          2,
                          GL_FLOAT,
                          GL_FALSE,
                          sizeof(Vertex),
                          &kQuad[0].u);
}

ShaderPipelineV4::FrameUniform find_frame_uniform(unsigned program, const std::string &base) {
    ShaderPipelineV4::FrameUniform frame;
    frame.texture = glGetUniformLocation(program, (base + "Texture").c_str());
    frame.tex_coord = glGetAttribLocation(program, (base + "TexCoord").c_str());
    frame.input_size = glGetUniformLocation(program, (base + "InputSize").c_str());
    frame.texture_size = glGetUniformLocation(program, (base + "TextureSize").c_str());
    return frame;
}

void merge_frame_uniform(ShaderPipelineV4::FrameUniform &dst,
                         const ShaderPipelineV4::FrameUniform &fallback) {
    if (dst.texture < 0) dst.texture = fallback.texture;
    if (dst.tex_coord < 0) dst.tex_coord = fallback.tex_coord;
    if (dst.input_size < 0) dst.input_size = fallback.input_size;
    if (dst.texture_size < 0) dst.texture_size = fallback.texture_size;
}

void set_frame_uniform(const ShaderPipelineV4::FrameUniform &uniform,
                       const TextureView &view,
                       TextureAllocator &allocator) {
    if (uniform.input_size >= 0)
        glUniform2f(uniform.input_size,
                    static_cast<float>(view.input_width),
                    static_cast<float>(view.input_height));
    if (uniform.texture_size >= 0)
        glUniform2f(uniform.texture_size,
                    static_cast<float>(view.texture_width),
                    static_cast<float>(view.texture_height));
    if (uniform.texture >= 0 && view.texture) {
        const int unit = allocator.get(view.texture);
        if (unit >= 0) glUniform1i(uniform.texture, unit);
    }
    bind_tex_coord_attrib(uniform.tex_coord);
}

TextureView target_view(const ShaderPipelineV4::Target &target) {
    TextureView view;
    view.texture = target.texture;
    view.input_width = target.input_width > 0 ? target.input_width : target.width;
    view.input_height = target.input_height > 0 ? target.input_height : target.height;
    view.texture_width = target.width;
    view.texture_height = target.height;
    return view;
}

bool valid_identifier(const std::string &s) {
    if (s.empty()) return false;
    const unsigned char first = static_cast<unsigned char>(s.front());
    if (!(std::isalpha(first) || s.front() == '_')) return false;
    for (char c : s) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (!(std::isalnum(u) || c == '_')) return false;
    }
    return true;
}
}

ShaderPipelineV4::~ShaderPipelineV4() {
    if (SDL_GL_GetCurrentContext()) clear();
}

bool ShaderPipelineV4::parse_bool(const std::string &s, bool d) {
    const std::string v = lower(trim(s));
    if (v == "1" || v == "true") return true;
    if (v == "0" || v == "false") return false;
    return d;
}

int ShaderPipelineV4::parse_int(const std::string &s, int d) {
    try {
        const std::string t = trim(s);
        size_t p = 0;
        const int v = std::stoi(t, &p);
        return p == t.size() ? v : d;
    }
    catch (...) { return d; }
}

unsigned ShaderPipelineV4::parse_uint_prefix(const std::string &s, unsigned d) {
    const std::string t = trim(s);
    if (t.empty() || t.front() == '-') return d;
    errno = 0;
    char *end = nullptr;
    const unsigned long value = std::strtoul(t.c_str(), &end, 0);
    if (end == t.c_str() || errno == ERANGE) return d;
    if (value > static_cast<unsigned long>(UINT_MAX)) return UINT_MAX;
    return static_cast<unsigned>(value);
}

float ShaderPipelineV4::parse_float(const std::string &s, float d) {
    try {
        const std::string t = trim(s);
        size_t p = 0;
        const float v = std::stof(t, &p);
        return p == t.size() ? v : d;
    }
    catch (...) { return d; }
}

void ShaderPipelineV4::destroy_target(Target &target) {
    if (target.fbo && pDeleteFbo) pDeleteFbo(1, &target.fbo);
    if (target.texture) glDeleteTextures(1, &target.texture);
    target = Target();
}

void ShaderPipelineV4::clear() {
    for (Pass &pass : _passes)
        if (pass.program) glDeleteProgram(pass.program);
    _passes.clear();

    if (pDeleteFbo) {
        for (Target &target : _targets) destroy_target(target);
        for (Target &target : _prev_history) destroy_target(target);
        destroy_target(_feedback);
    }
    else {
        _targets.clear();
        _prev_history.clear();
        _feedback = Target();
    }

    _targets.clear();
    _prev_history.clear();
    _parameters.clear();
    _feedback = Target();
    _feedback_pass = -1;
    _uses_prev_history = false;
    _uses_feedback = false;
    _alias_defines.clear();
    ags_lut_clear();
    _frame_count = 0;
    _last_frame_ticks = 0;
}

void ShaderPipelineV4::set_parameter(const std::string &name, float value, bool overwrite) {
    for (Parameter &parameter : _parameters) {
        if (parameter.name == name) {
            if (overwrite) parameter.value = value;
            return;
        }
    }
    Parameter parameter;
    parameter.name = name;
    parameter.value = value;
    _parameters.push_back(parameter);
}

bool ShaderPipelineV4::load_text(const std::string &path,
                                 std::string &text,
                                 std::string &error) const {
    std::ifstream file(path.c_str(), std::ios::binary);
    if (!file) {
        error = "cannot read shader: " + path;
        return false;
    }
    std::ostringstream stream;
    stream << file.rdbuf();
    text = stream.str();
    if (text.empty()) {
        error = "empty shader: " + path;
        return false;
    }
    return true;
}

bool ShaderPipelineV4::compile_shader(unsigned type,
                                      const std::string &source,
                                      unsigned &shader,
                                      std::string &error) const {
    shader = glCreateShader(static_cast<GLenum>(type));
    if (!shader) {
        error = "glCreateShader failed";
        return false;
    }
    const char *src = source.c_str();
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);

    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (ok == GL_TRUE) return true;

    GLint length = 0;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
    std::vector<char> log(static_cast<size_t>(std::max(length, 1)));
    if (length) glGetShaderInfoLog(shader, length, nullptr, log.data());
    error = log.data();
    glDeleteShader(shader);
    shader = 0;
    return false;
}

bool ShaderPipelineV4::create_program(const std::string &vertex,
                                      const std::string &fragment,
                                      unsigned &program,
                                      std::string &error) const {
    unsigned vertex_shader = 0;
    unsigned fragment_shader = 0;
    if (!compile_shader(GL_VERTEX_SHADER, vertex, vertex_shader, error)) return false;
    if (!compile_shader(GL_FRAGMENT_SHADER, fragment, fragment_shader, error)) {
        glDeleteShader(vertex_shader);
        return false;
    }

    program = glCreateProgram();
    if (!program) {
        glDeleteShader(vertex_shader);
        glDeleteShader(fragment_shader);
        error = "glCreateProgram failed";
        return false;
    }

    glAttachShader(program, vertex_shader);
    glAttachShader(program, fragment_shader);
    glBindAttribLocation(program, 0, "VertexCoord");
    glBindAttribLocation(program, 1, "TexCoord");
    glBindAttribLocation(program, 2, "COLOR");
    glBindAttribLocation(program, 2, "Color");
    glBindAttribLocation(program, 3, "LUTTexCoord");
    glBindAttribLocation(program, 0, "aPosition");
    glBindAttribLocation(program, 1, "aTexCoord");
    glLinkProgram(program);
    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);

    GLint ok = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (ok == GL_TRUE) return true;

    GLint length = 0;
    glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
    std::vector<char> log(static_cast<size_t>(std::max(length, 1)));
    if (length) glGetProgramInfoLog(program, length, nullptr, log.data());
    error = log.data();
    glDeleteProgram(program);
    program = 0;
    return false;
}

bool ShaderPipelineV4::add_pass(const std::string &path,
                                const Pass *preset,
                                std::string &error) {
    std::string source;
    if (!load_text(path, source, error)) return false;

    std::istringstream pragma_stream(source);
    std::string line;
    while (std::getline(pragma_stream, line)) {
        line = trim(line);
        if (line.rfind("#pragma parameter ", 0) != 0) continue;
        std::istringstream head(line.substr(18));
        std::string name;
        head >> name;
        if (name.empty()) continue;
        const size_t quote1 = line.find('"');
        const size_t quote2 = quote1 == std::string::npos
            ? std::string::npos
            : line.find('"', quote1 + 1);
        if (quote2 == std::string::npos) continue;
        std::istringstream values(line.substr(quote2 + 1));
        float default_value = 0.f;
        if (values >> default_value) set_parameter(name, default_value, false);
    }

    Pass pass;
    if (preset) pass = *preset;
    pass.source_path = path;

    const bool combined = combined_shader(source);
    const std::string vertex = combined
        ? inject_preamble(source, "VERTEX", _alias_defines)
        : fallback_vertex(source, _alias_defines);
    const std::string fragment = combined
        ? inject_preamble(source, "FRAGMENT", _alias_defines)
        : inject_preamble(source, nullptr, _alias_defines);

    if (!create_program(vertex, fragment, pass.program, error)) return false;

    pass.texture = glGetUniformLocation(pass.program, "Texture");
    if (pass.texture < 0) pass.texture = glGetUniformLocation(pass.program, "uTexture");
    pass.input_size = glGetUniformLocation(pass.program, "InputSize");
    if (pass.input_size < 0) pass.input_size = glGetUniformLocation(pass.program, "SourceSize");
    if (pass.input_size < 0) pass.input_size = glGetUniformLocation(pass.program, "uInputSize");
    pass.texture_size = glGetUniformLocation(pass.program, "TextureSize");
    pass.output_size = glGetUniformLocation(pass.program, "OutputSize");
    if (pass.output_size < 0) pass.output_size = glGetUniformLocation(pass.program, "uOutputSize");
    pass.original_size = glGetUniformLocation(pass.program, "OriginalSize");
    if (pass.original_size < 0) pass.original_size = glGetUniformLocation(pass.program, "uOriginalSize");
    pass.final_viewport_size = glGetUniformLocation(pass.program, "FinalViewportSize");
    pass.texel_size = glGetUniformLocation(pass.program, "uTexelSize");
    pass.frame_count = glGetUniformLocation(pass.program, "FrameCount");
    if (pass.frame_count < 0) pass.frame_count = glGetUniformLocation(pass.program, "uFrameCount");
    pass.frame_direction = glGetUniformLocation(pass.program, "FrameDirection");
    pass.frame_time_delta = glGetUniformLocation(pass.program, "FrameTimeDelta");
    pass.original_fps = glGetUniformLocation(pass.program, "OriginalFPS");
    pass.rotation = glGetUniformLocation(pass.program, "Rotation");
    pass.original_aspect = glGetUniformLocation(pass.program, "OriginalAspect");
    pass.original_aspect_rotated = glGetUniformLocation(pass.program, "OriginalAspectRotated");
    pass.gyroscope = glGetUniformLocation(pass.program, "Gyroscope");
    pass.accelerometer = glGetUniformLocation(pass.program, "Accelerometer");
    pass.accelerometer_rest = glGetUniformLocation(pass.program, "AccelerometerRest");
    pass.time = glGetUniformLocation(pass.program, "Time");
    if (pass.time < 0) pass.time = glGetUniformLocation(pass.program, "uTime");
    pass.mvp_matrix = glGetUniformLocation(pass.program, "MVPMatrix");
    pass.lut_tex_coord = glGetAttribLocation(pass.program, "LUTTexCoord");

    pass.orig = find_frame_uniform(pass.program, "Orig");
    pass.feedback = find_frame_uniform(pass.program, "Feedback");
    merge_frame_uniform(pass.feedback, find_frame_uniform(pass.program, "PassFeedback"));

    for (int n = 0; n < MaxPassRefs; ++n) {
        pass.pass_ref[n] = find_frame_uniform(pass.program, "Pass" + std::to_string(n + 1));
        pass.pass_prev[n] = find_frame_uniform(pass.program, "PassPrev" + std::to_string(n + 1));
        if (n == 0)
            merge_frame_uniform(pass.pass_prev[n], find_frame_uniform(pass.program, "PassPrev"));
    }

    for (int n = 0; n < MaxFrameHistory; ++n) {
        const std::string prev_base = n == 0 ? "Prev" : "Prev" + std::to_string(n);
        pass.prev[n] = find_frame_uniform(pass.program, prev_base);

        const std::string history_base = n == 0
            ? "OriginalHistory"
            : "OriginalHistory" + std::to_string(n);
        pass.original_history[n] = find_frame_uniform(pass.program, history_base);

        if (pass.prev[n].used() || pass.original_history[n].used())
            _uses_prev_history = true;
    }

    if (pass.feedback.used()) _uses_feedback = true;

    for (std::size_t j = 0; j < _passes.size(); ++j) {
        if (_passes[j].alias.empty()) continue;
        AliasUniform alias;
        alias.pass_index = j;
        alias.frame = find_frame_uniform(pass.program, _passes[j].alias);
        if (alias.frame.used()) pass.aliases.push_back(alias);
    }

    _passes.push_back(pass);
    return true;
}

bool ShaderPipelineV4::parse_chain(const std::string &path,
                                   std::vector<std::string> &out,
                                   std::string &error) const {
    std::string source;
    if (!load_text(path, source, error)) return false;
    std::istringstream input(source);
    std::string line;
    const std::string directory = parent_dir(path);
    while (std::getline(input, line)) {
        line = trim(line);
        if (line.empty() || line.front() == '#') continue;
        if (line.compare(0, 5, "pass=") == 0) {
            const std::string shader = unquote(line.substr(5));
            if (!shader.empty()) out.push_back(join_path(directory, shader));
        }
    }
    if (out.empty()) {
        error = "shader chain has no passes: " + path;
        return false;
    }
    return true;
}

bool ShaderPipelineV4::parse_glslp(const std::string &path,
                                   std::vector<Pass> &out,
                                   std::string &error) {
    std::string source;
    if (!load_text(path, source, error)) return false;

    struct Entry {
        std::string path;
        Pass pass;
        bool wrap_set = false;
    };

    std::vector<Entry> entries;
    std::vector<Parameter> preset_parameters;
    int count = -1;
    _feedback_pass = -1;

    std::istringstream input(source);
    std::string line;
    const std::string directory = parent_dir(path);

    while (std::getline(input, line)) {
        line = trim(line);
        if (line.empty() || line.front() == '#') continue;
        const size_t equals = line.find('=');
        if (equals == std::string::npos) continue;

        const std::string key = trim(line.substr(0, equals));
        const std::string value = unquote(line.substr(equals + 1));

        if (key == "shaders") {
            count = parse_int(value, -1);
            continue;
        }
        if (key == "feedback_pass") {
            _feedback_pass = parse_int(value, -1);
            continue;
        }
        if (key.rfind("shader", 0) == 0 && key.size() > 6 &&
            std::isdigit(static_cast<unsigned char>(key[6]))) {
            const int index = parse_int(key.substr(6), -1);
            if (index >= 0) {
                if (static_cast<size_t>(index) >= entries.size())
                    entries.resize(static_cast<size_t>(index) + 1);
                entries[static_cast<size_t>(index)].path = join_path(directory, value);
            }
            continue;
        }

        size_t number = key.size();
        while (number > 0 && std::isdigit(static_cast<unsigned char>(key[number - 1]))) --number;
        if (number < key.size()) {
            const std::string base = key.substr(0, number);
            const bool pass_key = base == "filter_linear" ||
                                  base == "mipmap_input" ||
                                  base == "wrap_mode" ||
                                  base == "float_framebuffer" ||
                                  base == "srgb_framebuffer" ||
                                  base == "alias" ||
                                  base == "scale" ||
                                  base == "scale_x" ||
                                  base == "scale_y" ||
                                  base == "scale_type" ||
                                  base == "scale_type_x" ||
                                  base == "scale_type_y" ||
                                  base == "frame_count_mod";
            if (pass_key) {
                const int index = parse_int(key.substr(number), -1);
                if (index < 0) continue;
                if (static_cast<size_t>(index) >= entries.size())
                    entries.resize(static_cast<size_t>(index) + 1);
                Entry &entry = entries[static_cast<size_t>(index)];

                if (base == "filter_linear") entry.pass.filter_linear = parse_bool(value, true);
                else if (base == "mipmap_input") entry.pass.mipmap_input = parse_bool(value, false);
                else if (base == "wrap_mode") {
                    entry.pass.wrap_mode = parse_wrap(value);
                    entry.wrap_set = true;
                }
                else if (base == "float_framebuffer") entry.pass.float_framebuffer = parse_bool(value, false);
                else if (base == "srgb_framebuffer") entry.pass.srgb_framebuffer = parse_bool(value, false);
                else if (base == "alias") entry.pass.alias = value;
                else if (base == "scale") entry.pass.scale_x = entry.pass.scale_y = parse_float(value, 1.f);
                else if (base == "scale_x") entry.pass.scale_x = parse_float(value, 1.f);
                else if (base == "scale_y") entry.pass.scale_y = parse_float(value, 1.f);
                else if (base == "scale_type") {
                    const ScaleType type = parse_scale_type(value);
                    entry.pass.scale_type_x = type;
                    entry.pass.scale_type_y = type;
                }
                else if (base == "scale_type_x") entry.pass.scale_type_x = parse_scale_type(value);
                else if (base == "scale_type_y") entry.pass.scale_type_y = parse_scale_type(value);
                else if (base == "frame_count_mod")
                    entry.pass.frame_count_mod = parse_uint_prefix(value, 0);
                continue;
            }
        }

        if (key == "textures" || key == "parameters") continue;
        const float parameter_value = parse_float(value, NAN);
        if (!std::isnan(parameter_value)) {
            Parameter parameter;
            parameter.name = key;
            parameter.value = parameter_value;
            preset_parameters.push_back(parameter);
        }
    }

    if (count < 0) count = static_cast<int>(entries.size());
    if (count <= 0) {
        error = "glslp has no shaders: " + path;
        return false;
    }
    if (_feedback_pass >= count) {
        error = "feedback_pass is out of range in " + path;
        return false;
    }

    out.clear();
    _alias_defines.clear();
    for (int i = 0; i < count; ++i) {
        if (i >= static_cast<int>(entries.size()) || entries[static_cast<size_t>(i)].path.empty()) {
            error = "missing shader" + std::to_string(i) + " in " + path;
            return false;
        }
        Entry &entry = entries[static_cast<size_t>(i)];
        if (!entry.wrap_set) entry.pass.wrap_mode = WrapMode::ClampToBorder;
        if (entry.pass.float_framebuffer && entry.pass.srgb_framebuffer) {
            error = "shader" + std::to_string(i) +
                    " cannot use float_framebuffer and srgb_framebuffer together";
            return false;
        }
        entry.pass.source_path = entry.path;
        out.push_back(entry.pass);
        if (valid_identifier(entry.pass.alias))
            _alias_defines += "#define " + entry.pass.alias + "_ALIAS\n";
    }

    for (const Parameter &parameter : preset_parameters)
        set_parameter(parameter.name, parameter.value, true);
    return true;
}

bool ShaderPipelineV4::load(const std::string &path, std::string &error) {
    clear();

    if (suffix(path, ".glslp")) {
        std::vector<Pass> passes;
        if (!parse_glslp(path, passes, error)) return false;
        if (!ags_lut_load_preset(path, error)) {
            clear();
            return false;
        }
        for (const Pass &pass : passes) {
            if (!add_pass(pass.source_path, &pass, error)) {
                clear();
                return false;
            }
        }
    }
    else if (suffix(path, ".agschain")) {
        std::vector<std::string> passes;
        if (!parse_chain(path, passes, error)) return false;
        for (const std::string &shader : passes) {
            if (!add_pass(shader, nullptr, error)) {
                clear();
                return false;
            }
        }
    }
    else if (!add_pass(path, nullptr, error)) {
        clear();
        return false;
    }

    if (_passes.size() > 1) _targets.resize(_passes.size() - 1);
    if (_uses_prev_history) _prev_history.resize(MaxFrameHistory);
    _uses_feedback = _uses_feedback && _feedback_pass >= 0;
    return true;
}

bool ShaderPipelineV4::ensure_fbo_functions(std::string &error) {
    return resolve_fbo(error);
}

bool ShaderPipelineV4::ensure_target(Target &target,
                                     int width,
                                     int height,
                                     bool use_float,
                                     bool use_srgb,
                                     std::string &error) {
    return ensure_target(target, width, height, target_format(use_float, use_srgb), error);
}

bool ShaderPipelineV4::ensure_target(Target &target,
                                     int width,
                                     int height,
                                     int format,
                                     std::string &error) {
    if (!resolve_fbo(error)) return false;
    if (target.fbo && target.width == width && target.height == height && target.format == format)
        return true;

    destroy_target(target);
    target.width = width;
    target.height = height;
    target.format = format;

    glGenTextures(1, &target.texture);
    glBindTexture(GL_TEXTURE_2D, target.texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    pGenFbo(1, &target.fbo);
    pBindFbo(GL_FRAMEBUFFER_EXT, target.fbo);
    pAttachTex(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_TEXTURE_2D, target.texture, 0);
    if (pCheckFbo(GL_FRAMEBUFFER_EXT) != GL_FRAMEBUFFER_COMPLETE_EXT) {
        error = "incomplete shader framebuffer";
        destroy_target(target);
        pBindFbo(GL_FRAMEBUFFER_EXT, 0);
        return false;
    }
    return true;
}

bool ShaderPipelineV4::copy_texture_to_target(unsigned source,
                                              int width,
                                              int height,
                                              int format,
                                              Target &destination,
                                              std::string &error) {
    if (!source || width <= 0 || height <= 0) return false;
    if (!ensure_target(destination, width, height, format, error)) return false;

    GLuint source_fbo = 0;
    pGenFbo(1, &source_fbo);
    pBindFbo(GL_FRAMEBUFFER_EXT, source_fbo);
    pAttachTex(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_TEXTURE_2D, source, 0);
    if (pCheckFbo(GL_FRAMEBUFFER_EXT) != GL_FRAMEBUFFER_COMPLETE_EXT) {
        error = "incomplete source framebuffer";
        pBindFbo(GL_FRAMEBUFFER_EXT, 0);
        pDeleteFbo(1, &source_fbo);
        return false;
    }

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, destination.texture);
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, width, height);
    const GLenum gl_error = glGetError();
    pBindFbo(GL_FRAMEBUFFER_EXT, 0);
    pDeleteFbo(1, &source_fbo);
    if (gl_error != GL_NO_ERROR) {
        error = "OpenGL failed to copy shader history texture";
        return false;
    }
    return true;
}

bool ShaderPipelineV4::copy_target_to_target(const Target &source,
                                             Target &destination,
                                             std::string &error) {
    if (!source.texture || !source.fbo) return false;
    if (!ensure_target(destination, source.width, source.height, source.format, error)) return false;
    destination.input_width = source.input_width;
    destination.input_height = source.input_height;

    pBindFbo(GL_FRAMEBUFFER_EXT, source.fbo);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, destination.texture);
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, source.width, source.height);
    if (glGetError() != GL_NO_ERROR) {
        error = "OpenGL failed to copy feedback framebuffer";
        return false;
    }
    return true;
}

void ShaderPipelineV4::apply(unsigned input_texture,
                             int input_width,
                             int input_height,
                             int output_width,
                             int output_height) {
    if (_passes.empty() || !input_texture ||
        input_width <= 0 || input_height <= 0 ||
        output_width <= 0 || output_height <= 0)
        return;

    std::string fbo_error;
    if (!resolve_fbo(fbo_error)) {
        std::fprintf(stderr, "AGS shader: %s\n", fbo_error.c_str());
        return;
    }

    const Uint64 now = SDL_GetPerformanceCounter();
    const Uint64 frequency = SDL_GetPerformanceFrequency();
    GLint frame_time_delta_us = 16667;
    if (_last_frame_ticks && frequency && now > _last_frame_ticks) {
        const Uint64 elapsed = now - _last_frame_ticks;
        const Uint64 usec = (elapsed * 1000000ull) / frequency;
        frame_time_delta_us = static_cast<GLint>(std::min<Uint64>(usec, static_cast<Uint64>(INT_MAX)));
        if (frame_time_delta_us <= 0) frame_time_delta_us = 1;
    }
    _last_frame_ticks = now;
    const GLfloat original_fps_value = frame_time_delta_us > 0
        ? 1000000.f / static_cast<float>(frame_time_delta_us)
        : 60.f;
    const GLfloat original_aspect_value = static_cast<float>(input_width) /
                                          static_cast<float>(std::max(input_height, 1));

    GLint old_program = 0;
    GLint old_array_buffer = 0;
    GLint old_active_texture = GL_TEXTURE0;
    GLint old_fbo = 0;
    GLint old_viewport[4] = {0, 0, 0, 0};
    glGetIntegerv(GL_CURRENT_PROGRAM, &old_program);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &old_array_buffer);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &old_active_texture);
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &old_fbo);
    glGetIntegerv(GL_VIEWPORT, old_viewport);

    GLint max_texture_units = 0;
    glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &max_texture_units);
    const int usable_units = std::max(1, std::min(max_texture_units, MaxSavedTextureUnits));
    std::vector<GLint> old_texture_bindings(static_cast<size_t>(usable_units), 0);
    for (int unit = 0; unit < usable_units; ++unit) {
        glActiveTexture(GL_TEXTURE0 + unit);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &old_texture_bindings[static_cast<size_t>(unit)]);
    }

    GLint max_vertex_attribs = 0;
    glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &max_vertex_attribs);
    max_vertex_attribs = std::max(0, max_vertex_attribs);
    std::vector<AttribState> attrib(static_cast<size_t>(max_vertex_attribs));
    for (GLint index = 0; index < max_vertex_attribs; ++index)
        save_attrib(static_cast<GLuint>(index), attrib[static_cast<size_t>(index)]);

    const GLboolean blend = glIsEnabled(GL_BLEND);
    const GLboolean depth_test = glIsEnabled(GL_DEPTH_TEST);
    const GLboolean cull = glIsEnabled(GL_CULL_FACE);
    const GLboolean scissor = glIsEnabled(GL_SCISSOR_TEST);
#ifdef GL_FRAMEBUFFER_SRGB
    const GLboolean framebuffer_srgb = glIsEnabled(GL_FRAMEBUFFER_SRGB);
#endif

    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);

    std::vector<int> pass_input_width(_passes.size(), input_width);
    std::vector<int> pass_input_height(_passes.size(), input_height);
    std::vector<int> pass_output_width(_passes.size(), output_width);
    std::vector<int> pass_output_height(_passes.size(), output_height);
    int source_width = input_width;
    int source_height = input_height;
    for (size_t i = 0; i < _passes.size(); ++i) {
        pass_input_width[i] = source_width;
        pass_input_height[i] = source_height;
        const bool last = i + 1 == _passes.size();
        if (!last) {
            pass_output_width[i] = scaled_dim(_passes[i].scale_type_x,
                                              _passes[i].scale_x,
                                              source_width,
                                              output_width);
            pass_output_height[i] = scaled_dim(_passes[i].scale_type_y,
                                               _passes[i].scale_y,
                                               source_height,
                                               output_height);
        }
        source_width = pass_output_width[i];
        source_height = pass_output_height[i];
    }

    bool render_ok = true;
    if (_uses_feedback && _feedback_pass >= 0 &&
        _feedback_pass < static_cast<int>(_passes.size())) {
        const Pass &feedback_pass = _passes[static_cast<size_t>(_feedback_pass)];
        const int feedback_format = _feedback_pass + 1 == static_cast<int>(_passes.size())
            ? GL_RGBA
            : target_format(feedback_pass.float_framebuffer, feedback_pass.srgb_framebuffer);
        const int fw = pass_output_width[static_cast<size_t>(_feedback_pass)];
        const int fh = pass_output_height[static_cast<size_t>(_feedback_pass)];
        const bool fresh = !_feedback.texture || _feedback.width != fw ||
                           _feedback.height != fh || _feedback.format != feedback_format;
        if (!ensure_target(_feedback, fw, fh, feedback_format, fbo_error)) {
            std::fprintf(stderr, "AGS shader: %s\n", fbo_error.c_str());
            render_ok = false;
        }
        else {
            _feedback.input_width = pass_input_width[static_cast<size_t>(_feedback_pass)];
            _feedback.input_height = pass_input_height[static_cast<size_t>(_feedback_pass)];
            if (fresh) {
                GLfloat old_clear[4];
                glGetFloatv(GL_COLOR_CLEAR_VALUE, old_clear);
                pBindFbo(GL_FRAMEBUFFER_EXT, _feedback.fbo);
                glClearColor(0.f, 0.f, 0.f, 0.f);
                glClear(GL_COLOR_BUFFER_BIT);
                glClearColor(old_clear[0], old_clear[1], old_clear[2], old_clear[3]);
            }
        }
    }

    const GLfloat identity[16] = {
        1,0,0,0,
        0,1,0,0,
        0,0,1,0,
        0,0,0,1
    };

    GLuint current_texture = input_texture;
    for (size_t i = 0; render_ok && i < _passes.size(); ++i) {
        const Pass &pass = _passes[i];
        const bool last = i + 1 == _passes.size();
        const int in_w = pass_input_width[i];
        const int in_h = pass_input_height[i];
        const int out_w = pass_output_width[i];
        const int out_h = pass_output_height[i];

        if (last) {
            pBindFbo(GL_FRAMEBUFFER_EXT, static_cast<GLuint>(old_fbo));
#ifdef GL_FRAMEBUFFER_SRGB
            if (framebuffer_srgb) glEnable(GL_FRAMEBUFFER_SRGB);
            else glDisable(GL_FRAMEBUFFER_SRGB);
#endif
        }
        else {
            Target &target = _targets[i];
            if (!ensure_target(target,
                               out_w,
                               out_h,
                               pass.float_framebuffer,
                               pass.srgb_framebuffer,
                               fbo_error)) {
                std::fprintf(stderr, "AGS shader: %s\n", fbo_error.c_str());
                render_ok = false;
                break;
            }
            target.input_width = in_w;
            target.input_height = in_h;
            pBindFbo(GL_FRAMEBUFFER_EXT, target.fbo);
#ifdef GL_FRAMEBUFFER_SRGB
            if (pass.srgb_framebuffer) glEnable(GL_FRAMEBUFFER_SRGB);
            else glDisable(GL_FRAMEBUFFER_SRGB);
#endif
        }

        glViewport(0, 0, out_w, out_h);
        glUseProgram(pass.program);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, current_texture);
        glBindBuffer(GL_ARRAY_BUFFER, 0);

        const GLenum wrap = wrap_gl(pass.wrap_mode);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
        if (pass.mipmap_input) {
            resolve_mipmap();
            if (pGenerateMipmap) {
                pGenerateMipmap(GL_TEXTURE_2D);
                glTexParameteri(GL_TEXTURE_2D,
                                GL_TEXTURE_MIN_FILTER,
                                pass.filter_linear ? GL_LINEAR_MIPMAP_LINEAR : GL_NEAREST_MIPMAP_NEAREST);
            }
            else {
                glTexParameteri(GL_TEXTURE_2D,
                                GL_TEXTURE_MIN_FILTER,
                                pass.filter_linear ? GL_LINEAR : GL_NEAREST);
            }
        }
        else {
            glTexParameteri(GL_TEXTURE_2D,
                            GL_TEXTURE_MIN_FILTER,
                            pass.filter_linear ? GL_LINEAR : GL_NEAREST);
        }
        glTexParameteri(GL_TEXTURE_2D,
                        GL_TEXTURE_MAG_FILTER,
                        pass.filter_linear ? GL_LINEAR : GL_NEAREST);

        if (pass.texture >= 0) glUniform1i(pass.texture, 0);
        if (pass.input_size >= 0) glUniform2f(pass.input_size, static_cast<float>(in_w), static_cast<float>(in_h));
        if (pass.texture_size >= 0) glUniform2f(pass.texture_size, static_cast<float>(in_w), static_cast<float>(in_h));
        if (pass.output_size >= 0) glUniform2f(pass.output_size, static_cast<float>(out_w), static_cast<float>(out_h));
        if (pass.original_size >= 0) glUniform2f(pass.original_size, static_cast<float>(input_width), static_cast<float>(input_height));
        if (pass.final_viewport_size >= 0) glUniform2f(pass.final_viewport_size, static_cast<float>(output_width), static_cast<float>(output_height));
        if (pass.texel_size >= 0) glUniform2f(pass.texel_size, 1.f / std::max(in_w, 1), 1.f / std::max(in_h, 1));
        if (pass.frame_count >= 0) {
            unsigned long long frame = _frame_count;
            if (pass.frame_count_mod) frame %= pass.frame_count_mod;
            glUniform1i(pass.frame_count, static_cast<GLint>(frame));
        }
        if (pass.frame_direction >= 0) glUniform1i(pass.frame_direction, 1);
        if (pass.frame_time_delta >= 0) glUniform1i(pass.frame_time_delta, frame_time_delta_us);
        if (pass.original_fps >= 0) glUniform1f(pass.original_fps, original_fps_value);
        if (pass.rotation >= 0) glUniform1i(pass.rotation, 0);
        if (pass.original_aspect >= 0) glUniform1f(pass.original_aspect, original_aspect_value);
        if (pass.original_aspect_rotated >= 0) glUniform1f(pass.original_aspect_rotated, original_aspect_value);
        if (pass.gyroscope >= 0) glUniform3f(pass.gyroscope, 0.f, 0.f, 0.f);
        if (pass.accelerometer >= 0) glUniform3f(pass.accelerometer, 0.f, 0.f, 0.f);
        if (pass.accelerometer_rest >= 0) glUniform3f(pass.accelerometer_rest, 0.f, 0.f, 0.f);
        if (pass.time >= 0) glUniform1f(pass.time, static_cast<float>(SDL_GetTicks()) / 1000.f);
        if (pass.mvp_matrix >= 0) glUniformMatrix4fv(pass.mvp_matrix, 1, GL_FALSE, identity);

        for (const Parameter &parameter : _parameters) {
            const GLint location = glGetUniformLocation(pass.program, parameter.name.c_str());
            if (location >= 0) glUniform1f(location, parameter.value);
        }

        TextureAllocator allocator(usable_units, current_texture);
        TextureView original;
        original.texture = input_texture;
        original.input_width = input_width;
        original.input_height = input_height;
        original.texture_width = input_width;
        original.texture_height = input_height;
        set_frame_uniform(pass.orig, original, allocator);

        if (_uses_feedback && _feedback.texture)
            set_frame_uniform(pass.feedback, target_view(_feedback), allocator);

        for (int ref = 0; ref < MaxPassRefs; ++ref) {
            const size_t source_pass = static_cast<size_t>(ref);
            if (source_pass < i && source_pass < _targets.size())
                set_frame_uniform(pass.pass_ref[ref], target_view(_targets[source_pass]), allocator);
        }

        for (int depth = 1; depth <= MaxPassRefs; ++depth) {
            const FrameUniform &uniform = pass.pass_prev[depth - 1];
            if (!uniform.used()) continue;
            if (i >= static_cast<size_t>(depth)) {
                const size_t source_pass = i - static_cast<size_t>(depth);
                if (source_pass < _targets.size())
                    set_frame_uniform(uniform, target_view(_targets[source_pass]), allocator);
            }
            else if (i + 1 == static_cast<size_t>(depth)) {
                set_frame_uniform(uniform, original, allocator);
            }
        }

        for (const AliasUniform &alias : pass.aliases) {
            if (alias.pass_index < i && alias.pass_index < _targets.size())
                set_frame_uniform(alias.frame, target_view(_targets[alias.pass_index]), allocator);
        }

        for (int n = 0; n < MaxFrameHistory; ++n) {
            if (!pass.prev[n].used() && !pass.original_history[n].used()) continue;
            TextureView history = original;
            if (n < static_cast<int>(_prev_history.size()) &&
                _prev_history[static_cast<size_t>(n)].texture)
                history = target_view(_prev_history[static_cast<size_t>(n)]);
            set_frame_uniform(pass.prev[n], history, allocator);
            set_frame_uniform(pass.original_history[n], history, allocator);
        }

        allocator.next = ags_lut_bind(pass.program, allocator.next, usable_units);
        glActiveTexture(GL_TEXTURE0);

        glEnableVertexAttribArray(0);
        glEnableVertexAttribArray(1);
        glDisableVertexAttribArray(2);
        glVertexAttrib4f(2, 1.f, 1.f, 1.f, 1.f);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), &kQuad[0].x);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), &kQuad[0].u);
        bind_tex_coord_attrib(pass.lut_tex_coord);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

        if (!last) current_texture = _targets[i].texture;
    }

    if (render_ok && _uses_feedback && _feedback_pass >= 0) {
        const size_t index = static_cast<size_t>(_feedback_pass);
        if (index + 1 < _passes.size()) {
            if (!copy_target_to_target(_targets[index], _feedback, fbo_error)) {
                std::fprintf(stderr, "AGS shader: %s\n", fbo_error.c_str());
                render_ok = false;
            }
        }
        else {
            pBindFbo(GL_FRAMEBUFFER_EXT, static_cast<GLuint>(old_fbo));
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, _feedback.texture);
            glCopyTexSubImage2D(GL_TEXTURE_2D,
                                0, 0, 0, 0, 0,
                                _feedback.width,
                                _feedback.height);
            if (glGetError() != GL_NO_ERROR) {
                std::fprintf(stderr, "AGS shader: failed to copy final feedback pass\n");
                render_ok = false;
            }
        }
    }

    if (render_ok && _uses_prev_history && !_prev_history.empty()) {
        Target recycled = _prev_history.back();
        for (size_t i = _prev_history.size() - 1; i > 0; --i)
            _prev_history[i] = _prev_history[i - 1];
        _prev_history[0] = recycled;
        if (!copy_texture_to_target(input_texture,
                                    input_width,
                                    input_height,
                                    GL_RGBA,
                                    _prev_history[0],
                                    fbo_error)) {
            std::fprintf(stderr, "AGS shader: %s\n", fbo_error.c_str());
        }
        else {
            _prev_history[0].input_width = input_width;
            _prev_history[0].input_height = input_height;
        }
    }

    glUseProgram(static_cast<GLuint>(old_program));
    for (GLint index = 0; index < max_vertex_attribs; ++index)
        restore_attrib(static_cast<GLuint>(index), attrib[static_cast<size_t>(index)]);
    glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(old_array_buffer));
    pBindFbo(GL_FRAMEBUFFER_EXT, static_cast<GLuint>(old_fbo));
    glViewport(old_viewport[0], old_viewport[1], old_viewport[2], old_viewport[3]);

    for (int unit = 0; unit < usable_units; ++unit) {
        glActiveTexture(GL_TEXTURE0 + unit);
        glBindTexture(GL_TEXTURE_2D,
                      static_cast<GLuint>(old_texture_bindings[static_cast<size_t>(unit)]));
    }
    glActiveTexture(static_cast<GLenum>(old_active_texture));

#ifdef GL_FRAMEBUFFER_SRGB
    if (framebuffer_srgb) glEnable(GL_FRAMEBUFFER_SRGB);
    else glDisable(GL_FRAMEBUFFER_SRGB);
#endif
    if (blend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
    if (depth_test) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (cull) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
    if (scissor) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);

    if (render_ok) ++_frame_count;
}
