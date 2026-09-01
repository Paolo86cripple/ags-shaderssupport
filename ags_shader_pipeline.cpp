#include "gfx/ags_shader_pipeline.h"

#if AGS_HAS_OPENGL

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <sstream>
#include <vector>

#include <SDL.h>

#include "debug/out.h"
#include "util/path.h"

namespace AGS
{
namespace Engine
{
namespace OGL
{

namespace
{

static const char *kFullscreenVertexShader =
"#version 120\n"
"attribute vec2 aPosition;\n"
"attribute vec2 aTexCoord;\n"
"varying vec2 vTexCoord;\n"
"void main()\n"
"{\n"
"    vTexCoord = aTexCoord;\n"
"    gl_Position = vec4(aPosition.xy, 0.0, 1.0);\n"
"}\n";

struct QuadVertex
{
    float x;
    float y;
    float u;
    float v;
};

static const QuadVertex kQuad[4] =
{
    { -1.0f, -1.0f, 0.0f, 0.0f },
    {  1.0f, -1.0f, 1.0f, 0.0f },
    { -1.0f,  1.0f, 0.0f, 1.0f },
    {  1.0f,  1.0f, 1.0f, 1.0f }
};

static String ParentDirectory(const String &path)
{
    const char *s = path.GetCStr();
    const char *slash = std::strrchr(s, '/');
#ifdef _WIN32
    const char *backslash = std::strrchr(s, '\\');
    if (!slash || (backslash && backslash > slash))
        slash = backslash;
#endif
    if (!slash)
        return String(".");
    return String(std::string(s, static_cast<size_t>(slash - s)).c_str());
}

static String JoinPath(const String &base, const String &child)
{
    const char *c = child.GetCStr();
    if (c[0] == '/' || c[0] == '\\' ||
        (std::strlen(c) >= 2 &&
         std::isalpha(static_cast<unsigned char>(c[0])) && c[1] == ':'))
        return child;
    return Path::ConcatPaths(base, child);
}

} // namespace

AGSShaderPipeline::AGSShaderPipeline() = default;

AGSShaderPipeline::~AGSShaderPipeline()
{
    Clear();
}

bool AGSShaderPipeline::HasSuffixNoCase(const String &value, const char *suffix)
{
    const std::string lhs(value.GetCStr());
    const std::string rhs(suffix);
    if (lhs.size() < rhs.size())
        return false;

    const size_t start = lhs.size() - rhs.size();
    for (size_t i = 0; i < rhs.size(); ++i)
    {
        if (std::tolower(static_cast<unsigned char>(lhs[start + i])) !=
            std::tolower(static_cast<unsigned char>(rhs[i])))
            return false;
    }
    return true;
}

bool AGSShaderPipeline::LoadTextFile(const String &path, std::string &text,
                                     String &error) const
{
    std::ifstream file(path.GetCStr(), std::ios::binary);
    if (!file)
    {
        error = String::FromFormat("Could not open shader file '%s'.", path.GetCStr());
        return false;
    }

    std::ostringstream stream;
    stream << file.rdbuf();
    text = stream.str();

    if (text.empty())
    {
        error = String::FromFormat("Shader file '%s' is empty.", path.GetCStr());
        return false;
    }
    return true;
}

bool AGSShaderPipeline::CompileShader(GLenum type, const char *source,
                                       GLuint &shader, const String &name,
                                       String &error)
{
    shader = glCreateShader(type);
    if (shader == 0u)
    {
        error = String::FromFormat("Could not create OpenGL shader '%s'.", name.GetCStr());
        return false;
    }

    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint compiled = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (compiled != GL_FALSE)
        return true;

    GLint log_len = 0;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &log_len);
    std::vector<GLchar> log(static_cast<size_t>(std::max(log_len, 1)));
    if (log_len > 0)
        glGetShaderInfoLog(shader, log_len, nullptr, log.data());

