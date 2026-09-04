#pragma once
#include <string>
#include <vector>
class ShaderPipelineV3 {
public:
    enum class ScaleType { Source, Viewport, Absolute };
    enum class WrapMode { ClampToBorder, ClampToEdge, Repeat, MirroredRepeat };
    static constexpr int MaxPrevPasses = 8;
    static constexpr int MaxFrameHistory = 8;
    ~ShaderPipelineV3();
    bool load(const std::string &path, std::string &error);
    bool loaded() const { return !_passes.empty(); }
    void clear();
    void apply(unsigned input_texture, int input_width, int input_height, int output_width, int output_height);
private:
    struct Parameter { std::string name; float value=0.f; };
    struct Pass {
        unsigned program=0;
        int texture=-1,input_size=-1,texture_size=-1,output_size=-1,original_size=-1,texel_size=-1,frame_count=-1,frame_direction=-1,time=-1,mvp_matrix=-1;
        int prev_texture[MaxPrevPasses];
        int prev_texture_size[MaxPrevPasses];
        int feedback_texture=-1,feedback_texture_size=-1;
        int original_history_texture[MaxFrameHistory];
        int original_history_texture_size[MaxFrameHistory];
        bool filter_linear=true;
        bool float_framebuffer=false;
        bool srgb_framebuffer=false;
        WrapMode wrap_mode=WrapMode::ClampToEdge;
        ScaleType scale_type_x=ScaleType::Source,scale_type_y=ScaleType::Source;
        float scale_x=1.f,scale_y=1.f;
        std::string source_path;
        std::string alias;
        Pass() {
            for (int i=0;i<MaxPrevPasses;++i) { prev_texture[i]=-1; prev_texture_size[i]=-1; }
            for (int i=0;i<MaxFrameHistory;++i) { original_history_texture[i]=-1; original_history_texture_size[i]=-1; }
        }
    };
    struct Target { unsigned fbo=0,texture=0; int width=0,height=0,format=0; };
    std::vector<Pass> _passes;
    std::vector<Target> _targets;
    std::vector<Target> _feedback;
    std::vector<Target> _original_history;
    std::vector<Parameter> _parameters;
    unsigned long long _frame_count=0;
    bool load_text(const std::string&,std::string&,std::string&) const;
    bool add_pass(const std::string&,const Pass*,std::string&);
    bool create_program(const std::string&,const std::string&,unsigned&,std::string&) const;
    bool compile_shader(unsigned,const std::string&,unsigned&,std::string&) const;
    bool parse_chain(const std::string&,std::vector<std::string>&,std::string&) const;
    bool parse_glslp(const std::string&,std::vector<Pass>&,std::string&);
    bool ensure_fbo_functions(std::string&);
    bool ensure_target(Target&,int,int,bool,bool,std::string&);
    bool copy_texture_to_target(unsigned,int,int,Target&,std::string&);
    void destroy_target(Target&);
    void set_parameter(const std::string&,float,bool);
    static bool parse_bool(const std::string &s, bool d) {
        if (s == "1" || s == "true" || s == "TRUE") return true;
        if (s == "0" || s == "false" || s == "FALSE") return false;
        return d;
    }
    static int parse_int(const std::string &s, int d) {
        try { size_t p = 0; int v = std::stoi(s, &p); return p == s.size() ? v : d; }
        catch (...) { return d; }
    }
    static float parse_float(const std::string &s, float d) {
        try { size_t p = 0; float v = std::stof(s, &p); return p == s.size() ? v : d; }
        catch (...) { return d; }
    }
};
