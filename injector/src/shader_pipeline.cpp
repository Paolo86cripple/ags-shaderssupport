#include "shader_pipeline.h"

#include <GL/gl.h>
#include <SDL2/SDL.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace
{
const char *kVertexShader =
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
    float x;
    float y;
    float u;
    float v;
};

const QuadVertex kQuad[] =
{
    { -1.0f, -1.0f, 0.0f, 0.0f },
    {  1.0f, -1.0f, 1.0f, 0.0f },
    { -1.0f,  1.0f, 0.0f, 1.0f },
    {  1.0f,  1.0f, 1.0f, 1.0f }
};

std::string Trim(std::string value)
{
    const size_t first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos)
        return std::string();
    const size_t last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::string Lower(std::string value)
{
    for (char &ch : value)
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return value;
}

bool EndsWith(const std::string &value, const std::string &suffix)
{
    if (value.size() < suffix.size())
        return false;
    return Lower(value.substr(value.size() - suffix.size())) == Lower(suffix);
}

std::string DirectoryOf(const std::string &path)
{
    const size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? "." : path.substr(0, slash);
}

std::string JoinPath(const std::string &directory, const std::string &path)
{
    if (path.empty() || path[0] == '/')
        return path;
    return directory + "/" + path;
}
}

ShaderPipeline::~ShaderPipeline()
{
    clear();
}

void ShaderPipeline::clear()
{
    for (Pass &pass : _passes)
    {
        if (pass.program != 0u)
            glDeleteProgram(pass.program);
    }
    _passes.clear();

    for (Target &target : _targets)
    {
        if (target.fbo != 0u)
            glDeleteFramebuffers(1, &target.fbo);
        if (target.texture != 0u)
            glDeleteTextures(1, &target.texture);
        target = Target();
    }

    _target_width = 0;
    _target_height = 0;
    _frame_count = 0;
}

bool ShaderPipeline::load_text(const std::string &path, std::string &text,
    std::string &error) const
{
    std::ifstream file(path.c_str(), std::ios::binary);
    if (!file)
    {
        error = "cannot read shader file: " + path;
        return false;
    }

    std::ostringstream stream;
    stream << file.rdbuf();
    text = stream.str();
    if (text.empty())
    {
        error = "shader file is empty: " + path;
        return false;
    }
    return true;
}

bool ShaderPipeline::compile_shader(unsigned type, const std::string &source,
    unsigned &shader, std::string &error) const
{
    shader = glCreateShader(static_cast<GLenum>(type));
    if (shader == 0u)
    {
        error = "glCreateShader failed";
        return false;
    }

    const char *source_ptr = source.c_str();
    glShaderSource(shader, 1, &source_ptr, nullptr);
    glCompileShader(shader);

    GLint status = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (status == GL_TRUE)
        return true;

    GLint log_length = 0;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &log_length);
    std::vector<char> log(static_cast<size_t>(std::max(log_length, 1)));
    if (log_length > 0)
        glGetShaderInfoLog(shader, log_length, nullptr, log.data());

    error = log.data();
    glDeleteShader(shader);
    shader = 0u;
    return false;
}

bool ShaderPipeline::create_program(const std::string &fragment_source,
    unsigned &program, std::string &error) const
{
    unsigned vertex_shader = 0u;
    unsigned fragment_shader = 0u;

    if (!compile_shader(GL_VERTEX_SHADER, kVertexShader, vertex_shader, error))
        return false;
    if (!compile_shader(GL_FRAGMENT_SHADER, fragment_source, fragment_shader, error))
    {
        glDeleteShader(vertex_shader);
        return false;
    }

    program = glCreateProgram();
    if (program == 0u)
    {
        glDeleteShader(vertex_shader);
        glDeleteShader(fragment_shader);
        error = "glCreateProgram failed";
        return false;
    }

    glAttachShader(program, vertex_shader);
    glAttachShader(program, fragment_shader);
    glBindAttribLocation(program, 0, "aPosition");
    glBindAttribLocation(program, 1, "aTexCoord");
    glLinkProgram(program);

    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);

    GLint status = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &status);
    if (status == GL_TRUE)
        return true;

    GLint log_length = 0;
    glGetProgramiv(program, GL_INFO_LOG_LENGTH, &log_length);
    std::vector<char> log(static_cast<size_t>(std::max(log_length, 1)));
    if (log_length > 0)
        glGetProgramInfoLog(program, log_length, nullptr, log.data());

    error = log.data();
    glDeleteProgram(program);
    program = 0u;
    return false;
}