    error = String::FromFormat("OpenGL shader '%s' failed to compile:\n%s",
                               name.GetCStr(), log.data());
    glDeleteShader(shader);
    shader = 0u;
    return false;
}

bool AGSShaderPipeline::CreateProgram(const char *fragment_source,
                                       const String &name,
                                       GLuint &program, String &error)
{
    GLuint vertex_shader = 0u;
    GLuint fragment_shader = 0u;

    if (!CompileShader(GL_VERTEX_SHADER, kFullscreenVertexShader,
                       vertex_shader, name, error))
        return false;

    if (!CompileShader(GL_FRAGMENT_SHADER, fragment_source,
                       fragment_shader, name, error))
    {
        glDeleteShader(vertex_shader);
        return false;
    }

    program = glCreateProgram();
    if (program == 0u)
    {
        glDeleteShader(vertex_shader);
        glDeleteShader(fragment_shader);
        error = String::FromFormat("Could not create OpenGL program '%s'.", name.GetCStr());
        return false;
    }

    glAttachShader(program, vertex_shader);
    glAttachShader(program, fragment_shader);
    glBindAttribLocation(program, 0, "aPosition");
    glBindAttribLocation(program, 1, "aTexCoord");
    glLinkProgram(program);

    GLint linked = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (linked == GL_FALSE)
    {
        GLint log_len = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &log_len);
        std::vector<GLchar> log(static_cast<size_t>(std::max(log_len, 1)));
        if (log_len > 0)
            glGetProgramInfoLog(program, log_len, nullptr, log.data());

        error = String::FromFormat("OpenGL shader program '%s' failed to link:\n%s",
                                   name.GetCStr(), log.data());
        glDeleteProgram(program);
        program = 0u;
    }

    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);
    return program != 0u;
}

bool AGSShaderPipeline::CreatePass(const String &path, Pass &pass, String &error)
{
    std::string source;
    if (!LoadTextFile(path, source, error))
        return false;

    if (!CreateProgram(source.c_str(), path, pass.Program, error))
        return false;

    pass.Name = path;
    pass.Position = 0;
    pass.TexCoord = 1;
    pass.Texture = glGetUniformLocation(pass.Program, "uTexture");
    pass.InputSize = glGetUniformLocation(pass.Program, "uInputSize");
    pass.OutputSize = glGetUniformLocation(pass.Program, "uOutputSize");
    pass.OriginalSize = glGetUniformLocation(pass.Program, "uOriginalSize");
    pass.TexelSize = glGetUniformLocation(pass.Program, "uTexelSize");
    pass.Time = glGetUniformLocation(pass.Program, "uTime");
    pass.FrameCount = glGetUniformLocation(pass.Program, "uFrameCount");
    return true;
}

bool AGSShaderPipeline::ParseChain(const String &chain_path,
                                   std::vector<String> &shader_paths,
                                   String &error) const
{
    std::string text;
    if (!LoadTextFile(chain_path, text, error))
        return false;

    const String chain_dir = ParentDirectory(chain_path);
    std::istringstream stream(text);
    std::string line;

    while (std::getline(stream, line))
    {
        const size_t first = line.find_first_not_of(" \t\r");
        if (first == std::string::npos)
            continue;
        line.erase(0, first);

        if (line[0] == '#')
            continue;
        if (line.compare(0, 5, "pass=") != 0)
            continue;

        line = line.substr(5);
        const size_t last = line.find_last_not_of(" \t\r");
        if (last == std::string::npos)
            continue;
        line.resize(last + 1);

        if (line.size() >= 2 && line.front() == '"' && line.back() == '"')
            line = line.substr(1, line.size() - 2);

        if (!line.empty())
            shader_paths.push_back(JoinPath(chain_dir, String(line.c_str())));
    }

    if (shader_paths.empty())
    {
        error = String::FromFormat("Shader chain '%s' contains no pass= entries.",
                                   chain_path.GetCStr());
        return false;
    }
    return true;
}

void AGSShaderPipeline::DestroyPass(Pass &pass)
{
    if (pass.Program != 0u)
        glDeleteProgram(pass.Program);
    pass = Pass();
}

void AGSShaderPipeline::DestroyRenderTarget(RenderTarget &target)
{
    if (target.Fbo != 0u)
        glDeleteFramebuffers(1, &target.Fbo);
    if (target.Texture != 0u)
        glDeleteTextures(1, &target.Texture);
    target = RenderTarget();
}

void AGSShaderPipeline::Clear()
{
    for (auto &pass : _passes)
        DestroyPass(pass);
    _passes.clear();

    for (auto &target : _targets)
        DestroyRenderTarget(target);
    _targets.clear();

    _targetSize = Size();
    _frameCount = 0u;
}

bool AGSShaderPipeline::Load(const String &path, String &error)
{
    Clear();

    if (path.IsEmpty())
        return false;

    std::vector<String> shader_paths;
    if (HasSuffixNoCase(path, ".agschain"))
    {
        if (!ParseChain(path, shader_paths, error))
            return false;
    }
    else
    {
        shader_paths.push_back(path);
    }

    for (const auto &shader_path : shader_paths)
    {
        Pass pass;
        if (!CreatePass(shader_path, pass, error))
        {
            Clear();
            return false;
        }
        _passes.push_back(pass);
    }

    return true;
}

