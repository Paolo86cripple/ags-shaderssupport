#include "shader_pipeline_v3.h"
#include "lut_manager.h"

#include <GL/gl.h>
#include <GL/glext.h>
#include <SDL2/SDL.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {
using GenFbo = void (*)(GLsizei, GLuint *);
using DeleteFbo = void (*)(GLsizei, const GLuint *);
using BindFbo = void (*)(GLenum, GLuint);
using AttachTex = void (*)(GLenum, GLenum, GLenum, GLuint, GLint);
using CheckFbo = GLenum (*)(GLenum);
GenFbo pGenFbo = nullptr;
DeleteFbo pDeleteFbo = nullptr;
BindFbo pBindFbo = nullptr;
AttachTex pAttachTex = nullptr;
CheckFbo pCheckFbo = nullptr;

constexpr int MaxSavedTextureUnits = 32;
constexpr int FirstAliasTextureUnit = ShaderPipelineV3::MaxPrevPasses + 1;

const char *kVertex =
    "#version 120\n"
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

std::string trim(const std::string &s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return {};
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

std::string lower(std::string s) {
    for (char &c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string unquote(std::string s) {
    s = trim(s);
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"') s = s.substr(1, s.size() - 2);
    return s;
}

std::string parent_dir(const std::string &p) {
    size_t n = p.find_last_of("/\\");
    return n == std::string::npos ? "." : p.substr(0, n);
}

std::string join_path(const std::string &d, const std::string &p) {
    if (p.empty() || p.front() == '/' || (p.size() > 1 && p[1] == ':')) return p;
    return d + "/" + p;
}

bool suffix(const std::string &s, const char *ext) {
    size_t n = std::strlen(ext);
    return s.size() >= n && lower(s.substr(s.size() - n)) == lower(ext);
}

std::string combined_stage(const std::string &source, const char *define) {
    std::string prefix = std::string("#define ") + define + "\n";
    size_t version = source.find("#version");
    if (version != std::string::npos) {
        size_t end = source.find('\n', version);
        if (end != std::string::npos) return source.substr(0, end + 1) + prefix + source.substr(end + 1);
    }
    return prefix + source;
}

bool combined_shader(const std::string &s) {
    return s.find("defined(VERTEX)") != std::string::npos ||
           s.find("defined(FRAGMENT)") != std::string::npos;
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

ShaderPipelineV3::WrapMode parse_wrap(const std::string &s) {
    std::string v = lower(unquote(s));
    if (v == "clamp_to_edge") return ShaderPipelineV3::WrapMode::ClampToEdge;
    if (v == "repeat") return ShaderPipelineV3::WrapMode::Repeat;
    if (v == "mirrored_repeat") return ShaderPipelineV3::WrapMode::MirroredRepeat;
    return ShaderPipelineV3::WrapMode::ClampToBorder;
}

GLenum wrap_gl(ShaderPipelineV3::WrapMode mode) {
    switch (mode) {
        case ShaderPipelineV3::WrapMode::ClampToEdge: return GL_CLAMP_TO_EDGE;
        case ShaderPipelineV3::WrapMode::Repeat: return GL_REPEAT;
        case ShaderPipelineV3::WrapMode::MirroredRepeat: return GL_MIRRORED_REPEAT;
        default: return GL_CLAMP_TO_BORDER;
    }
}
}

ShaderPipelineV3::~ShaderPipelineV3() { clear(); }

void ShaderPipelineV3::destroy_target(Target &t) {
    if (t.fbo && pDeleteFbo) pDeleteFbo(1, &t.fbo);
    if (t.texture) glDeleteTextures(1, &t.texture);
    t = Target();
}

void ShaderPipelineV3::clear() {
    for (Pass &p : _passes)
        if (p.program) glDeleteProgram(p.program);
    _passes.clear();
    if (pDeleteFbo)
        for (Target &t : _targets) destroy_target(t);
    _targets.clear();
    _parameters.clear();
    ags_lut_clear();
    _frame_count = 0;
}

void ShaderPipelineV3::set_parameter(const std::string &name, float value, bool overwrite) {
    for (Parameter &p : _parameters) {
        if (p.name == name) {
            if (overwrite) p.value = value;
            return;
        }
    }
    Parameter p;
    p.name = name;
    p.value = value;
    _parameters.push_back(p);
}

bool ShaderPipelineV3::load_text(const std::string &path, std::string &text, std::string &error) const {
    std::ifstream file(path.c_str(), std::ios::binary);
    if (!file) { error = "cannot read shader: " + path; return false; }
    std::ostringstream ss;
    ss << file.rdbuf();
    text = ss.str();
    if (text.empty()) { error = "empty shader: " + path; return false; }
    return true;
}

bool ShaderPipelineV3::compile_shader(unsigned type, const std::string &source, unsigned &shader, std::string &error) const {
    shader = glCreateShader(static_cast<GLenum>(type));
    if (!shader) { error = "glCreateShader failed"; return false; }
    const char *src = source.c_str();
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);
    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (ok == GL_TRUE) return true;
    GLint n = 0;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &n);
    std::vector<char> log(static_cast<size_t>(std::max(n, 1)));
    if (n) glGetShaderInfoLog(shader, n, nullptr, log.data());
    error = log.data();
    glDeleteShader(shader);
    shader = 0;
    return false;
}

bool ShaderPipelineV3::create_program(const std::string &vs, const std::string &fs, unsigned &program, std::string &error) const {
    unsigned v = 0, f = 0;
    if (!compile_shader(GL_VERTEX_SHADER, vs, v, error)) return false;
    if (!compile_shader(GL_FRAGMENT_SHADER, fs, f, error)) {
        glDeleteShader(v);
        return false;
    }
    program = glCreateProgram();
    if (!program) {
        glDeleteShader(v);
        glDeleteShader(f);
        error = "glCreateProgram failed";
        return false;
    }
    glAttachShader(program, v);
    glAttachShader(program, f);
    glBindAttribLocation(program, 0, "VertexCoord");
    glBindAttribLocation(program, 1, "TexCoord");
    glBindAttribLocation(program, 2, "COLOR");
    glBindAttribLocation(program, 0, "aPosition");
    glBindAttribLocation(program, 1, "aTexCoord");
    glLinkProgram(program);
    glDeleteShader(v);
    glDeleteShader(f);
    GLint ok = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (ok == GL_TRUE) return true;
    GLint n = 0;
    glGetProgramiv(program, GL_INFO_LOG_LENGTH, &n);
    std::vector<char> log(static_cast<size_t>(std::max(n, 1)));
    if (n) glGetProgramInfoLog(program, n, nullptr, log.data());
    error = log.data();
    glDeleteProgram(program);
    program = 0;
    return false;
}

bool ShaderPipelineV3::add_pass(const std::string &path, const Pass *preset, std::string &error) {
    std::string source;
    if (!load_text(path, source, error)) return false;

    std::istringstream pragma_stream(source);
    std::string pragma_line;
    while (std::getline(pragma_stream, pragma_line)) {
        pragma_line = trim(pragma_line);
        if (pragma_line.rfind("#pragma parameter ", 0) != 0) continue;
        std::istringstream head(pragma_line.substr(18));
        std::string name;
        head >> name;
        if (name.empty()) continue;
        size_t q1 = pragma_line.find('"');
        size_t q2 = q1 == std::string::npos ? std::string::npos : pragma_line.find('"', q1 + 1);
        if (q2 == std::string::npos) continue;
        std::istringstream values(pragma_line.substr(q2 + 1));
        float def = 0.f;
        if (values >> def) set_parameter(name, def, false);
    }

    Pass pass;
    if (preset) pass = *preset;
    pass.source_path = path;
    std::string vs = combined_shader(source) ? combined_stage(source, "VERTEX") : std::string(kVertex);
    std::string fs = combined_shader(source) ? combined_stage(source, "FRAGMENT") : source;
    if (!create_program(vs, fs, pass.program, error)) return false;

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
    pass.texel_size = glGetUniformLocation(pass.program, "uTexelSize");
    pass.frame_count = glGetUniformLocation(pass.program, "FrameCount");
    if (pass.frame_count < 0) pass.frame_count = glGetUniformLocation(pass.program, "uFrameCount");
    pass.frame_direction = glGetUniformLocation(pass.program, "FrameDirection");
    pass.time = glGetUniformLocation(pass.program, "uTime");
    pass.mvp_matrix = glGetUniformLocation(pass.program, "MVPMatrix");

    for (int n = 0; n < MaxPrevPasses; ++n) {
        int ordinal = n + 1;
        std::string tn = n == 0 ? "PassPrevTexture" : "PassPrev" + std::to_string(ordinal) + "Texture";
        std::string sn = n == 0 ? "PassPrevTextureSize" : "PassPrev" + std::to_string(ordinal) + "TextureSize";
        pass.prev_texture[n] = glGetUniformLocation(pass.program, tn.c_str());
        pass.prev_texture_size[n] = glGetUniformLocation(pass.program, sn.c_str());
        if (n == 0) {
            if (pass.prev_texture[n] < 0) pass.prev_texture[n] = glGetUniformLocation(pass.program, "PassPrev1Texture");
            if (pass.prev_texture_size[n] < 0) pass.prev_texture_size[n] = glGetUniformLocation(pass.program, "PassPrev1TextureSize");
        }
    }

    _passes.push_back(pass);
    return true;
}

bool ShaderPipelineV3::parse_chain(const std::string &path, std::vector<std::string> &out, std::string &error) const {
    std::string source;
    if (!load_text(path, source, error)) return false;
    std::istringstream in(source);
    std::string line;
    const std::string d = parent_dir(path);
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line.front() == '#') continue;
        if (line.compare(0, 5, "pass=") == 0) {
            std::string p = unquote(line.substr(5));
            if (!p.empty()) out.push_back(join_path(d, p));
        }
    }
    if (out.empty()) { error = "shader chain has no passes: " + path; return false; }
    return true;
}

