#include "shader_pipeline.h"
#include "ags_native_source_hook.h"
#include "preset_parser.h"

#include <GL/gl.h>
#include <GL/glext.h>
#include <SDL2/SDL.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <unistd.h>

#ifndef GL_READ_FRAMEBUFFER
#define GL_READ_FRAMEBUFFER 0x8CA8
#endif
#ifndef GL_DRAW_FRAMEBUFFER
#define GL_DRAW_FRAMEBUFFER 0x8CA9
#endif
#ifndef GL_READ_FRAMEBUFFER_BINDING
#define GL_READ_FRAMEBUFFER_BINDING 0x8CAA
#endif
#ifndef GL_DRAW_FRAMEBUFFER_BINDING
#define GL_DRAW_FRAMEBUFFER_BINDING 0x8CA6
#endif

namespace {
using GenFramebuffersFn = void (*)(GLsizei, GLuint *);
using DeleteFramebuffersFn = void (*)(GLsizei, const GLuint *);
using BindFramebufferFn = void (*)(GLenum, GLuint);
using FramebufferTexture2DFn = void (*)(GLenum, GLenum, GLenum, GLuint, GLint);
using CheckFramebufferStatusFn = GLenum (*)(GLenum);
using BlitFramebufferFn = void (*)(GLint, GLint, GLint, GLint,
                                   GLint, GLint, GLint, GLint,
                                   GLbitfield, GLenum);

GenFramebuffersFn p_gen_framebuffers = nullptr;
DeleteFramebuffersFn p_delete_framebuffers = nullptr;
BindFramebufferFn p_bind_framebuffer = nullptr;
FramebufferTexture2DFn p_framebuffer_texture_2d = nullptr;
CheckFramebufferStatusFn p_check_framebuffer_status = nullptr;
BlitFramebufferFn p_blit_framebuffer = nullptr;

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

bool debug_enabled() {
    const char *value = std::getenv("AGS_SHADER_DEBUG");
    return value && value[0] && std::strcmp(value, "0") != 0;
}

void *gl_proc(const char *core_name, const char *ext_name = nullptr) {
    void *proc = SDL_GL_GetProcAddress(core_name);
    if (!proc && ext_name) proc = SDL_GL_GetProcAddress(ext_name);
    return proc;
}

bool resolve_blit_functions() {
    if (p_gen_framebuffers && p_delete_framebuffers && p_bind_framebuffer &&
        p_framebuffer_texture_2d && p_check_framebuffer_status && p_blit_framebuffer)
        return true;

    p_gen_framebuffers = reinterpret_cast<GenFramebuffersFn>(
        gl_proc("glGenFramebuffers", "glGenFramebuffersEXT"));
    p_delete_framebuffers = reinterpret_cast<DeleteFramebuffersFn>(
        gl_proc("glDeleteFramebuffers", "glDeleteFramebuffersEXT"));
    p_bind_framebuffer = reinterpret_cast<BindFramebufferFn>(
        gl_proc("glBindFramebuffer", "glBindFramebufferEXT"));
    p_framebuffer_texture_2d = reinterpret_cast<FramebufferTexture2DFn>(
        gl_proc("glFramebufferTexture2D", "glFramebufferTexture2DEXT"));
    p_check_framebuffer_status = reinterpret_cast<CheckFramebufferStatusFn>(
        gl_proc("glCheckFramebufferStatus", "glCheckFramebufferStatusEXT"));
    p_blit_framebuffer = reinterpret_cast<BlitFramebufferFn>(
        gl_proc("glBlitFramebuffer", "glBlitFramebufferEXT"));

    return p_gen_framebuffers && p_delete_framebuffers && p_bind_framebuffer &&
           p_framebuffer_texture_2d && p_check_framebuffer_status && p_blit_framebuffer;
}

bool parse_source_size(int &width, int &height) {
    width = 0;
    height = 0;
    const char *value = std::getenv("AGS_SHADER_SOURCE_SIZE");
    if (!value || !value[0]) return false;

    char *end = nullptr;
    const long parsed_width = std::strtol(value, &end, 10);
    if (end == value || (*end != 'x' && *end != 'X')) return false;

    const char *height_text = end + 1;
    char *height_end = nullptr;
    const long parsed_height = std::strtol(height_text, &height_end, 10);
    if (height_end == height_text || *height_end != '\0') return false;
    if (parsed_width <= 0 || parsed_height <= 0 ||
        parsed_width > 32768 || parsed_height > 32768)
        return false;

    width = static_cast<int>(parsed_width);
    height = static_cast<int>(parsed_height);
    return true;
}

GLenum source_filter() {
    const char *value = std::getenv("AGS_SHADER_SOURCE_FILTER");
    if (!value) return GL_NEAREST;

    std::string filter(value);
    std::transform(filter.begin(), filter.end(), filter.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return filter == "linear" ? GL_LINEAR : GL_NEAREST;
}

const char *gl_string(GLenum name) {
    const GLubyte *value = glGetString(name);
    return value ? reinterpret_cast<const char *>(value) : "(unavailable)";
}
}

ShaderPipeline::~ShaderPipeline() {
    release_source_resampler();
}

void ShaderPipeline::release_source_resampler() {
    if (SDL_GL_GetCurrentContext() && resolve_blit_functions()) {
        if (_source_read_fbo)
            p_delete_framebuffers(1, reinterpret_cast<const GLuint *>(&_source_read_fbo));
        if (_source_draw_fbo)
            p_delete_framebuffers(1, reinterpret_cast<const GLuint *>(&_source_draw_fbo));
        if (_source_texture)
            glDeleteTextures(1, reinterpret_cast<const GLuint *>(&_source_texture));
    }

    _source_texture = 0;
    _source_read_fbo = 0;
    _source_draw_fbo = 0;
    _source_width = 0;
    _source_height = 0;
}

void ShaderPipeline::clear() {
    _impl.clear();
    release_source_resampler();
}

bool ShaderPipeline::load(const std::string &path, std::string &error) {
    release_source_resampler();

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

bool ShaderPipeline::prepare_source_texture(unsigned input_texture,
                                            int input_width,
                                            int input_height,
                                            unsigned &source_texture,
                                            int &source_width,
                                            int &source_height,
                                            bool force_exact_copy) {
    source_texture = input_texture;
    source_width = input_width;
    source_height = input_height;

    int requested_width = 0;
    int requested_height = 0;
    const bool explicit_size = parse_source_size(requested_width, requested_height);
    if (!explicit_size) {
        if (!force_exact_copy) return true;
        requested_width = input_width;
        requested_height = input_height;
    }

    if (!force_exact_copy &&
        requested_width == input_width && requested_height == input_height)
        return true;

    if (!resolve_blit_functions()) {
        if (debug_enabled())
            std::fprintf(stderr,
                         "AGS shader: GPU source preparation unavailable (framebuffer blit functions missing)\n");
        return false;
    }

    GLint old_active_texture = GL_TEXTURE0;
    GLint old_texture = 0;
    GLint old_read_fbo = 0;
    GLint old_draw_fbo = 0;
    glGetIntegerv(GL_ACTIVE_TEXTURE, &old_active_texture);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &old_texture);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &old_read_fbo);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &old_draw_fbo);

    if (!_source_texture || _source_width != requested_width ||
        _source_height != requested_height) {
        release_source_resampler();

        _source_width = requested_width;
        _source_height = requested_height;
        glGenTextures(1, reinterpret_cast<GLuint *>(&_source_texture));
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(_source_texture));
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D,
                     0,
                     GL_RGBA,
                     requested_width,
                     requested_height,
                     0,
                     GL_RGBA,
                     GL_UNSIGNED_BYTE,
                     nullptr);

        p_gen_framebuffers(1, reinterpret_cast<GLuint *>(&_source_read_fbo));
        p_gen_framebuffers(1, reinterpret_cast<GLuint *>(&_source_draw_fbo));
    }

    while (glGetError() != GL_NO_ERROR) {}

    p_bind_framebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(_source_read_fbo));
    p_framebuffer_texture_2d(GL_READ_FRAMEBUFFER,
                             GL_COLOR_ATTACHMENT0,
                             GL_TEXTURE_2D,
                             static_cast<GLuint>(input_texture),
                             0);
    const bool read_complete =
        p_check_framebuffer_status(GL_READ_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;

    p_bind_framebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(_source_draw_fbo));
    p_framebuffer_texture_2d(GL_DRAW_FRAMEBUFFER,
                             GL_COLOR_ATTACHMENT0,
                             GL_TEXTURE_2D,
                             static_cast<GLuint>(_source_texture),
                             0);
    const bool draw_complete =
        p_check_framebuffer_status(GL_DRAW_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;

    bool ok = read_complete && draw_complete;
    if (ok) {
        p_blit_framebuffer(0,
                           0,
                           input_width,
                           input_height,
                           0,
                           0,
                           requested_width,
                           requested_height,
                           GL_COLOR_BUFFER_BIT,
                           source_filter());
        ok = glGetError() == GL_NO_ERROR;
    }

    p_bind_framebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(old_read_fbo));
    p_bind_framebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(old_draw_fbo));
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(old_texture));
    glActiveTexture(static_cast<GLenum>(old_active_texture));

    if (!ok) {
        if (debug_enabled())
            std::fprintf(stderr,
                         "AGS shader: GPU source preparation failed; using original %dx%d source\n",
                         input_width,
                         input_height);
        return false;
    }

    source_texture = _source_texture;
    source_width = requested_width;
    source_height = requested_height;
    return true;
}

