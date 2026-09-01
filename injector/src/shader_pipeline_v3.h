#pragma once
#include <string>
#include <vector>
class ShaderPipelineV3 {
public:
    enum class ScaleType { Source, Viewport, Absolute };
    ~ShaderPipelineV3();
    bool load(const std::string &path, std::string &error);
    bool loaded() const { return !_passes.empty(); }
    void clear();
    void apply(unsigned input_texture, int input_width, int input_height, int output_width, int output_height);
private:
    struct Pass { unsigned program=0; int texture=-1,input_size=-1,texture_size=-1,output_size=-1,original_size=-1,texel_size=-1,frame_count=-1,frame_direction=-1,time=-1; bool filter_linear=true; ScaleType scale_type_x=ScaleType::Source,scale_type_y=ScaleType::Source; float scale_x=1.f,scale_y=1.f; std::string source_path; };
    struct Target { unsigned fbo=0,texture=0; int width=0,height=0; };
    std::vector<Pass> _passes; Target _targets[2]; unsigned long long _frame_count=0;
    bool load_text(const std::string&,std::string&,std::string&) const;
    bool add_pass(const std::string&,const Pass*,std::string&);
    bool create_program(const std::string&,const std::string&,unsigned&,std::string&) const;
    bool compile_shader(unsigned,const std::string&,unsigned&,std::string&) const;
    bool parse_chain(const std::string&,std::vector<std::string>&,std::string&) const;
    bool parse_glslp(const std::string&,std::vector<Pass>&,std::string&) const;
    bool ensure_fbo_functions(std::string&);
    bool ensure_target(Target&,int,int,bool,std::string&);
    void destroy_target(Target&);
    static std::string trim(std::string); static std::string lower(std::string); static std::string unquote(std::string); static std::string parent_dir(const std::string&); static std::string join_path(const std::string&,const std::string&); static bool has_suffix(const std::string&,const char*); static bool parse_bool(const std::string&,bool); static int parse_int(const std::string&,int); static float parse_float(const std::string&,float); static ScaleType parse_scale_type(const std::string&); static int resolve_dimension(ScaleType,float,int,int);
};
