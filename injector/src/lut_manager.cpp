#include "lut_manager.h"

#include <GL/gl.h>
#include <png.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {
struct Lut {
    std::string name;
    std::string path;
    unsigned texture = 0;
    int width = 0;
    int height = 0;
    bool linear = true;
    GLenum wrap = GL_CLAMP_TO_EDGE;
};

std::vector<Lut> g_luts;

std::string trim(const std::string &s) {
    const size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return {};
    const size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

std::string unquote(std::string s) {
    s = trim(s);
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"') s = s.substr(1, s.size() - 2);
    return s;
}

std::string lower(std::string s) {
    for (char &c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string parent_dir(const std::string &p) {
    const size_t n = p.find_last_of("/\\");
    return n == std::string::npos ? "." : p.substr(0, n);
}

std::string join_path(const std::string &d, const std::string &p) {
    if (p.empty() || p.front() == '/' || (p.size() > 1 && p[1] == ':')) return p;
    return d + "/" + p;
}

bool parse_bool(const std::string &v, bool d) {
    const std::string s = lower(unquote(v));
    if (s == "1" || s == "true") return true;
    if (s == "0" || s == "false") return false;
    return d;
}

GLenum parse_wrap(const std::string &v) {
    const std::string s = lower(unquote(v));
    if (s == "repeat") return GL_REPEAT;
    if (s == "mirrored_repeat") return GL_MIRRORED_REPEAT;
    if (s == "clamp_to_border") return GL_CLAMP_TO_BORDER;
    return GL_CLAMP_TO_EDGE;
}

std::vector<std::string> split_semicolon(const std::string &s) {
    std::vector<std::string> out;
    std::istringstream in(s);
    std::string item;
    while (std::getline(in, item, ';')) {
        item = trim(item);
        if (!item.empty()) out.push_back(item);
    }
    return out;
}

bool load_png(Lut &lut, std::string &error) {
    png_image image;
    image.version = PNG_IMAGE_VERSION;
    image.opaque = nullptr;
    if (!png_image_begin_read_from_file(&image, lut.path.c_str())) {
        error = "cannot read LUT PNG '" + lut.path + "': " + image.message;
        return false;
    }
    image.format = PNG_FORMAT_RGBA;
    std::vector<unsigned char> pixels(PNG_IMAGE_SIZE(image));
    if (!png_image_finish_read(&image, nullptr, pixels.data(), 0, nullptr)) {
        error = "cannot decode LUT PNG '" + lut.path + "': " + image.message;
        png_image_free(&image);
        return false;
    }
    lut.width = static_cast<int>(image.width);
    lut.height = static_cast<int>(image.height);
    png_image_free(&image);

    glGenTextures(1, &lut.texture);
    glBindTexture(GL_TEXTURE_2D, lut.texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, lut.linear ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, lut.linear ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, lut.wrap);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, lut.wrap);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, lut.width, lut.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    if (glGetError() != GL_NO_ERROR) {
        error = "OpenGL failed to upload LUT PNG: " + lut.path;
        glDeleteTextures(1, &lut.texture);
        lut.texture = 0;
        return false;
    }
    return true;
}
}

void ags_lut_clear() {
    for (Lut &lut : g_luts) {
        if (lut.texture) glDeleteTextures(1, &lut.texture);
    }
    g_luts.clear();
}

bool ags_lut_load_preset(const std::string &preset_path, std::string &error) {
    ags_lut_clear();
    std::ifstream file(preset_path.c_str(), std::ios::binary);
    if (!file) {
        error = "cannot read shader preset: " + preset_path;
        return false;
    }

    std::vector<std::pair<std::string,std::string>> entries;
    std::string line;
    std::vector<std::string> names;
    while (std::getline(file, line)) {
        line = trim(line);
        if (line.empty() || line.front() == '#') continue;
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = trim(line.substr(0, eq));
        const std::string value = unquote(line.substr(eq + 1));
        entries.push_back(std::make_pair(key, value));
        if (key == "textures") names = split_semicolon(value);
    }

    if (names.empty()) return true;
    const std::string dir = parent_dir(preset_path);
    for (const std::string &name : names) {
        Lut lut;
        lut.name = name;
        for (const auto &kv : entries) {
            if (kv.first == name) lut.path = join_path(dir, kv.second);
            else if (kv.first == name + "_linear") lut.linear = parse_bool(kv.second, true);
            else if (kv.first == name + "_wrap_mode") lut.wrap = parse_wrap(kv.second);
        }
        if (lut.path.empty()) {
            error = "missing LUT path for '" + name + "' in " + preset_path;
            ags_lut_clear();
            return false;
        }
        if (!load_png(lut, error)) {
            ags_lut_clear();
            return false;
        }
        g_luts.push_back(lut);
    }
    return true;
}

int ags_lut_bind(unsigned program, int first_unit, int max_units) {
    int unit = first_unit;
    for (const Lut &lut : g_luts) {
        if (unit >= max_units) break;
        GLint sampler = glGetUniformLocation(program, lut.name.c_str());
        if (sampler < 0) sampler = glGetUniformLocation(program, (lut.name + "Texture").c_str());
        GLint size_loc = glGetUniformLocation(program, (lut.name + "Size").c_str());
        if (size_loc < 0) size_loc = glGetUniformLocation(program, (lut.name + "TextureSize").c_str());
        if (sampler < 0 && size_loc < 0) continue;

        glActiveTexture(GL_TEXTURE0 + unit);
        glBindTexture(GL_TEXTURE_2D, lut.texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, lut.linear ? GL_LINEAR : GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, lut.linear ? GL_LINEAR : GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, lut.wrap);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, lut.wrap);
        if (sampler >= 0) glUniform1i(sampler, unit);
        if (size_loc >= 0) glUniform2f(size_loc, static_cast<float>(lut.width), static_cast<float>(lut.height));
        ++unit;
    }
    return unit;
}
