#pragma once

#include <cstddef>
#include <string>
#include <vector>

class ShaderPipelineV4 {
public:
    enum class ScaleType { Source, Viewport, Absolute };
    enum class WrapMode { ClampToBorder, ClampToEdge, Repeat, MirroredRepeat };

    static constexpr int MaxPassRefs = 16;
    static constexpr int MaxFrameHistory = 8;

    struct FrameUniform {
        int texture = -1;
        int tex_coord = -1;
        int input_size = -1;
        int texture_size = -1;

        bool used() const {
            return texture >= 0 || tex_coord >= 0 || input_size >= 0 || texture_size >= 0;
        }
    };

    struct Target {
        unsigned fbo = 0;
        unsigned texture = 0;
        int width = 0;
        int height = 0;
        int input_width = 0;
        int input_height = 0;
        int format = 0;
    };

    ~ShaderPipelineV4();

    bool load(const std::string &path, std::string &error);
    bool loaded() const { return !_passes.empty(); }
    void clear();
    void apply(unsigned input_texture,
               int input_width,
               int input_height,
               int output_width,
               int output_height);

private:
    struct AliasUniform {
        std::size_t pass_index = 0;
        FrameUniform frame;
    };

    struct Parameter {
        std::string name;
        float value = 0.f;
    };

    struct Pass {
        unsigned program = 0;

        int texture = -1;
        int input_size = -1;
        int texture_size = -1;
        int output_size = -1;
        int original_size = -1;
        int final_viewport_size = -1;
        int texel_size = -1;
        int frame_count = -1;
        int frame_direction = -1;
        int frame_time_delta = -1;
        int original_fps = -1;
        int rotation = -1;
        int original_aspect = -1;
        int original_aspect_rotated = -1;
        int gyroscope = -1;
        int accelerometer = -1;
        int accelerometer_rest = -1;
        int time = -1;
        int mvp_matrix = -1;
        int lut_tex_coord = -1;

        FrameUniform orig;
        FrameUniform feedback;
        FrameUniform pass_ref[MaxPassRefs];
        FrameUniform pass_prev[MaxPassRefs];
        FrameUniform prev[MaxFrameHistory];
        FrameUniform original_history[MaxFrameHistory];
        std::vector<AliasUniform> aliases;

        bool filter_linear = true;
        bool mipmap_input = false;
        bool float_framebuffer = false;
        bool srgb_framebuffer = false;
        WrapMode wrap_mode = WrapMode::ClampToEdge;
        ScaleType scale_type_x = ScaleType::Source;
        ScaleType scale_type_y = ScaleType::Source;
        float scale_x = 1.f;
        float scale_y = 1.f;
        unsigned frame_count_mod = 0;
        std::string source_path;
        std::string alias;
    };

    std::vector<Pass> _passes;
    std::vector<Target> _targets;
    std::vector<Target> _prev_history;
    std::vector<Parameter> _parameters;
    Target _feedback;
    int _feedback_pass = -1;
    bool _uses_prev_history = false;
    bool _uses_feedback = false;
    std::string _alias_defines;
    unsigned long long _frame_count = 0;
    unsigned long long _last_frame_ticks = 0;

    bool load_text(const std::string&, std::string&, std::string&) const;
    bool add_pass(const std::string&, const Pass*, std::string&);
    bool create_program(const std::string&, const std::string&, unsigned&, std::string&) const;
    bool compile_shader(unsigned, const std::string&, unsigned&, std::string&) const;
    bool parse_chain(const std::string&, std::vector<std::string>&, std::string&) const;
    bool parse_glslp(const std::string&, std::vector<Pass>&, std::string&);
    bool ensure_fbo_functions(std::string&);
    bool ensure_target(Target&, int, int, int, std::string&);
    bool ensure_target(Target&, int, int, bool, bool, std::string&);
    bool copy_texture_to_target(unsigned, int, int, int, Target&, std::string&);
    bool copy_target_to_target(const Target&, Target&, std::string&);
    void destroy_target(Target&);
    void set_parameter(const std::string&, float, bool);

    static bool parse_bool(const std::string &s, bool d);
    static int parse_int(const std::string &s, int d);
    static unsigned parse_uint_prefix(const std::string &s, unsigned d);
    static float parse_float(const std::string &s, float d);
};
