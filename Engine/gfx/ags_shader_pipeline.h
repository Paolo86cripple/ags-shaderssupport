#ifndef __AGS_EE_GFX__AGS_SHADER_PIPELINE_H
#define __AGS_EE_GFX__AGS_SHADER_PIPELINE_H

#include <vector>
#include <string>

#include "gfx/ogl_headers.h"
#include "util/geometry.h"
#include "util/string.h"

namespace AGS
{
namespace Engine
{
namespace OGL
{

class AGSShaderPipeline
{
public:
    AGSShaderPipeline();
    ~AGSShaderPipeline();

    bool Load(const String &path, String &error);
    void Clear();
    bool IsLoaded() const { return !_passes.empty(); }

    void Apply(GLuint input_texture,
        const Size &input_size,
        GLuint output_fbo,
        const Size &output_size,
        const Rect &output_viewport);

private:
    struct Pass
    {
        GLuint Program = 0u;
        GLint Position = -1;
        GLint TexCoord = -1;
        GLint Texture = -1;
        GLint InputSize = -1;
        GLint OutputSize = -1;
        GLint OriginalSize = -1;
        GLint TexelSize = -1;
        GLint Time = -1;
        GLint FrameCount = -1;
        String Name;
    };

    struct RenderTarget
    {
        GLuint Fbo = 0u;
        GLuint Texture = 0u;
        Size SizePx;
    };

    bool LoadTextFile(const String &path, std::string &text, String &error) const;
    bool CreatePass(const String &path, Pass &pass, String &error);
    bool CreateProgram(const char *fragment_source, const String &name,
        GLuint &program, String &error);
    bool CompileShader(GLenum type, const char *source, GLuint &shader,
        const String &name, String &error);
    bool ParseChain(const String &chain_path,
        std::vector<String> &shader_paths, String &error) const;
    bool EnsureRenderTargets(int width, int height, size_t count, String &error);

    void DestroyPass(Pass &pass);
    void DestroyRenderTarget(RenderTarget &target);

    static bool HasSuffixNoCase(const String &value, const char *suffix);

    std::vector<Pass> _passes;
    std::vector<RenderTarget> _targets;
    Size _targetSize;
    uint64_t _frameCount = 0u;
};

} // namespace OGL
} // namespace Engine
} // namespace AGS

#endif
