#include "shader_pipeline.h"

#include <GL/gl.h>
#include <GL/glext.h>
#include <SDL2/SDL.h>
#include <dlfcn.h>
#include <png.h>

#include <cerrno>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <string>
#include <vector>

#include <sys/stat.h>
#include <sys/types.h>

namespace {
using SwapWindowFn = void (*)(SDL_Window *);
using PollEventFn = int (*)(SDL_Event *);
using WaitEventFn = int (*)(SDL_Event *);
using WaitEventTimeoutFn = int (*)(SDL_Event *, int);

SwapWindowFn g_real_swap = nullptr;
PollEventFn g_real_poll_event = nullptr;
WaitEventFn g_real_wait_event = nullptr;
WaitEventTimeoutFn g_real_wait_event_timeout = nullptr;
thread_local bool g_in_swap = false;
ShaderPipeline g_pipeline;
GLuint g_capture_texture = 0u;
int g_capture_width = 0;
int g_capture_height = 0;
bool g_initialized = false;
bool g_f12_down = false;
bool g_screenshot_pending = false;
unsigned long g_screenshot_sequence = 0;

void DebugLog(const char *format, ...) {
    if (!std::getenv("AGS_SHADER_DEBUG")) return;
    va_list args;
    va_start(args, format);
    std::vfprintf(stderr, format, args);
    va_end(args);
    std::fputc('\n', stderr);
}

void ResolveRealSwap() {
    if (!g_real_swap)
        g_real_swap = reinterpret_cast<SwapWindowFn>(dlsym(RTLD_NEXT, "SDL_GL_SwapWindow"));
}

void ResolveRealEventFunctions() {
    if (!g_real_poll_event)
        g_real_poll_event = reinterpret_cast<PollEventFn>(dlsym(RTLD_NEXT, "SDL_PollEvent"));
    if (!g_real_wait_event)
        g_real_wait_event = reinterpret_cast<WaitEventFn>(dlsym(RTLD_NEXT, "SDL_WaitEvent"));
    if (!g_real_wait_event_timeout)
        g_real_wait_event_timeout = reinterpret_cast<WaitEventTimeoutFn>(
            dlsym(RTLD_NEXT, "SDL_WaitEventTimeout"));
}

void ObserveEvent(const SDL_Event *event) {
    if (!event) return;
    if (event->type == SDL_KEYDOWN &&
        event->key.keysym.scancode == SDL_SCANCODE_F12 &&
        event->key.repeat == 0) {
        g_screenshot_pending = true;
        DebugLog("AGS shader: F12 screenshot requested from SDL event");
    }
}

bool EnsureCaptureTexture(int width, int height) {
    if (g_capture_texture && g_capture_width == width && g_capture_height == height)
        return true;

    if (g_capture_texture) glDeleteTextures(1, &g_capture_texture);
    g_capture_texture = 0;

    glGenTextures(1, &g_capture_texture);
    glBindTexture(GL_TEXTURE_2D, g_capture_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D,
                 0,
                 GL_RGBA,
                 width,
                 height,
                 0,
                 GL_RGBA,
                 GL_UNSIGNED_BYTE,
                 nullptr);

    if (glGetError() != GL_NO_ERROR) {
        if (g_capture_texture) glDeleteTextures(1, &g_capture_texture);
        g_capture_texture = 0;
        g_capture_width = 0;
        g_capture_height = 0;
        return false;
    }

    g_capture_width = width;
    g_capture_height = height;
    return true;
}

void InitializePipeline() {
    if (g_initialized) return;
    g_initialized = true;

    const char *shader_path = std::getenv("AGS_SHADER_CHAIN");
    if (!shader_path || !shader_path[0]) shader_path = std::getenv("AGS_SHADER");
    if (!shader_path || !shader_path[0]) {
        DebugLog("AGS shader: no shader selected");
        return;
    }

    std::string error;
    if (!g_pipeline.load(shader_path, error)) {
        DebugLog("AGS shader: failed to load '%s': %s", shader_path, error.c_str());
        return;
    }
    DebugLog("AGS shader: loaded '%s'", shader_path);
}

bool CaptureBackBuffer(int width, int height) {
    GLint old_active = GL_TEXTURE0;
    GLint old_texture0 = 0;
    GLint old_read = GL_BACK;

    glGetIntegerv(GL_ACTIVE_TEXTURE, &old_active);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &old_texture0);
    glGetIntegerv(GL_READ_BUFFER, &old_read);

