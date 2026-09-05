#pragma once

/*
 * AGS Shader Injector pass diagnostics.
 * Copyright (C) 2026 Paolo86cripple and contributors.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <GL/gl.h>
#include <GL/glext.h>
#include <SDL2/SDL.h>
#include <png.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include <sys/stat.h>
#include <sys/types.h>

#ifndef GL_TIME_ELAPSED
#define GL_TIME_ELAPSED 0x88BF
#endif
#ifndef GL_QUERY_RESULT
#define GL_QUERY_RESULT 0x8866
#endif
#ifndef GL_QUERY_RESULT_AVAILABLE
#define GL_QUERY_RESULT_AVAILABLE 0x8867
#endif

/* scummvm_gl_exec_opt.h is force-included immediately before this file. */
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
    ::glGetIntegerv(GL_VIEWPORT, viewport);
    ::glGetIntegerv(GL_FRAMEBUFFER_BINDING, &framebuffer);
    ::glGetIntegerv(GL_READ_BUFFER, &old_read_buffer);
    ::glGetIntegerv(GL_PACK_ALIGNMENT, &old_pack_alignment);

    const int width = viewport[2];
    const int height = viewport[3];
    if (width <= 0 || height <= 0) return;

    ::glReadBuffer(framebuffer == 0 ? GL_BACK : GL_COLOR_ATTACHMENT0);
    ::glPixelStorei(GL_PACK_ALIGNMENT, 1);

    std::vector<unsigned char> pixels(static_cast<std::size_t>(width) *
                                      static_cast<std::size_t>(height) * 4u);
    while (::glGetError() != GL_NO_ERROR) {}
    ::glReadPixels(viewport[0],
                   viewport[1],
                   width,
                   height,
                   GL_RGBA,
                   GL_UNSIGNED_BYTE,
                   pixels.data());
    const GLenum read_error = ::glGetError();

    ::glPixelStorei(GL_PACK_ALIGNMENT, old_pack_alignment);
    ::glReadBuffer(static_cast<GLenum>(old_read_buffer));

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

using GenQueriesFn = void (*)(GLsizei, GLuint *);
using DeleteQueriesFn = void (*)(GLsizei, const GLuint *);
using BeginQueryFn = void (*)(GLenum, GLuint);
using EndQueryFn = void (*)(GLenum);
using GetQueryObjectivFn = void (*)(GLuint, GLenum, GLint *);
using GetQueryObjectui64vFn = void (*)(GLuint, GLenum, GLuint64 *);

struct ProfilePass {
    GLuint query = 0;
    unsigned index = 0;
    int width = 0;
    int height = 0;
};

struct ProfileFrame {
    std::vector<ProfilePass> passes;
};

struct ProfileAccumulator {
    std::vector<unsigned long long> total_ns;
    std::vector<int> width;
    std::vector<int> height;
    unsigned samples = 0;
    Uint32 last_report_ms = 0;
};

inline GenQueriesFn &profile_gen_queries() {
    static GenQueriesFn fn = nullptr;
    return fn;
}

inline DeleteQueriesFn &profile_delete_queries() {
    static DeleteQueriesFn fn = nullptr;
    return fn;
}

inline BeginQueryFn &profile_begin_query() {
    static BeginQueryFn fn = nullptr;
    return fn;
}

inline EndQueryFn &profile_end_query() {
    static EndQueryFn fn = nullptr;
    return fn;
}

inline GetQueryObjectivFn &profile_get_query_object_iv() {
    static GetQueryObjectivFn fn = nullptr;
    return fn;
}

inline GetQueryObjectui64vFn &profile_get_query_object_ui64v() {
    static GetQueryObjectui64vFn fn = nullptr;
    return fn;
}

inline bool profile_requested() {
    static const bool enabled = []() {
        const char *value = std::getenv("AGS_SHADER_PROFILE_PASSES");
        return value && value[0] && std::strcmp(value, "0") != 0;
    }();
    return enabled;
}

inline void *profile_proc(const char *core, const char *fallback = nullptr) {
    void *proc = SDL_GL_GetProcAddress(core);
    if (!proc && fallback) proc = SDL_GL_GetProcAddress(fallback);
    return proc;
}

