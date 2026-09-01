#include "gfx/ags_shader_pipeline.h"

#if AGS_HAS_OPENGL

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <SDL.h>

#include "debug/out.h"

namespace AGS
{
namespace Engine
{
namespace OGL
{

namespace
{
static const char *kVertexShader =
    "#version 120\n"
    "attribute vec2 aPosition;\n"
    "attribute vec2 aTexCoord;\n"
    "varying vec2 vTexCoord;\n"
    "void main() {\n"
    "    vTexCoord = aTexCoord;\n"
    "    gl_Position = vec4(aPosition, 0.0, 1.0);\n"
    "}\n";

struct QuadVertex
{
    float x, y, u, v;
};

static const QuadVertex kQuad[] = {
    { -1.f, -1.f, 0.f, 0.f },
    {  1.f, -1.f, 1.f, 0.f },
    { -1.f,  1.f, 0.f, 1.f },
    {  1.f,  1.f, 1.f, 1.f }
};

static String ParentDir(const String &path)
{
    const std::string p(path.GetCStr());
    const size_t at = p.find_last_of("/\\");
    return at == std::string::npos ? String(".") : String(p.substr(0, at).c_str());
}

static String JoinPath(const String &base, const String &child)
{
    const std::string c(child.GetCStr());
    if (!c.empty() && (c[0] == '/' || c[0] == '\\'))
        return child;
    if (c.size() >= 2 && std::isalpha(static_cast<unsigned char>(c[0])) && c[1] == ':')
        return child;
    return Path::ConcatPaths(base, child);
}
}

AGSShaderPipeline::AGSShaderPipeline() = default;

AGSShaderPipeline::~AGSShaderPipeline()
{
    Clear();
}

bool AGSShaderPipeline::HasSuffixNoCase(const String &value, const char *suffix)
{
    const std::string a(value.GetCStr());
    const std::string b(suffix);
    if (a.size() < b.size())
        return false;
    const size_t off = a.size() - b.size();
    for (size_t i = 0; i < b.size(); ++i)
    {
        if (std::tolower(static_cast<unsigned char>(a[off + i])) !=
            std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    }
    return true;
}

bool AGSShaderPipeline::LoadTextFile(const String &path, std::string &text, String &error) const
{
    std::ifstream file(path.GetCStr(), std::ios::binary);
    if (!file)
    {
        error = String::FromFormat("Could not open shader file '%s'.", path.GetCStr());
        return false;
    }
    std::ostringstream ss;
    ss << file.rdbuf();
    text = ss.str();
    if (text.empty())
    {
        error = String::FromFormat("Shader file '%s' is empty.", path.GetCStr());
        return false;
    }
    return true;
}

bool AGSShaderPipeline::CompileShader(GLenum type, const char *source, GLuint &shader,
    const String &name, String &error)
{
    shader = glCreateShader(type);
    if (!shader)
    {
        error = String::FromFormat("Could not create OpenGL shader '%s'.", name.GetCStr());
        return false;
    }
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (ok == GL_TRUE)
        return true;

    GLint log_len = 0;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &log_len);
    std::vector<GLchar> log(static_cast<size_t>(std::max(log_len, 1)));
    if (log_len > 0)
        glGetShaderInfoLog(shader, log_len, nullptr, log.data());
    error = String::FromFormat("OpenGL shader '%s' failed to compile:\n%s",
        name.GetCStr(), log.data());
    glDeleteShader(shader);
    shader = 0;
    return false;
}

bool AGSShaderPipeline::CreateProgram(const char *fragment_source, const String &name,
    GLuint &program, String &error)
{
    GLuint vs = 0, fs = 0;
    if (!CompileShader(GL_VERTEX_SHADER, kVertexShader, vs, name, error))
        return false;
    if (!CompileShader(GL_FRAGMENT_SHADER, fragment_source, fs, name, error))
    {
        glDeleteShader(vs);
        return false;
    }

    program = glCreateProgram();
    if (!program)
    {
        glDeleteShader(vs);
        glDeleteShader(fs);
        error = String::FromFormat("Could not create OpenGL program '%s'.", name.GetCStr());
        return false;
    }

    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glBindAttribLocation(program, 0, "aPosition");
    glBindAttribLocation(program, 1, "aTexCoord");
    glLinkProgram(program);

    GLint linked = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    glDeleteShader(vs);
    glDeleteShader(fs);

    if (linked == GL_TRUE)
        return true;

    GLint log_len = 0;
    glGetProgramiv(program, GL_INFO_LOG_LENGTH, &log_len);
    std::vector<GLchar> log(static_cast<size_t>(std::max(log_len, 1)));
    if (log_len > 0)
        glGetProgramInfoLog(program, log_len, nullptr, log.data());
    error = String::FromFormat("OpenGL shader program '%s' failed to link:\n%s",
        name.GetCStr(), log.data());
    glDeleteProgram(program);
    program = 0;
    return false;
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
    std::vector<String> &shader_paths, String &error) const
{
    std::string text;
    if (!LoadTextFile(chain_path, text, error))
        return false;

    const String dir = ParentDir(chain_path);
    std::istringstream ss(text);
    std::string line;
    while (std::getline(ss, line))
    {
        const size_t first = line.find_first_not_of(" \t\r");
        if (first == std::string::npos)
            continue;
        line.erase(0, first);
        if (line.empty() || line[0] == '#')
            continue;
        if (line.compare(0, 5, "pass=") != 0)
            continue;
        line.erase(0, 5);
        const size_t last = line.find_last_not_of(" \t\r");
        if (last == std::string::npos)
            continue;
        line.resize(last + 1);
        shader_paths.push_back(JoinPath(dir, String(line.c_str())));
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
    if (pass.Program)
        glDeleteProgram(pass.Program);
    pass = Pass();
}

void AGSShaderPipeline::DestroyRenderTarget(RenderTarget &target)
{
    if (target.Fbo)
        glDeleteFramebuffers(1, &target.Fbo);
    if (target.Texture)
        glDeleteTextures(1, &target.Texture);
    target = RenderTarget();
}

void AGSShaderPipeline::Clear()
{
    for (auto &pass : _passes)
        DestroyPass(pass);
    for (auto &target : _targets)
        DestroyRenderTarget(target);
    _passes.clear();
    _targets.clear();
    _targetSize = Size();
    _frameCount = 0;
}

bool AGSShaderPipeline::Load(const String &path, String &error)
{
    Clear();
    std::vector<String> paths;
    if (HasSuffixNoCase(path, ".agschain"))
    {
        if (!ParseChain(path, paths, error))
            return false;
    }
    else
    {
        paths.push_back(path);
    }

    for (const auto &shader_path : paths)
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

bool AGSShaderPipeline::EnsureRenderTargets(int width, int height, size_t count, String &error)
{
    if (_targets.size() == count && _targetSize.Width == width && _targetSize.Height == height)
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
            error = String::FromFormat("Could not create shader framebuffer (%d x %d).", width, height);
            return false;
        }
        _targets.push_back(target);
    }

    glBindTexture(GL_TEXTURE_2D, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    _targetSize = Size(width, height);
    return true;
}

void AGSShaderPipeline::Apply(GLuint input_texture, const Size &input_size,
    GLuint output_fbo, const Size &output_size, const Rect &output_viewport)
{
    if (_passes.empty() || input_texture == 0 || output_viewport.IsEmpty())
        return;

    String error;
    const size_t intermediates = _passes.size() > 1 ? 2u : 0u;
    if (!EnsureRenderTargets(output_size.Width, output_size.Height, intermediates, error))
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
        const bool last = (i + 1 == _passes.size());
        const size_t target_index = i & 1u;
        const GLuint target_fbo = last ? output_fbo : _targets[target_index].Fbo;

        glBindFramebuffer(GL_FRAMEBUFFER, target_fbo);
        if (last)
        {
            glViewport(output_viewport.Left, output_viewport.Top,
                output_viewport.GetWidth(), output_viewport.GetHeight());
        }
        else
        {
            glViewport(0, 0, output_size.Width, output_size.Height);
        }

        const Pass &pass = _passes[i];
        glUseProgram(pass.Program);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, current_texture);

        if (pass.Texture >= 0) glUniform1i(pass.Texture, 0);
        if (pass.InputSize >= 0) glUniform2f(pass.InputSize,
            static_cast<float>(current_size.Width), static_cast<float>(current_size.Height));
        if (pass.OutputSize >= 0) glUniform2f(pass.OutputSize,
            static_cast<float>(output_size.Width), static_cast<float>(output_size.Height));
        if (pass.OriginalSize >= 0) glUniform2f(pass.OriginalSize,
            static_cast<float>(input_size.Width), static_cast<float>(input_size.Height));
        if (pass.TexelSize >= 0) glUniform2f(pass.TexelSize,
            1.0f / std::max(1, current_size.Width),
            1.0f / std::max(1, current_size.Height));
        if (pass.Time >= 0) glUniform1f(pass.Time, time);
        if (pass.FrameCount >= 0) glUniform1f(pass.FrameCount,
            static_cast<float>(_frameCount));

        glEnableVertexAttribArray(pass.Position);
        glEnableVertexAttribArray(pass.TexCoord);
        glVertexAttribPointer(pass.Position, 2, GL_FLOAT, GL_FALSE,
            sizeof(QuadVertex), &kQuad[0].x);
        glVertexAttribPointer(pass.TexCoord, 2, GL_FLOAT, GL_FALSE,
            sizeof(QuadVertex), &kQuad[0].u);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        glDisableVertexAttribArray(pass.Position);
        glDisableVertexAttribArray(pass.TexCoord);

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