    glReadBuffer(GL_BACK);
    const bool ready = EnsureCaptureTexture(width, height);
    if (ready) {
        glBindTexture(GL_TEXTURE_2D, g_capture_texture);
        glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, width, height);
    }

    const GLenum copy_error = ready ? glGetError() : GL_NO_ERROR;
    glReadBuffer(static_cast<GLenum>(old_read));
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(old_texture0));
    glActiveTexture(static_cast<GLenum>(old_active));

    if (!ready || copy_error != GL_NO_ERROR) {
        DebugLog("AGS shader: failed to capture back buffer (GL error 0x%x)", copy_error);
        return false;
    }
    return true;
}

std::string HomeDirectory() {
    const char *home = std::getenv("HOME");
    return home && home[0] ? std::string(home) : std::string(".");
}

std::string ExpandHome(std::string value, const std::string &home) {
    const std::string quoted_home = "$HOME";
    const std::string braced_home = "${HOME}";
    std::size_t pos = 0;
    while ((pos = value.find(quoted_home, pos)) != std::string::npos) {
        value.replace(pos, quoted_home.size(), home);
        pos += home.size();
    }
    pos = 0;
    while ((pos = value.find(braced_home, pos)) != std::string::npos) {
        value.replace(pos, braced_home.size(), home);
        pos += home.size();
    }
    return value;
}

std::string XdgPicturesDirectory() {
    const char *override_dir = std::getenv("AGS_SHADER_SCREENSHOT_DIR");
    if (override_dir && override_dir[0]) return override_dir;

    const std::string home = HomeDirectory();
    const char *xdg_config = std::getenv("XDG_CONFIG_HOME");
    const std::string config_home = xdg_config && xdg_config[0]
                                      ? std::string(xdg_config)
                                      : home + "/.config";
    std::ifstream input((config_home + "/user-dirs.dirs").c_str());
    std::string line;
    const std::string key = "XDG_PICTURES_DIR=";
    while (std::getline(input, line)) {
        if (line.compare(0, key.size(), key) != 0) continue;
        std::string value = line.substr(key.size());
        if (value.size() >= 2 && value.front() == '"' && value.back() == '"')
            value = value.substr(1, value.size() - 2);
        value = ExpandHome(value, home);
        if (!value.empty()) return value;
    }

    return home + "/Pictures";
}

bool DirectoryExists(const std::string &path) {
    struct stat info;
    return stat(path.c_str(), &info) == 0 && S_ISDIR(info.st_mode);
}

bool EnsureDirectory(const std::string &path) {
    if (path.empty()) return false;
    if (DirectoryExists(path)) return true;

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
            if (!DirectoryExists(current) &&
                mkdir(current.c_str(), 0755) != 0 && errno != EEXIST) {
                return false;
            }
        }
        if (slash == std::string::npos) break;
        start = slash + 1;
    }
    return DirectoryExists(path);
}

std::string ScreenshotFilename(const std::string &directory) {
    using namespace std::chrono;
    const system_clock::time_point now = system_clock::now();
    const std::time_t timestamp = system_clock::to_time_t(now);
    std::tm local_tm;
    std::memset(&local_tm, 0, sizeof(local_tm));
#if defined(_POSIX_VERSION)
    localtime_r(&timestamp, &local_tm);
#else
    const std::tm *tmp = std::localtime(&timestamp);
    if (tmp) local_tm = *tmp;
#endif
    const long millis = static_cast<long>(
        duration_cast<milliseconds>(now.time_since_epoch()).count() % 1000);

    char name[160];
    std::snprintf(name,
                  sizeof(name),
                  "ags-shader-%04d%02d%02d-%02d%02d%02d-%03ld-%03lu.png",
                  local_tm.tm_year + 1900,
                  local_tm.tm_mon + 1,
                  local_tm.tm_mday,
                  local_tm.tm_hour,
                  local_tm.tm_min,
                  local_tm.tm_sec,
                  millis,
                  ++g_screenshot_sequence);
    return directory + "/" + name;
}

