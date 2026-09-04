#pragma once

#include <GL/gl.h>
#include <png.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <sys/stat.h>
#include <sys/types.h>

// gl_vertex_compat.h is force-included immediately before this file. Replace
// only its public glDrawArrays macro, then delegate to the compatibility wrapper
// so auxiliary RetroArch TexCoord attributes keep working exactly as before.
#ifdef glDrawArrays
#undef glDrawArrays
#endif

namespace ags_shader_gl_diag {

inline bool directory_exists(const std::string &path) {
    struct stat info;
    return stat(path.c_str(), &info) == 0 && S_ISDIR(info.st_mode);
}

inline bool ensure_directory(const std::string &path) {
    if (path.empty()) return false;
    if (directory_exists(path)) return true;

    std::string current;
    std::size_t start = 0;
    if (path[0] == '/') {
        current = "/";
        start = 1;
    }

    while (start <= path.size()) {
        const std::size_t slash = path.find('/', start);
        const std::string part = path.substr(start,
                                             slash == std::string::npos
                                                 ? std::string::npos
                                                 : slash - start);
        if (!part.empty()) {
            if (!current.empty() && current.back() != '/') current += '/';
            current += part;
            if (!directory_exists(current) &&
                mkdir(current.c_str(), 0755) != 0 && errno != EEXIST)
                return false;
        }
        if (slash == std::string::npos) break;
        start = slash + 1;
    }
    return directory_exists(path);
}

inline bool write_png(const std::string &path,
                      const unsigned char *pixels,
                      int width,
                      int height) {
    FILE *file = std::fopen(path.c_str(), "wb");
    if (!file) return false;

    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    if (!png) {
        std::fclose(file);
        return false;
    }
    png_infop info = png_create_info_struct(png);
    if (!info) {
        png_destroy_write_struct(&png, nullptr);
        std::fclose(file);
        return false;
    }
    if (setjmp(png_jmpbuf(png))) {
        png_destroy_write_struct(&png, &info);
        std::fclose(file);
        return false;
    }

    png_init_io(png, file);
    png_set_IHDR(png,
                 info,
                 static_cast<png_uint_32>(width),
                 static_cast<png_uint_32>(height),
                 8,
                 PNG_COLOR_TYPE_RGBA,
                 PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_DEFAULT,
                 PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png, info);

    const std::size_t row_bytes = static_cast<std::size_t>(width) * 4u;
    std::vector<png_bytep> rows(static_cast<std::size_t>(height));
    for (int y = 0; y < height; ++y) {
        rows[static_cast<std::size_t>(y)] = const_cast<png_bytep>(
            pixels + static_cast<std::size_t>(height - 1 - y) * row_bytes);
    }

    png_write_image(png, rows.data());
    png_write_end(png, nullptr);
    png_destroy_write_struct(&png, &info);
    std::fclose(file);
    return true;
}

inline int requested_pass_count() {
    const char *value = std::getenv("AGS_SHADER_DUMP_PASS_COUNT");
    if (!value || !value[0]) return 64;
    char *end = nullptr;
    const long parsed = std::strtol(value, &end, 10);
    if (end == value || *end != '\0' || parsed <= 0 || parsed > 256) return 64;
    return static_cast<int>(parsed);
}

inline void dump_current_framebuffer(unsigned pass_index) {
    const char *directory_env = std::getenv("AGS_SHADER_DUMP_PASSES_DIR");
    if (!directory_env || !directory_env[0]) return;

    const std::string directory(directory_env);
    if (!ensure_directory(directory)) {
        std::fprintf(stderr,
                     "AGS shader dump: cannot create directory '%s'\n",
                     directory.c_str());
        return;
    }

    GLint viewport[4] = {0, 0, 0, 0};
    GLint framebuffer = 0;
    GLint old_read_buffer = GL_BACK;
    GLint old_pack_alignment = 4;
    glGetIntegerv(GL_VIEWPORT, viewport);
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &framebuffer);
    glGetIntegerv(GL_READ_BUFFER, &old_read_buffer);
    glGetIntegerv(GL_PACK_ALIGNMENT, &old_pack_alignment);

    const int width = viewport[2];
    const int height = viewport[3];
    if (width <= 0 || height <= 0) return;

    glReadBuffer(framebuffer == 0 ? GL_BACK : GL_COLOR_ATTACHMENT0);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);

    std::vector<unsigned char> pixels(static_cast<std::size_t>(width) *
                                      static_cast<std::size_t>(height) * 4u);
    while (glGetError() != GL_NO_ERROR) {}
    glReadPixels(viewport[0],
                 viewport[1],
                 width,
                 height,
                 GL_RGBA,
                 GL_UNSIGNED_BYTE,
                 pixels.data());
    const GLenum read_error = glGetError();

    glPixelStorei(GL_PACK_ALIGNMENT, old_pack_alignment);
    glReadBuffer(static_cast<GLenum>(old_read_buffer));

    if (read_error != GL_NO_ERROR) {
        std::fprintf(stderr,
                     "AGS shader dump: pass %u glReadPixels failed (0x%x)\n",
                     pass_index,
                     read_error);
        return;
    }

    char filename[128];
    std::snprintf(filename,
                  sizeof(filename),
                  "pass-%02u-%dx%d.png",
                  pass_index,
                  width,
                  height);
    const std::string path = directory + "/" + filename;
    if (!write_png(path, pixels.data(), width, height)) {
        std::fprintf(stderr,
                     "AGS shader dump: failed to write '%s'\n",
                     path.c_str());
        return;
    }

    std::fprintf(stderr,
                 "AGS shader dump: pass %02u framebuffer=%d %dx%d -> %s\n",
                 pass_index,
                 framebuffer,
                 width,
                 height,
                 path.c_str());
}

inline void draw_arrays(GLenum mode, GLint first, GLsizei count) {
    ags_shader_gl_compat::draw_arrays(mode, first, count);

    static bool dump_complete = false;
    static unsigned pass_index = 0;
    if (dump_complete) return;

    const char *directory = std::getenv("AGS_SHADER_DUMP_PASSES_DIR");
    if (!directory || !directory[0]) return;

    dump_current_framebuffer(pass_index);

    GLint framebuffer = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &framebuffer);
    ++pass_index;

    // The final pass normally renders back to AGS' default framebuffer (0).
    // AGS_SHADER_DUMP_PASS_COUNT provides a deterministic fallback for unusual
    // hosts that keep a non-zero framebuffer bound around presentation.
    if (framebuffer == 0 || pass_index >= static_cast<unsigned>(requested_pass_count())) {
        dump_complete = true;
        std::fprintf(stderr,
                     "AGS shader dump: completed after %u pass(es)\n",
                     pass_index);
    }
}

} // namespace ags_shader_gl_diag

#define glDrawArrays ags_shader_gl_diag::draw_arrays