ShaderPipelineV3::ScaleType parse_type(const std::string &s) {
    std::string v = lower(unquote(s));
    if (v == "viewport") return ShaderPipelineV3::ScaleType::Viewport;
    if (v == "absolute") return ShaderPipelineV3::ScaleType::Absolute;
    return ShaderPipelineV3::ScaleType::Source;
}

bool ShaderPipelineV3::parse_glslp(const std::string &path, std::vector<Pass> &out, std::string &error) {
    std::string source;
    if (!load_text(path, source, error)) return false;
    struct Entry { std::string path; Pass pass; };
    std::vector<Entry> e;
    int count = -1;
    std::vector<Parameter> preset_parameters;
    std::istringstream in(source);
    std::string line;
    const std::string d = parent_dir(path);

    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line.front() == '#') continue;
        size_t q = line.find('=');
        if (q == std::string::npos) continue;
        std::string k = trim(line.substr(0, q));
        std::string v = unquote(line.substr(q + 1));
        if (k == "shaders") { count = parse_int(v, -1); continue; }
        if (k.rfind("shader", 0) == 0 && k.size() > 6 && std::isdigit(static_cast<unsigned char>(k[6]))) {
            int i = parse_int(k.substr(6), -1);
            if (i >= 0) {
                if (static_cast<size_t>(i) >= e.size()) e.resize(static_cast<size_t>(i) + 1);
                e[i].path = join_path(d, v);
            }
            continue;
        }

        size_t n = k.size();
        while (n > 0 && std::isdigit(static_cast<unsigned char>(k[n - 1]))) --n;
        if (n < k.size()) {
            int i = parse_int(k.substr(n), -1);
            if (i < 0) continue;
            if (static_cast<size_t>(i) >= e.size()) e.resize(static_cast<size_t>(i) + 1);
            std::string b = k.substr(0, n);
            if (b == "filter_linear") e[i].pass.filter_linear = parse_bool(v, true);
            else if (b == "wrap_mode") e[i].pass.wrap_mode = parse_wrap(v);
            else if (b == "float_framebuffer") e[i].pass.float_framebuffer = parse_bool(v, false);
            else if (b == "srgb_framebuffer") e[i].pass.srgb_framebuffer = parse_bool(v, false);
            else if (b == "alias") e[i].pass.alias = v;
            else if (b == "scale") e[i].pass.scale_x = e[i].pass.scale_y = parse_float(v, 1.f);
            else if (b == "scale_x") e[i].pass.scale_x = parse_float(v, 1.f);
            else if (b == "scale_y") e[i].pass.scale_y = parse_float(v, 1.f);
            else if (b == "scale_type") {
                auto t = parse_type(v);
                e[i].pass.scale_type_x = t;
                e[i].pass.scale_type_y = t;
            }
            else if (b == "scale_type_x") e[i].pass.scale_type_x = parse_type(v);
            else if (b == "scale_type_y") e[i].pass.scale_type_y = parse_type(v);
            continue;
        }

        if (k == "textures" || k == "parameters") continue;
        float pv = parse_float(v, NAN);
        if (!std::isnan(pv)) {
            Parameter p;
            p.name = k;
            p.value = pv;
            preset_parameters.push_back(p);
        }
    }

    if (count < 0) count = static_cast<int>(e.size());
    if (count <= 0) { error = "glslp has no shaders: " + path; return false; }
    out.clear();
    for (int i = 0; i < count; ++i) {
        if (i >= static_cast<int>(e.size()) || e[i].path.empty()) {
            error = "missing shader" + std::to_string(i) + " in " + path;
            return false;
        }
        if (e[i].pass.float_framebuffer && e[i].pass.srgb_framebuffer) {
            error = "shader" + std::to_string(i) + " cannot use float_framebuffer and srgb_framebuffer together";
            return false;
        }
        e[i].pass.source_path = e[i].path;
        out.push_back(e[i].pass);
    }
    for (const Parameter &p : preset_parameters) set_parameter(p.name, p.value, true);
    return true;
}