bool WritePng(const std::string &path,
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

bool ReadFinalBackBuffer(int width,
                         int height,
                         std::vector<unsigned char> &pixels) {
    GLint old_read = GL_BACK;
    GLint old_pack_alignment = 4;
#ifdef GL_PIXEL_PACK_BUFFER_BINDING
    GLint old_pack_buffer = 0;
#endif

    glGetIntegerv(GL_READ_BUFFER, &old_read);
    glGetIntegerv(GL_PACK_ALIGNMENT, &old_pack_alignment);
#ifdef GL_PIXEL_PACK_BUFFER_BINDING
    glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &old_pack_buffer);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
#endif

    glReadBuffer(GL_BACK);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    pixels.resize(static_cast<std::size_t>(width) *
                  static_cast<std::size_t>(height) * 4u);

    while (glGetError() != GL_NO_ERROR) {}
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    const GLenum read_error = glGetError();

    glPixelStorei(GL_PACK_ALIGNMENT, old_pack_alignment);
    glReadBuffer(static_cast<GLenum>(old_read));
#ifdef GL_PIXEL_PACK_BUFFER_BINDING
    glBindBuffer(GL_PIXEL_PACK_BUFFER, static_cast<GLuint>(old_pack_buffer));
#endif

    if (read_error != GL_NO_ERROR) {
        DebugLog("AGS shader: screenshot glReadPixels failed (GL error 0x%x)", read_error);
        pixels.clear();
        return false;
    }
    return true;
}

bool ScreenshotRequested(SDL_Window *window) {
    if (g_screenshot_pending) {
        g_screenshot_pending = false;
        return true;
    }

    const Uint8 *keys = SDL_GetKeyboardState(nullptr);
    const bool focused = !window || SDL_GetKeyboardFocus() == window;
    const bool pressed = focused && keys && keys[SDL_SCANCODE_F12] != 0;
    const bool trigger = pressed && !g_f12_down;
    g_f12_down = pressed;
    return trigger;
}

void SaveScreenshot(int width, int height) {
    std::vector<unsigned char> pixels;
    if (!ReadFinalBackBuffer(width, height, pixels)) return;

    const std::string directory = XdgPicturesDirectory();
    if (!EnsureDirectory(directory)) {
        DebugLog("AGS shader: cannot create screenshot directory '%s'", directory.c_str());
        return;
    }

    const std::string path = ScreenshotFilename(directory);
    if (!WritePng(path, pixels.data(), width, height)) {
        DebugLog("AGS shader: failed to write screenshot '%s'", path.c_str());
        return;
    }

    std::fprintf(stderr, "AGS shader screenshot: %s\n", path.c_str());
}
}

extern "C" int SDL_PollEvent(SDL_Event *event) {
    ResolveRealEventFunctions();
    if (!g_real_poll_event) return 0;
    const int result = g_real_poll_event(event);
    if (result > 0) ObserveEvent(event);
    return result;
}

extern "C" int SDL_WaitEvent(SDL_Event *event) {
    ResolveRealEventFunctions();
    if (!g_real_wait_event) return 0;
    const int result = g_real_wait_event(event);
    if (result > 0) ObserveEvent(event);
    return result;
}

extern "C" int SDL_WaitEventTimeout(SDL_Event *event, int timeout) {
    ResolveRealEventFunctions();
    if (!g_real_wait_event_timeout) return 0;
    const int result = g_real_wait_event_timeout(event, timeout);
    if (result > 0) ObserveEvent(event);
    return result;
}

extern "C" void SDL_GL_SwapWindow(SDL_Window *window) {
    ResolveRealSwap();
    if (!g_real_swap) return;
    if (g_in_swap) {
        g_real_swap(window);
        return;
    }

    g_in_swap = true;
    InitializePipeline();

    int width = 0;
    int height = 0;
    SDL_GL_GetDrawableSize(window, &width, &height);

    if (g_pipeline.loaded() && width > 0 && height > 0 && CaptureBackBuffer(width, height))
        g_pipeline.apply(g_capture_texture, width, height, width, height);

    if (width > 0 && height > 0 && ScreenshotRequested(window))
        SaveScreenshot(width, height);

    g_real_swap(window);
    g_in_swap = false;
}