bool AGSShaderPipeline::EnsureRenderTargets(int width, int height,
                                            size_t count, String &error)
{
    if (_targets.size() == count &&
        _targetSize.Width == width &&
        _targetSize.Height == height)
        return true;

    for (auto &target : _targets)
        DestroyRenderTarget(target);
    _targets.clear();

    for (size_t i = 0; i < count; ++i)
    {
        RenderTarget target;
        target.SizePx = Size(width, height);

        glGenTextures(1, &target.Texture);
        glBindTexture(GL_TEXTURE_2D, target.Texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

        glGenFramebuffers(1, &target.Fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, target.Fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, target.Texture, 0);

        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        {
            DestroyRenderTarget(target);
            for (auto &existing : _targets)
                DestroyRenderTarget(existing);
            _targets.clear();
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            error = String::FromFormat("Could not create shader framebuffer (%d x %d).",
                                       width, height);
            return false;
        }

        _targets.push_back(target);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    _targetSize = Size(width, height);
    return true;
}

void AGSShaderPipeline::Apply(GLuint input_texture,
                              const Size &input_size,
                              GLuint output_fbo,
                              const Size &output_size,
                              const Rect &output_viewport)
{
    if (_passes.empty() || input_texture == 0u || output_viewport.IsEmpty())
        return;

    String error;
    const size_t intermediate_count = _passes.size() > 1 ? 2u : 0u;
    if (!EnsureRenderTargets(output_size.Width, output_size.Height,
                             intermediate_count, error))
    {
        Debug::Printf(kDbgMsg_Error, "AGS shader pipeline: %s", error.GetCStr());
        return;
    }

    const float time = static_cast<float>(SDL_GetTicks()) / 1000.0f;
    GLuint current_texture = input_texture;
    Size current_size = input_size;

    glDisable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    for (size_t i = 0; i < _passes.size(); ++i)
    {
        const bool last = i + 1 == _passes.size();
        const size_t target_index = i & 1u;
        const GLuint target_fbo = last ? output_fbo : _targets[target_index].Fbo;

        glBindFramebuffer(GL_FRAMEBUFFER, target_fbo);
        if (last)
            glViewport(output_viewport.Left, output_viewport.Top,
                       output_viewport.GetWidth(), output_viewport.GetHeight());
        else
            glViewport(0, 0, output_size.Width, output_size.Height);

        glUseProgram(_passes[i].Program);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, current_texture);

        if (_passes[i].Texture >= 0)
            glUniform1i(_passes[i].Texture, 0);
        if (_passes[i].InputSize >= 0)
            glUniform2f(_passes[i].InputSize,
                        static_cast<float>(current_size.Width),
                        static_cast<float>(current_size.Height));
        if (_passes[i].OutputSize >= 0)
            glUniform2f(_passes[i].OutputSize,
                        static_cast<float>(output_size.Width),
                        static_cast<float>(output_size.Height));
        if (_passes[i].OriginalSize >= 0)
            glUniform2f(_passes[i].OriginalSize,
                        static_cast<float>(input_size.Width),
                        static_cast<float>(input_size.Height));
        if (_passes[i].TexelSize >= 0)
            glUniform2f(_passes[i].TexelSize,
                        1.0f / static_cast<float>(std::max(current_size.Width, 1)),
                        1.0f / static_cast<float>(std::max(current_size.Height, 1)));
        if (_passes[i].Time >= 0)
            glUniform1f(_passes[i].Time, time);
        if (_passes[i].FrameCount >= 0)
            glUniform1f(_passes[i].FrameCount,
                        static_cast<float>(_frameCount));

        glVertexAttribPointer(_passes[i].Position, 2, GL_FLOAT, GL_FALSE,
                              sizeof(QuadVertex), &kQuad[0].x);
        glEnableVertexAttribArray(_passes[i].Position);
        glVertexAttribPointer(_passes[i].TexCoord, 2, GL_FLOAT, GL_FALSE,
                              sizeof(QuadVertex), &kQuad[0].u);
        glEnableVertexAttribArray(_passes[i].TexCoord);

        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

        glDisableVertexAttribArray(_passes[i].Position);
        glDisableVertexAttribArray(_passes[i].TexCoord);

        if (!last)
        {
            current_texture = _targets[target_index].Texture;
            current_size = output_size;
        }
    }

    glUseProgram(0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, output_fbo);
    glViewport(output_viewport.Left, output_viewport.Top,
               output_viewport.GetWidth(), output_viewport.GetHeight());

    ++_frameCount;
}

} // namespace OGL
} // namespace Engine
} // namespace AGS

#endif
