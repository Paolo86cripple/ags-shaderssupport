#pragma once

#include "shader_pipeline_v4.h"

#include <string>

class ShaderPipeline {
public:
    bool load(const std::string &path, std::string &error);
    bool loaded() const { return _impl.loaded(); }
    void clear() { _impl.clear(); }
    void apply(unsigned input_texture,
               int input_width,
               int input_height,
               int output_width,
               int output_height) {
        _impl.apply(input_texture,
                    input_width,
                    input_height,
                    output_width,
                    output_height);
    }

private:
    ShaderPipelineV4 _impl;
};
