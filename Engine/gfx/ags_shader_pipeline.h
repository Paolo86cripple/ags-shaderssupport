#pragma once

#include <string>
#include <vector>

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

    bool Load(const std::string &path, std::string &error);
    bool IsLoaded() const { return !_passes.empty(); }
    void Clear();
    void Apply(int input_width, int input_height, int output_width, int output_height);

private:
    struct Pass
    {
        unsigned program = 0;
        int texture = -1;
        int texture_size = -1;
        int input_size = -1;
        int output_size = -1;
        int original_size = -1;
        int frame_count = -1;
        int frame_direction = -1;
        bool filter_linear = false;
        float scale_x = 1.0f;
        float scale_y = 1.0f;
        int scale_type = 0;
        std::string source_path;
    };

    struct Target
    {
        unsigned fbo = 0;
        unsigned texture = 0;
        int width = 0;
        int height = 0;
    };

    std::vector<Pass> _passes;
    Target _targets[2];
    unsigned _capture_texture = 0;
    int _capture_width = 0;
    int _capture_height = 0;
    unsigned long long _frame_count = 0;

    typedef void (*GenFboProc)(int, unsigned *);
    typedef void (*DeleteFboProc)(int, const unsigned *);
    typedef void (*BindFboProc)(unsigned, unsigned);
    typedef void (*AttachTextureProc)(unsigned, unsigned, unsigned, unsigned, int);
    typedef unsigned (*CheckFboProc)(unsigned);

    GenFboProc _gen_fbo = nullptr;
    DeleteFboProc _delete_fbo = nullptr;
    BindFboProc _bind_fbo = nullptr;
    AttachTextureProc _attach_texture = nullptr;
    CheckFboProc _check_fbo = nullptr;

    bool LoadFboFunctions(std::string &error);
    bool EnsureCaptureTexture(int width, int height, std::string &error);
    bool EnsureTarget(Target &target, int width, int height, bool linear, std::string &error);
    void DestroyTarget(Target &target);
    bool LoadShaderFile(const std::string &path, std::string &source, std::string &error) const;
    bool AddPass(const std::string &path, const Pass *preset, std::string &error);
    bool CreateProgram(const std::string &vertex, const std::string &fragment, unsigned &program, std::string &error) const;
    bool CompileShader(unsigned type, const std::string &source, unsigned &shader, std::string &error) const;
    bool ParsePreset(const std::string &path, std::vector<Pass> &out, std::string &error) const;
    bool ParseChain(const std::string &path, std::vector<std::string> &out, std::string &error) const;

    static std::string Trim(const std::string &value);
    static std::string Unquote(const std::string &value);
    static std::string ParentDir(const std::string &path);
    static std::string JoinPath(const std::string &dir, const std::string &path);
    static std::string Lower(std::string value);
    static bool HasSuffix(const std::string &path, const char *suffix);
    static bool ParseBool(const std::string &value, bool fallback);
    static int ParseInt(const std::string &value, int fallback);
    static float ParseFloat(const std::string &value, float fallback);
    static int ParseScaleType(const std::string &value);
    static int ResolveDimension(int type, float scale, int source, int viewport);
};

} // namespace OGL
} // namespace Engine
} // namespace AGS
