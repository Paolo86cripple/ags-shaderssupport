#include "lut_manager.h"

#include <cstddef>
#include <cstdio>

#include <GL/gl.h>
#include <GL/glext.h>
#include <SDL2/SDL.h>
#include <jpeglib.h>
#include <png.h>

#include <cctype>
#include <fstream>
#include <setjmp.h>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {
using GenerateMipmap = void (*)(GLenum);
GenerateMipmap g_generate_mipmap = nullptr;
bool g_mipmap_resolved = false;

struct Lut {
    std::string name;
    std::string path;
    unsigned texture = 0;
    int width = 0;
    int height = 0;
    bool linear = true;
    bool mipmap = false;
    GLenum wrap = GL_CLAMP_TO_BORDER;
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
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"')
        s = s.substr(1, s.size() - 2);
    return s;
}

std::string lower(std::string s) {
    for (char &c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
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
    if (s == "clamp_to_edge") return GL_CLAMP_TO_EDGE;
    if (s == "repeat") return GL_REPEAT;
    if (s == "mirrored_repeat") return GL_MIRRORED_REPEAT;
    return GL_CLAMP_TO_BORDER;
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

bool suffix(const std::string &path, const char *extension) {
    const std::string p = lower(path);
    const std::string e = lower(extension);
    return p.size() >= e.size() && p.compare(p.size() - e.size(), e.size(), e) == 0;
}

void resolve_mipmap() {
    if (g_mipmap_resolved) return;
    g_mipmap_resolved = true;
    g_generate_mipmap = reinterpret_cast<GenerateMipmap>(SDL_GL_GetProcAddress("glGenerateMipmap"));
    if (!g_generate_mipmap)
        g_generate_mipmap = reinterpret_cast<GenerateMipmap>(SDL_GL_GetProcAddress("glGenerateMipmapEXT"));
}

GLint min_filter(const Lut &lut) {
    if (!lut.mipmap) return lut.linear ? GL_LINEAR : GL_NEAREST;
    return lut.linear ? GL_LINEAR_MIPMAP_LINEAR : GL_NEAREST_MIPMAP_NEAREST;
}

struct UploadState {
    GLint active_texture = GL_TEXTURE0;
    GLint unit0_binding = 0;
    GLint unpack_alignment = 4;

    UploadState() {
        glGetIntegerv(GL_ACTIVE_TEXTURE, &active_texture);
        glActiveTexture(GL_TEXTURE0);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &unit0_binding);
        glGetIntegerv(GL_UNPACK_ALIGNMENT, &unpack_alignment);
    }

    ~UploadState() {
        glPixelStorei(GL_UNPACK_ALIGNMENT, unpack_alignment);
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(unit0_binding));
        glActiveTexture(static_cast<GLenum>(active_texture));
    }
};

bool upload_rgba(Lut &lut,
                 const std::vector<unsigned char> &pixels,
                 std::string &error) {
    if (lut.width <= 0 || lut.height <= 0 ||
        pixels.size() != static_cast<size_t>(lut.width) * static_cast<size_t>(lut.height) * 4u) {
        error = "invalid decoded LUT image: " + lut.path;
        return false;
    }

    UploadState state;
    glGenTextures(1, &lut.texture);
    glBindTexture(GL_TEXTURE_2D, lut.texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, min_filter(lut));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, lut.linear ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, lut.wrap);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, lut.wrap);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D,
                 0,
                 GL_RGBA,
                 lut.width,
                 lut.height,
                 0,
                 GL_RGBA,
                 GL_UNSIGNED_BYTE,
                 pixels.data());

    if (lut.mipmap) {
        resolve_mipmap();
        if (!g_generate_mipmap) {
            error = "OpenGL mipmap generation is unavailable for shader texture: " + lut.path;
            glDeleteTextures(1, &lut.texture);
            lut.texture = 0;
            return false;
        }
        g_generate_mipmap(GL_TEXTURE_2D);
    }

    if (glGetError() != GL_NO_ERROR) {
        error = "OpenGL failed to upload shader texture: " + lut.path;
        glDeleteTextures(1, &lut.texture);
        lut.texture = 0;
        return false;
    }
    return true;
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
    return upload_rgba(lut, pixels, error);
}

