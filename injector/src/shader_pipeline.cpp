#include "shader_pipeline.h"
#include "preset_parser.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include <unistd.h>

namespace {
bool has_glslp_suffix(const std::string &path) {
    static const std::string suffix = ".glslp";
    if (path.size() < suffix.size()) return false;
    const std::string tail = path.substr(path.size() - suffix.size());
    for (std::size_t i = 0; i < suffix.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(tail[i])) != suffix[i])
            return false;
    }
    return true;
}
}

bool ShaderPipeline::load(const std::string &path, std::string &error) {
    if (!has_glslp_suffix(path)) return _impl.load(path, error);

    std::string pattern = "/tmp/ags-shader-preset-XXXXXX.glslp";
    std::vector<char> writable(pattern.begin(), pattern.end());
    writable.push_back('\0');

    const int fd = ::mkstemps(writable.data(), 6);
    if (fd < 0) {
        error = "cannot create temporary flattened GLSLP preset";
        return false;
    }
    ::close(fd);

    const std::string flattened(writable.data());
    bool ok = ags_preset_write_flat(path, flattened, error);
    if (ok) ok = _impl.load(flattened, error);
    std::remove(flattened.c_str());
    return ok;
}