bool ShaderPipeline::add_pass(const std::string &path, std::string &error)
{
    std::string source;
    if (!load_text(path, source, error))
        return false;

    Pass pass;
    if (!create_program(source, pass.program, error))
    {
        error = "shader '" + path + "': " + error;
        return false;
    }

    pass.texture = glGetUniformLocation(pass.program, "uTexture");
    pass.input_size = glGetUniformLocation(pass.program, "uInputSize");
    pass.output_size = glGetUniformLocation(pass.program, "uOutputSize");
    pass.original_size = glGetUniformLocation(pass.program, "uOriginalSize");
    pass.texel_size = glGetUniformLocation(pass.program, "uTexelSize");
    pass.time = glGetUniformLocation(pass.program, "uTime");
    pass.frame_count = glGetUniformLocation(pass.program, "uFrameCount");
    _passes.push_back(pass);
    return true;
}

bool ShaderPipeline::parse_chain(const std::string &path,
    std::vector<std::string> &paths, std::string &error)
{
    std::string source;
    if (!load_text(path, source, error))
        return false;

    const std::string directory = DirectoryOf(path);
    std::istringstream stream(source);
    std::string line;

    while (std::getline(stream, line))
    {
        line = Trim(line);
        if (line.empty() || line[0] == '#')
            continue;
        if (line.compare(0, 5, "pass=") == 0)
        {
            const std::string shader = Trim(line.substr(5));
            if (!shader.empty())
                paths.push_back(JoinPath(directory, shader));
        }
    }

    if (paths.empty())
    {
        error = "shader chain contains no pass= entries: " + path;
        return false;
    }
    return true;
}

bool ShaderPipeline::load(const std::string &path, std::string &error)
{
    clear();

    std::vector<std::string> paths;
    if (EndsWith(path, ".agschain"))
    {
        if (!parse_chain(path, paths, error))
            return false;
    }
    else
    {
        paths.push_back(path);
    }

    for (const std::string &shader_path : paths)
    {
        if (!add_pass(shader_path, error))
        {
            clear();
            return false;
        }
    }
    return true;
}

bool ShaderPipeline::ensure_targets(int width, int height, std::string &error)
{
    if (_target_width == width && _target_height == height &&
        _targets[0].fbo != 0u && _targets[1].fbo != 0u)
        return true;

    for (Target &target : _targets)
    {
        if (target.fbo != 0u)
            glDeleteFramebuffers(1, &target.fbo);
        if (target.texture != 0u)
            glDeleteTextures(1, &target.texture);
        target = Target();
    }

    for (Target &target : _targets)
    {
        glGenTextures(1, &target.texture);
        glBindTexture(GL_TEXTURE_2D, target.texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0,
            GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

        glGenFramebuffers(1, &target.fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, target.fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
            GL_TEXTURE_2D, target.texture, 0);

        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        {
            error = "shader framebuffer is incomplete";
            return false;
        }
    }

    glBindTexture(GL_TEXTURE_2D, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    _target_width = width;
    _target_height = height;
    return true;
}

void ShaderPipeline::apply(unsigned input_texture, int input_width, int input_height,
    int output_width, int output_height)
{
    if (_passes.empty() || input_texture == 0u)
        return;

    std::string error;
    if (!ensure_targets(output_width, output_height, error))
        return;

    const float time = static_cast<float>(SDL_GetTicks()) / 1000.0f;
    unsigned current_texture = input_texture;
    int current_width = input_width;
    int current_height = input_height;

    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);

    for (size_t i = 0; i < _passes.size(); ++i)
    {
        const bool last = i + 1 == _passes.size();
        const unsigned target_fbo = last ? 0u : _targets[i & 1u].fbo;
        const Pass &pass = _passes[i];

        glBindFramebuffer(GL_FRAMEBUFFER, target_fbo);
        glViewport(0, 0, output_width, output_height);
        glUseProgram(pass.program);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, current_texture);
        if (pass.texture >= 0)
            glUniform1i(pass.texture, 0);
        if (pass.input_size >= 0)
            glUniform2f(pass.input_size,
                static_cast<float>(current_width), static_cast<float>(current_height));
        if (pass.output_size >= 0)
            glUniform2f(pass.output_size,
                static_cast<float>(output_width), static_cast<float>(output_height));
        if (pass.original_size >= 0)
            glUniform2f(pass.original_size,
                static_cast<float>(input_width), static_cast<float>(input_height));
        if (pass.texel_size >= 0)
            glUniform2f(pass.texel_size,
                1.0f / static_cast<float>(std::max(current_width, 1)),
                1.0f / static_cast<float>(std::max(current_height, 1)));
        if (pass.time >= 0)
            glUniform1f(pass.time, time);
        if (pass.frame_count >= 0)
            glUniform1f(pass.frame_count, static_cast<float>(_frame_count));

        glEnableVertexAttribArray(0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE,
            sizeof(QuadVertex), &kQuad[0].x);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE,
            sizeof(QuadVertex), &kQuad[0].u);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        glDisableVertexAttribArray(0);
        glDisableVertexAttribArray(1);

        if (!last)
        {
            current_texture = _targets[i & 1u].texture;
            current_width = output_width;
            current_height = output_height;
        }
    }

    glUseProgram(0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    ++_frame_count;
}
