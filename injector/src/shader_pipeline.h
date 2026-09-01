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
    };

    struct Target
    {
        unsigned fbo = 0;
        unsigned texture = 0;
    };

    std::vector<Pass> _passes;
    Target _targets[2];
    int _target_width = 0;
    int _target_height = 0;
    unsigned long long _frame_count = 0;

    bool load_text(const std::string &path, std::string &text, std::string &error) const;
    bool compile_shader(unsigned type, const std::string &source,
        unsigned &shader, std::string &error) const;
    bool create_program(const std::string &fragment_source,
        unsigned &program, std::string &error) const;
    bool add_pass(const std::string &path, std::string &error);
    bool parse_chain(const std::string &path,
        std::vector<std::string> &paths, std::string &error) const;
    bool ensure_targets(int width, int height, std::string &error);
};