struct JpegErrorManager {
    jpeg_error_mgr base;
    jmp_buf jump;
    char message[JMSG_LENGTH_MAX];
};

void jpeg_error_exit(j_common_ptr common) {
    JpegErrorManager *manager = reinterpret_cast<JpegErrorManager *>(common->err);
    (*common->err->format_message)(common, manager->message);
    longjmp(manager->jump, 1);
}

bool load_jpeg(Lut &lut, std::string &error) {
    FILE *file = std::fopen(lut.path.c_str(), "rb");
    if (!file) {
        error = "cannot read LUT JPEG: " + lut.path;
        return false;
    }

    jpeg_decompress_struct info = {};
    JpegErrorManager manager;
    manager.message[0] = '\0';
    info.err = jpeg_std_error(&manager.base);
    manager.base.error_exit = jpeg_error_exit;

    if (setjmp(manager.jump)) {
        if (info.mem) jpeg_destroy_decompress(&info);
        std::fclose(file);
        error = "cannot decode LUT JPEG '" + lut.path + "': " + manager.message;
        return false;
    }

    jpeg_create_decompress(&info);
    jpeg_stdio_src(&info, file);
    jpeg_read_header(&info, TRUE);
    info.out_color_space = JCS_RGB;
    jpeg_start_decompress(&info);

    lut.width = static_cast<int>(info.output_width);
    lut.height = static_cast<int>(info.output_height);
    const size_t width = static_cast<size_t>(lut.width);
    const size_t height = static_cast<size_t>(lut.height);
    std::vector<unsigned char> pixels(width * height * 4u);
    std::vector<unsigned char> row(width * 3u);

    while (info.output_scanline < info.output_height) {
        JSAMPROW row_pointer = row.data();
        jpeg_read_scanlines(&info, &row_pointer, 1);
        const size_t y = static_cast<size_t>(info.output_scanline - 1u);
        unsigned char *destination = pixels.data() + y * width * 4u;
        for (size_t x = 0; x < width; ++x) {
            destination[x * 4u + 0u] = row[x * 3u + 0u];
            destination[x * 4u + 1u] = row[x * 3u + 1u];
            destination[x * 4u + 2u] = row[x * 3u + 2u];
            destination[x * 4u + 3u] = 255u;
        }
    }

    jpeg_finish_decompress(&info);
    jpeg_destroy_decompress(&info);
    std::fclose(file);
    return upload_rgba(lut, pixels, error);
}

bool load_image(Lut &lut, std::string &error) {
    if (suffix(lut.path, ".png")) return load_png(lut, error);
    if (suffix(lut.path, ".jpg") || suffix(lut.path, ".jpeg"))
        return load_jpeg(lut, error);
    error = "unsupported shader texture format: " + lut.path;
    return false;
}
}

void ags_lut_clear() {
    UploadState state;
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

    std::vector<std::pair<std::string, std::string>> entries;
    std::vector<std::string> names;
    std::string line;
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
        for (const auto &entry : entries) {
            if (entry.first == name) lut.path = join_path(dir, entry.second);
            else if (entry.first == name + "_linear") lut.linear = parse_bool(entry.second, true);
            else if (entry.first == name + "_mipmap") lut.mipmap = parse_bool(entry.second, false);
            else if (entry.first == name + "_wrap_mode") lut.wrap = parse_wrap(entry.second);
        }

        if (lut.path.empty()) {
            error = "missing shader texture path for '" + name + "' in " + preset_path;
            ags_lut_clear();
            return false;
        }
        if (!load_image(lut, error)) {
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
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, min_filter(lut));
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, lut.linear ? GL_LINEAR : GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, lut.wrap);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, lut.wrap);
        if (sampler >= 0) glUniform1i(sampler, unit);
        if (size_loc >= 0)
            glUniform2f(size_loc, static_cast<float>(lut.width), static_cast<float>(lut.height));
        ++unit;
    }
    return unit;
}