bool ShaderPipelineV3::load(const std::string &path, std::string &error) {
    clear();
    if (suffix(path, ".glslp")) {
        std::vector<Pass> p;
        if (!parse_glslp(path, p, error)) return false;
        if (!ags_lut_load_preset(path, error)) { clear(); return false; }
        for (const Pass &x : p)
            if (!add_pass(x.source_path, &x, error)) { clear(); return false; }
    }
    else if (suffix(path, ".agschain")) {
        std::vector<std::string> p;
        if (!parse_chain(path, p, error)) return false;
        for (const std::string &x : p)
            if (!add_pass(x, nullptr, error)) { clear(); return false; }
    }
    else if (!add_pass(path, nullptr, error)) {
        clear();
        return false;
    }
    if (_passes.size() > 1) _targets.resize(_passes.size() - 1);
    return true;
}

bool ShaderPipelineV3::ensure_fbo_functions(std::string &error) { return resolve_fbo(error); }

bool ShaderPipelineV3::ensure_target(Target &t, int w, int h, bool use_float, bool use_srgb, std::string &error) {
    if (!resolve_fbo(error)) return false;
    GLint format = GL_RGBA;
#ifdef GL_RGBA16F_ARB
    if (use_float) format = GL_RGBA16F_ARB;
#endif
#ifdef GL_SRGB8_ALPHA8_EXT
    if (use_srgb) format = GL_SRGB8_ALPHA8_EXT;
#endif
    if (t.fbo && t.width == w && t.height == h && t.format == format) return true;
    destroy_target(t);
    t.width = w;
    t.height = h;
    t.format = format;
    glGenTextures(1, &t.texture);
    glBindTexture(GL_TEXTURE_2D, t.texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, format, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    pGenFbo(1, &t.fbo);
    pBindFbo(GL_FRAMEBUFFER_EXT, t.fbo);
    pAttachTex(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_TEXTURE_2D, t.texture, 0);
    if (pCheckFbo(GL_FRAMEBUFFER_EXT) != GL_FRAMEBUFFER_COMPLETE_EXT) {
        error = "incomplete shader framebuffer";
        destroy_target(t);
        pBindFbo(GL_FRAMEBUFFER_EXT, 0);
        return false;
    }
    return true;
}

int dim(ShaderPipelineV3::ScaleType t, float s, int src, int view) {
    if (t == ShaderPipelineV3::ScaleType::Viewport) return std::max(1, static_cast<int>(std::lround(view * s)));
    if (t == ShaderPipelineV3::ScaleType::Absolute) return std::max(1, static_cast<int>(std::lround(s)));
    return std::max(1, static_cast<int>(std::lround(src * s)));
}

void ShaderPipelineV3::apply(unsigned input_texture, int iw, int ih, int ow, int oh) {
    if (_passes.empty() || input_texture == 0 || iw <= 0 || ih <= 0 || ow <= 0 || oh <= 0) return;
    if (_passes.size() > 1) {
        std::string e;
        if (!resolve_fbo(e)) { std::fprintf(stderr, "AGS shader: %s\n", e.c_str()); return; }
    }

    GLint old_program = 0, old_array = 0, old_active = GL_TEXTURE0, old_fbo = 0, old_view[4];
    glGetIntegerv(GL_CURRENT_PROGRAM, &old_program);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &old_array);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &old_active);
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &old_fbo);
    glGetIntegerv(GL_VIEWPORT, old_view);

    GLint max_units = 0;
    glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &max_units);
    const int saved_units = std::min(max_units, MaxSavedTextureUnits);
    std::vector<GLint> old_tex_units(static_cast<size_t>(saved_units), 0);
    for (int u = 0; u < saved_units; ++u) {
        glActiveTexture(GL_TEXTURE0 + u);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &old_tex_units[static_cast<size_t>(u)]);
    }

    GLboolean blend = glIsEnabled(GL_BLEND);
    GLboolean depth_test = glIsEnabled(GL_DEPTH_TEST);
    GLboolean cull = glIsEnabled(GL_CULL_FACE);
    GLboolean scissor = glIsEnabled(GL_SCISSOR_TEST);