inline bool resolve_profile_queries() {
    static int state = 0;
    if (state != 0) return state > 0;

    profile_gen_queries() = reinterpret_cast<GenQueriesFn>(
        profile_proc("glGenQueries", "glGenQueriesARB"));
    profile_delete_queries() = reinterpret_cast<DeleteQueriesFn>(
        profile_proc("glDeleteQueries", "glDeleteQueriesARB"));
    profile_begin_query() = reinterpret_cast<BeginQueryFn>(
        profile_proc("glBeginQuery", "glBeginQueryARB"));
    profile_end_query() = reinterpret_cast<EndQueryFn>(
        profile_proc("glEndQuery", "glEndQueryARB"));
    profile_get_query_object_iv() = reinterpret_cast<GetQueryObjectivFn>(
        profile_proc("glGetQueryObjectiv", "glGetQueryObjectivARB"));
    profile_get_query_object_ui64v() = reinterpret_cast<GetQueryObjectui64vFn>(
        profile_proc("glGetQueryObjectui64v", "glGetQueryObjectui64vEXT"));

    const bool ok = profile_gen_queries() && profile_delete_queries() &&
                    profile_begin_query() && profile_end_query() &&
                    profile_get_query_object_iv() && profile_get_query_object_ui64v();
    state = ok ? 1 : -1;
    if (!ok) {
        std::fprintf(stderr,
                     "AGS shader GPU profile: timer queries unavailable; profiling disabled\n");
    }
    else {
        std::fprintf(stderr,
                     "AGS shader GPU profile: asynchronous GL_TIME_ELAPSED profiling enabled\n");
    }
    return ok;
}

inline std::vector<ProfileFrame> &profile_pending_frames() {
    static std::vector<ProfileFrame> frames;
    return frames;
}

inline ProfileFrame &profile_active_frame() {
    static ProfileFrame frame;
    return frame;
}

inline ProfileAccumulator &profile_accumulator() {
    static ProfileAccumulator accumulator;
    return accumulator;
}

inline void profile_report_if_due() {
    ProfileAccumulator &acc = profile_accumulator();
    if (acc.samples == 0 || acc.total_ns.empty()) return;

    const Uint32 now = SDL_GetTicks();
    if (acc.last_report_ms == 0) {
        acc.last_report_ms = now;
        return;
    }
    if (acc.samples < 8 || now - acc.last_report_ms < 2000) return;

    unsigned long long frame_total_ns = 0;
    for (unsigned long long value : acc.total_ns)
        frame_total_ns += value / acc.samples;

    const double frame_ms = static_cast<double>(frame_total_ns) / 1000000.0;
    const double gpu_fps = frame_ms > 0.0 ? 1000.0 / frame_ms : 0.0;
    std::fprintf(stderr,
                 "AGS shader GPU profile: %u frame(s), pass draw total %.3f ms, GPU-only ceiling %.2f FPS\n",
                 acc.samples,
                 frame_ms,
                 gpu_fps);

    for (std::size_t i = 0; i < acc.total_ns.size(); ++i) {
        const unsigned long long average_ns = acc.total_ns[i] / acc.samples;
        const double ms = static_cast<double>(average_ns) / 1000000.0;
        const double share = frame_total_ns > 0
            ? 100.0 * static_cast<double>(average_ns) / static_cast<double>(frame_total_ns)
            : 0.0;
        std::fprintf(stderr,
                     "AGS shader GPU profile: pass %02zu %dx%d %.3f ms %5.1f%%\n",
                     i,
                     i < acc.width.size() ? acc.width[i] : 0,
                     i < acc.height.size() ? acc.height[i] : 0,
                     ms,
                     share);
    }

    for (unsigned long long &value : acc.total_ns) value = 0;
    acc.samples = 0;
    acc.last_report_ms = now;
}

