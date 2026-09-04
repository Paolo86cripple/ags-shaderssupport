#pragma once

#include "shader_pipeline_v4.h"

#include <string>

class ShaderPipeline {
public:
    ~ShaderPipeline();

    bool load(const std::string &path, std::string &error);
    bool loaded() const { return _impl.loaded(); }
    void clear();
    void apply(unsigned input_texture,
               int input_width,
               int input_height,
               int output_width,
               int output_height);

private:
    ShaderPipelineV4 _impl;
    unsigned _source_texture = 0;
    unsigned _source_read_fbo = 0;
    unsigned _source_draw_fbo = 0;
    int _source_width = 0;
    int _source_height = 0;
    bool _gpu_info_logged = false;

    void release_source_resampler();
    bool prepare_source_texture(unsigned input_texture,
                                int input_width,
                                int input_height,
                                unsigned &source_texture,
                                int &source_width,
                                int &source_height);
};
