#pragma once

#include <string>

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
    bool IsLoaded() const;
    void Clear();
    void Apply(int input_width, int input_height, int output_width, int output_height);

private:
    AGSShaderPipeline(const AGSShaderPipeline &) = delete;
    AGSShaderPipeline &operator=(const AGSShaderPipeline &) = delete;

    struct Impl;
    Impl *_impl;
};

} // namespace OGL
} // namespace Engine
} // namespace AGS