void ShaderPipeline::apply(unsigned input_texture,
                           int input_width,
                           int input_height,
                           int output_width,
                           int output_height) {
    if (!_gpu_info_logged && debug_enabled()) {
        _gpu_info_logged = true;
        std::fprintf(stderr, "AGS shader GPU vendor: %s\n", gl_string(GL_VENDOR));
        std::fprintf(stderr, "AGS shader GPU renderer: %s\n", gl_string(GL_RENDERER));
        std::fprintf(stderr, "AGS shader OpenGL: %s\n", gl_string(GL_VERSION));
#ifdef GL_SHADING_LANGUAGE_VERSION
        std::fprintf(stderr, "AGS shader GLSL: %s\n", gl_string(GL_SHADING_LANGUAGE_VERSION));
#endif
    }

    unsigned base_texture = input_texture;
    int base_width = input_width;
    int base_height = input_height;
    bool force_exact_copy = false;

    AgsNativeSource native_source;
    const bool using_native_source =
        ags_native_source_acquire(output_width, output_height, native_source);
    if (using_native_source) {
        base_texture = native_source.texture;
        base_width = native_source.width;
        base_height = native_source.height;
        force_exact_copy = native_source.texture_width != native_source.width ||
                           native_source.texture_height != native_source.height;

        static unsigned last_native_texture = 0;
        static int last_native_width = -1;
        static int last_native_height = -1;
        if (debug_enabled() &&
            (last_native_texture != native_source.texture ||
             last_native_width != native_source.width ||
             last_native_height != native_source.height)) {
            last_native_texture = native_source.texture;
            last_native_width = native_source.width;
            last_native_height = native_source.height;
            std::fprintf(stderr,
                         "AGS shader: native AGS source fbo=%u texture=%u logical=%dx%d backing=%dx%d -> output=%dx%d%s\n",
                         native_source.fbo,
                         native_source.texture,
                         native_source.width,
                         native_source.height,
                         native_source.texture_width,
                         native_source.texture_height,
                         output_width,
                         output_height,
                         force_exact_copy ? " (GPU crop to logical size)" : "");
        }
    }

    // From this point until the pipeline returns, FBO activity belongs to the
    // injector, not AGS. Keep it out of native-target discovery.
    ags_native_source_set_pipeline_active(true);

    unsigned source_texture = base_texture;
    int source_width = base_width;
    int source_height = base_height;
    const bool prepared = prepare_source_texture(base_texture,
                                                 base_width,
                                                 base_height,
                                                 source_texture,
                                                 source_width,
                                                 source_height,
                                                 force_exact_copy);

    static int last_logged_input_width = -1;
    static int last_logged_input_height = -1;
    static int last_logged_source_width = -1;
    static int last_logged_source_height = -1;
    if (debug_enabled() && prepared &&
        (source_width != base_width || source_height != base_height || force_exact_copy) &&
        (last_logged_input_width != base_width ||
         last_logged_input_height != base_height ||
         last_logged_source_width != source_width ||
         last_logged_source_height != source_height)) {
        last_logged_input_width = base_width;
        last_logged_input_height = base_height;
        last_logged_source_width = source_width;
        last_logged_source_height = source_height;
        std::fprintf(stderr,
                     "AGS shader: hardware source preparation %dx%d -> %dx%d (%s)\n",
                     base_width,
                     base_height,
                     source_width,
                     source_height,
                     source_filter() == GL_LINEAR ? "linear" : "nearest");
    }

    _impl.apply(source_texture,
                source_width,
                source_height,
                output_width,
                output_height);

    ags_native_source_set_pipeline_active(false);
}
