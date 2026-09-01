#pragma once

#include <string>
#include <vector>

class ShaderPipeline
{
public:
    ~ShaderPipeline();

    bool load(const std::string &path, std::string &error);
    void clear();
    bool loaded() const { return !_passes.empty(); }

    void apply(unsigned input_texture, int input_width, int input_height,
        int output_width, int output_height);

private:
    enum class ScaleType
    {
        Source,
        Viewport,
        Absolute
    };

    struct Pass
    {
        unsigned program = 0;
        int texture = -1;
        int input_size = -1;
        int output_size = -1;
        int original_size = -1;
        int texel_size = -1;
        int time = -1;
        int frame_count = -1;
        bool filter_linear = true;
        ScaleType scale_type_x = ScaleType::Source;
        ScaleType scale_type_y = ScaleType::Source;
        float scale_x = 1.0f;
        float scale_y = 1.0f;
        int absolute_x = 0;
        int absolute_y = 0;
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
    unsigned long long _frame_count = 0;

    bool load_text(const std::string &path, std::string &text, std::string &error) const;
    bool compile_shader(unsigned type, const std::string &source,
        unsigned &shader, std::string &error) const;
    bool create_program(const std::string &fragment_source,
        unsigned &program, std::string &error) const;
    bool add_pass(const std::string &path, const Pass *preset,
        std::string &error);
    bool parse_chain(const std::string &path,
        std::vector<std::string> &paths, std::string &error) const;
    bool parse_glslp(const std::string &path,
        std::vector<Pass> &passes, std::string &error) const;
    bool ensure_target(Target &target, int width, int height,
        bool linear, std::string &error);
    void destroy_target(Target &target);

    static bool has_suffix(const std::string &value, const std::string &suffix);
};