#ifdef GL_FRAMEBUFFER_SRGB
    GLboolean framebuffer_srgb = glIsEnabled(GL_FRAMEBUFFER_SRGB);
#endif
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);

    unsigned tex = input_texture;
    int sw = iw, sh = ih;
    const GLfloat identity[16] = {
        1,0,0,0,
        0,1,0,0,
        0,0,1,0,
        0,0,0,1
    };

    for (size_t i = 0; i < _passes.size(); ++i) {
        const Pass &p = _passes[i];
        bool last = i + 1 == _passes.size();
        int w = ow, h = oh;
        if (!last) {
            w = dim(p.scale_type_x, p.scale_x, sw, ow);
            h = dim(p.scale_type_y, p.scale_y, sh, oh);
        }

        if (last) {
            if (pBindFbo) pBindFbo(GL_FRAMEBUFFER_EXT, static_cast<GLuint>(old_fbo));
        }
        else {
            std::string e;
            if (i >= _targets.size() || !ensure_target(_targets[i], w, h, p.float_framebuffer, p.srgb_framebuffer, e)) {
                std::fprintf(stderr, "AGS shader: %s\n", e.c_str());
                break;
            }
            pBindFbo(GL_FRAMEBUFFER_EXT, _targets[i].fbo);
        }

#ifdef GL_FRAMEBUFFER_SRGB
        if (!last && p.srgb_framebuffer) glEnable(GL_FRAMEBUFFER_SRGB);
        else glDisable(GL_FRAMEBUFFER_SRGB);
#endif

        glViewport(0, 0, w, h);
        glUseProgram(p.program);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, tex);
        GLenum wrap = wrap_gl(p.wrap_mode);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, p.filter_linear ? GL_LINEAR : GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, p.filter_linear ? GL_LINEAR : GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);

        if (p.texture >= 0) glUniform1i(p.texture, 0);
        if (p.input_size >= 0) glUniform2f(p.input_size, static_cast<float>(sw), static_cast<float>(sh));
        if (p.texture_size >= 0) glUniform2f(p.texture_size, static_cast<float>(sw), static_cast<float>(sh));
        if (p.output_size >= 0) glUniform2f(p.output_size, static_cast<float>(w), static_cast<float>(h));
        if (p.original_size >= 0) glUniform2f(p.original_size, static_cast<float>(iw), static_cast<float>(ih));
        if (p.texel_size >= 0) glUniform2f(p.texel_size, 1.f / std::max(sw, 1), 1.f / std::max(sh, 1));
        if (p.frame_count >= 0) glUniform1i(p.frame_count, static_cast<GLint>(_frame_count));
        if (p.frame_direction >= 0) glUniform1i(p.frame_direction, 1);
        if (p.time >= 0) glUniform1f(p.time, static_cast<float>(SDL_GetTicks()) / 1000.f);
        if (p.mvp_matrix >= 0) glUniformMatrix4fv(p.mvp_matrix, 1, GL_FALSE, identity);

        for (const Parameter &param : _parameters) {
            GLint loc = glGetUniformLocation(p.program, param.name.c_str());
            if (loc >= 0) glUniform1f(loc, param.value);
        }

        for (int n = 0; n < MaxPrevPasses; ++n) {
            if (p.prev_texture[n] < 0 && p.prev_texture_size[n] < 0) continue;
            size_t d = static_cast<size_t>(n + 1);
            if (i < d) continue;
            size_t sp = i - d;
            if (sp >= _targets.size()) continue;
            const Target &history = _targets[sp];
            int unit = n + 1;
            if (unit >= saved_units) continue;
            glActiveTexture(GL_TEXTURE0 + unit);
            glBindTexture(GL_TEXTURE_2D, history.texture);
            if (p.prev_texture[n] >= 0) glUniform1i(p.prev_texture[n], unit);
            if (p.prev_texture_size[n] >= 0)
                glUniform2f(p.prev_texture_size[n], static_cast<float>(history.width), static_cast<float>(history.height));
        }

        int alias_unit = FirstAliasTextureUnit;
        for (size_t j = 0; j < i && alias_unit < saved_units; ++j) {
            if (_passes[j].alias.empty() || j >= _targets.size()) continue;
            const std::string &alias = _passes[j].alias;
            GLint sampler = glGetUniformLocation(p.program, alias.c_str());
            if (sampler < 0) sampler = glGetUniformLocation(p.program, (alias + "Texture").c_str());
            GLint size_loc = glGetUniformLocation(p.program, (alias + "Size").c_str());
            if (size_loc < 0) size_loc = glGetUniformLocation(p.program, (alias + "TextureSize").c_str());
            if (sampler < 0 && size_loc < 0) continue;
            const Target &t = _targets[j];
            glActiveTexture(GL_TEXTURE0 + alias_unit);
            glBindTexture(GL_TEXTURE_2D, t.texture);
            if (sampler >= 0) glUniform1i(sampler, alias_unit);
            if (size_loc >= 0) glUniform2f(size_loc, static_cast<float>(t.width), static_cast<float>(t.height));
            ++alias_unit;
        }

        ags_lut_bind(p.program, alias_unit, saved_units);
        glActiveTexture(GL_TEXTURE0);

        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glEnableVertexAttribArray(0);
        glEnableVertexAttribArray(1);
        glDisableVertexAttribArray(2);
        glVertexAttrib4f(2, 1, 1, 1, 1);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), &kQuad[0].x);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), &kQuad[0].u);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        glDisableVertexAttribArray(0);
        glDisableVertexAttribArray(1);

        if (!last) {
            tex = _targets[i].texture;
            sw = w;
            sh = h;
        }
    }

    glUseProgram(old_program);
    glBindBuffer(GL_ARRAY_BUFFER, old_array);
    if (pBindFbo) pBindFbo(GL_FRAMEBUFFER_EXT, static_cast<GLuint>(old_fbo));
    glViewport(old_view[0], old_view[1], old_view[2], old_view[3]);
    for (int u = 0; u < saved_units; ++u) {
        glActiveTexture(GL_TEXTURE0 + u);
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(old_tex_units[static_cast<size_t>(u)]));
    }
    glActiveTexture(old_active);
#ifdef GL_FRAMEBUFFER_SRGB
    if (framebuffer_srgb) glEnable(GL_FRAMEBUFFER_SRGB);
    else glDisable(GL_FRAMEBUFFER_SRGB);
#endif
    if (blend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
    if (depth_test) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (cull) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
    if (scissor) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
    ++_frame_count;
}