inline void profile_consume_frame(ProfileFrame &frame) {
    if (frame.passes.empty()) return;

    ProfileAccumulator &acc = profile_accumulator();
    const std::size_t count = frame.passes.size();
    if (acc.total_ns.size() != count) {
        acc.total_ns.assign(count, 0);
        acc.width.assign(count, 0);
        acc.height.assign(count, 0);
        acc.samples = 0;
        acc.last_report_ms = SDL_GetTicks();
    }

    for (std::size_t i = 0; i < count; ++i) {
        ProfilePass &pass = frame.passes[i];
        GLuint64 elapsed = 0;
        profile_get_query_object_ui64v()(pass.query, GL_QUERY_RESULT, &elapsed);
        acc.total_ns[i] += static_cast<unsigned long long>(elapsed);
        acc.width[i] = pass.width;
        acc.height[i] = pass.height;
        profile_delete_queries()(1, &pass.query);
        pass.query = 0;
    }
    ++acc.samples;
    profile_report_if_due();
}

inline void profile_poll_results() {
    std::vector<ProfileFrame> &pending = profile_pending_frames();
    for (std::size_t i = 0; i < pending.size();) {
        ProfileFrame &frame = pending[i];
        if (frame.passes.empty()) {
            pending.erase(pending.begin() + static_cast<std::ptrdiff_t>(i));
            continue;
        }

        GLint available = GL_FALSE;
        profile_get_query_object_iv()(frame.passes.back().query,
                                      GL_QUERY_RESULT_AVAILABLE,
                                      &available);
        if (!available) {
            ++i;
            continue;
        }

        profile_consume_frame(frame);
        pending.erase(pending.begin() + static_cast<std::ptrdiff_t>(i));
    }
}

inline void draw_arrays(GLenum mode, GLint first, GLsizei count) {
    static bool dump_complete = false;
    static unsigned dump_pass_index = 0;

    static unsigned profile_pass_index = 0;
    static bool profile_this_frame = false;
    constexpr std::size_t MaxProfileFramesInFlight = 8;

    const bool want_profile = profile_requested();
    bool profile_pass = false;
    ProfilePass timed_pass;

    if (want_profile && resolve_profile_queries()) {
        if (profile_pass_index == 0) {
            profile_poll_results();
            profile_this_frame =
                profile_pending_frames().size() < MaxProfileFramesInFlight;
            if (!profile_this_frame && profile_active_frame().passes.empty()) {
                static bool backlog_warned = false;
                if (!backlog_warned) {
                    backlog_warned = true;
                    std::fprintf(stderr,
                                 "AGS shader GPU profile: query backlog full; sampling frames opportunistically\n");
                }
            }
        }

        if (profile_this_frame) {
            GLint viewport[4] = {0, 0, 0, 0};
            ::glGetIntegerv(GL_VIEWPORT, viewport);
            timed_pass.index = profile_pass_index;
            timed_pass.width = viewport[2];
            timed_pass.height = viewport[3];
            profile_gen_queries()(1, &timed_pass.query);
            if (timed_pass.query) {
                profile_begin_query()(GL_TIME_ELAPSED, timed_pass.query);
                profile_pass = true;
            }
        }
    }

    ags_shader_scummvm_opt::draw_arrays(mode, first, count);

    if (profile_pass) {
        profile_end_query()(GL_TIME_ELAPSED);
        profile_active_frame().passes.push_back(timed_pass);
    }

    GLint framebuffer = 0;
    ::glGetIntegerv(GL_FRAMEBUFFER_BINDING, &framebuffer);

    if (want_profile && resolve_profile_queries()) {
        if (framebuffer == 0) {
            if (profile_this_frame && !profile_active_frame().passes.empty()) {
                profile_pending_frames().push_back(std::move(profile_active_frame()));
                profile_active_frame() = ProfileFrame();
            }
            profile_pass_index = 0;
            profile_this_frame = false;
        }
        else {
            ++profile_pass_index;
        }
    }

    if (dump_complete) return;

    const char *directory = std::getenv("AGS_SHADER_DUMP_PASSES_DIR");
    if (!directory || !directory[0]) return;

    dump_current_framebuffer(dump_pass_index);
    ++dump_pass_index;

    if (framebuffer == 0 || dump_pass_index >= static_cast<unsigned>(requested_pass_count())) {
        dump_complete = true;
        std::fprintf(stderr,
                     "AGS shader dump: completed after %u pass(es)\n",
                     dump_pass_index);
    }
}

} // namespace ags_shader_gl_diag

#define glDrawArrays ags_shader_gl_diag::draw_arrays
