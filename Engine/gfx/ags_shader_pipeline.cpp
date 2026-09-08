#include "gfx/ags_shader_pipeline.h"

#include "gfx/ogl_headers.h"

#include <SDL.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#if defined(__linux__) && !AGS_OPENGL_ES2
#include <dlfcn.h>
#endif

namespace AGS
{
namespace Engine
{
namespace OGL
{

#if defined(__linux__) && !AGS_OPENGL_ES2

#ifndef GL_FRAMEBUFFER
#define GL_FRAMEBUFFER 0x8D40
#endif
#ifndef GL_READ_FRAMEBUFFER
#define GL_READ_FRAMEBUFFER 0x8CA8
#endif
#ifndef GL_DRAW_FRAMEBUFFER
#define GL_DRAW_FRAMEBUFFER 0x8CA9
#endif
#ifndef GL_FRAMEBUFFER_COMPLETE
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#endif
#ifndef GL_COLOR_ATTACHMENT0
#define GL_COLOR_ATTACHMENT0 0x8CE0
#endif
#ifndef GL_DRAW_FRAMEBUFFER_BINDING
#define GL_DRAW_FRAMEBUFFER_BINDING 0x8CA6
#endif
#ifndef GL_READ_FRAMEBUFFER_BINDING
#define GL_READ_FRAMEBUFFER_BINDING 0x8CAA
#endif

namespace
{

struct _libra_error;
struct _shader_preset;
struct _preset_ctx;
struct _filter_chain_gl;

typedef _libra_error *libra_error_t;
typedef _shader_preset *libra_shader_preset_t;
typedef _preset_ctx *libra_preset_ctx_t;
typedef _filter_chain_gl *libra_gl_filter_chain_t;
typedef const void *(*libra_gl_loader_t)(const char *);

enum LIBRA_PRESET_CTX_RUNTIME
{
    LIBRA_PRESET_CTX_RUNTIME_NONE = 0,
    LIBRA_PRESET_CTX_RUNTIME_GL_CORE = 1
};

struct libra_image_gl_t
{
    uint32_t handle;
    uint32_t format;
    uint32_t width;
    uint32_t height;
};

struct libra_viewport_t
{
    float x;
    float y;
    uint32_t width;
    uint32_t height;
};

typedef size_t (*PFN_libra_instance_abi_version)(void);
typedef libra_error_t (*PFN_libra_preset_ctx_create)(libra_preset_ctx_t *);
typedef libra_error_t (*PFN_libra_preset_ctx_free)(libra_preset_ctx_t *);
typedef libra_error_t (*PFN_libra_preset_ctx_set_runtime)(libra_preset_ctx_t *, LIBRA_PRESET_CTX_RUNTIME);
typedef libra_error_t (*PFN_libra_preset_create_with_context)(
    const char *, libra_preset_ctx_t *, libra_shader_preset_t *);
typedef libra_error_t (*PFN_libra_gl_filter_chain_create)(
    libra_shader_preset_t *, libra_gl_loader_t, const void *, libra_gl_filter_chain_t *);
typedef libra_error_t (*PFN_libra_gl_filter_chain_frame)(
    libra_gl_filter_chain_t *, size_t, libra_image_gl_t, libra_image_gl_t,
    const libra_viewport_t *, const float *, const void *);
typedef libra_error_t (*PFN_libra_gl_filter_chain_free)(libra_gl_filter_chain_t *);
typedef int32_t (*PFN_libra_error_free)(libra_error_t *);
typedef int32_t (*PFN_libra_error_write)(libra_error_t, char **);
typedef int32_t (*PFN_libra_error_free_string)(char **);

// AGS' bundled GLAD intentionally exposes only OpenGL 2.1 + EXT FBO.
// Keep that loader untouched and resolve the small OpenGL 3.x surface required
// by the librashader bridge directly from the active SDL context.
typedef void (APIENTRY *PFN_AGS_GL_GEN_FRAMEBUFFERS)(GLsizei, GLuint *);
typedef void (APIENTRY *PFN_AGS_GL_DELETE_FRAMEBUFFERS)(GLsizei, const GLuint *);
typedef void (APIENTRY *PFN_AGS_GL_BIND_FRAMEBUFFER)(GLenum, GLuint);
typedef GLenum (APIENTRY *PFN_AGS_GL_CHECK_FRAMEBUFFER_STATUS)(GLenum);
typedef void (APIENTRY *PFN_AGS_GL_FRAMEBUFFER_TEXTURE_2D)(GLenum, GLenum, GLenum, GLuint, GLint);
typedef void (APIENTRY *PFN_AGS_GL_BLIT_FRAMEBUFFER)(
    GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum);

const size_t kLibrashaderAbi = 2;

const void *LoadOpenGLProc(const char *name)
{
    return SDL_GL_GetProcAddress(name);
}

bool HasSuffix(const std::string &value, const char *suffix)
{
    const size_t suffix_len = std::strlen(suffix);
    if (value.size() < suffix_len)
        return false;

    const size_t offset = value.size() - suffix_len;
    for (size_t i = 0; i < suffix_len; ++i)
    {
        const char lhs = value[offset + i];
        const char rhs = suffix[i];
        const char lhs_lower = (lhs >= 'A' && lhs <= 'Z') ? static_cast<char>(lhs - 'A' + 'a') : lhs;
        const char rhs_lower = (rhs >= 'A' && rhs <= 'Z') ? static_cast<char>(rhs - 'A' + 'a') : rhs;
        if (lhs_lower != rhs_lower)
            return false;
    }
    return true;
}

bool CurrentOpenGLAtLeast(int required_major, int required_minor,
                          std::string &version_string)
{
    const char *version = reinterpret_cast<const char *>(glGetString(GL_VERSION));
    if (!version)
    {
        version_string = "unknown";
        return false;
    }

    version_string = version;
    int major = 0;
    int minor = 0;
    if (std::sscanf(version, "%d.%d", &major, &minor) != 2)
        return false;

    return (major > required_major) ||
           (major == required_major && minor >= required_minor);
}

template <typename T>
bool LoadLibrarySymbol(void *library, const char *name, T &out)
{
    dlerror();
    out = reinterpret_cast<T>(dlsym(library, name));
    return out != nullptr;
}

template <typename T>
bool LoadGLSymbol(const char *name, T &out)
{
    out = reinterpret_cast<T>(SDL_GL_GetProcAddress(name));
    return out != nullptr;
}

} // namespace

struct AGSShaderPipeline::Impl
{
    void *library = nullptr;
    libra_gl_filter_chain_t chain = nullptr;

    GLuint input_texture = 0;
    GLuint output_texture = 0;
    GLuint output_fbo = 0;
    int target_width = 0;
    int target_height = 0;
    size_t frame_count = 0;
    bool frame_error_reported = false;

    PFN_libra_instance_abi_version instance_abi_version = nullptr;
    PFN_libra_preset_ctx_create preset_ctx_create = nullptr;
    PFN_libra_preset_ctx_free preset_ctx_free = nullptr;
    PFN_libra_preset_ctx_set_runtime preset_ctx_set_runtime = nullptr;
    PFN_libra_preset_create_with_context preset_create_with_context = nullptr;
    PFN_libra_gl_filter_chain_create gl_filter_chain_create = nullptr;
    PFN_libra_gl_filter_chain_frame gl_filter_chain_frame = nullptr;
    PFN_libra_gl_filter_chain_free gl_filter_chain_free = nullptr;
    PFN_libra_error_free error_free = nullptr;
    PFN_libra_error_write error_write = nullptr;
    PFN_libra_error_free_string error_free_string = nullptr;

    PFN_AGS_GL_GEN_FRAMEBUFFERS GenFramebuffers = nullptr;
    PFN_AGS_GL_DELETE_FRAMEBUFFERS DeleteFramebuffers = nullptr;
    PFN_AGS_GL_BIND_FRAMEBUFFER BindFramebuffer = nullptr;
    PFN_AGS_GL_CHECK_FRAMEBUFFER_STATUS CheckFramebufferStatus = nullptr;
    PFN_AGS_GL_FRAMEBUFFER_TEXTURE_2D FramebufferTexture2D = nullptr;
    PFN_AGS_GL_BLIT_FRAMEBUFFER BlitFramebuffer = nullptr;

    std::string ConsumeError(libra_error_t error)
    {
        if (!error)
            return std::string();

        std::string message("librashader error");
        char *raw = nullptr;
        if (error_write && error_write(error, &raw) == 0 && raw)
        {
            message = raw;
            if (error_free_string)
                error_free_string(&raw);
        }

        if (error_free)
            error_free(&error);
        return message;
    }

    bool LoadModernGL(std::string &error)
    {
#define LOAD_GL_SYMBOL(member, symbol) \
        if (!LoadGLSymbol(symbol, member)) \
        { \
            error = std::string("OpenGL 3.x entry point unavailable: ") + symbol; \
            return false; \
        }

        LOAD_GL_SYMBOL(GenFramebuffers, "glGenFramebuffers");
        LOAD_GL_SYMBOL(DeleteFramebuffers, "glDeleteFramebuffers");
        LOAD_GL_SYMBOL(BindFramebuffer, "glBindFramebuffer");
        LOAD_GL_SYMBOL(CheckFramebufferStatus, "glCheckFramebufferStatus");
        LOAD_GL_SYMBOL(FramebufferTexture2D, "glFramebufferTexture2D");
        LOAD_GL_SYMBOL(BlitFramebuffer, "glBlitFramebuffer");
#undef LOAD_GL_SYMBOL
        return true;
    }

    bool OpenLibrary(std::string &error)
    {
        if (library)
            return true;

        const char *candidates[] = {
            "librashader.so",
            "librashader.so.2"
        };

        for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); ++i)
        {
            library = dlopen(candidates[i], RTLD_NOW | RTLD_LOCAL);
            if (library)
                break;
        }

        if (!library)
        {
            const char *dl_error = dlerror();
            error = "cannot load librashader.so";
            if (dl_error && dl_error[0])
            {
                error += ": ";
                error += dl_error;
            }
            return false;
        }

#define LOAD_LIBRA_SYMBOL(member, symbol) \
        if (!LoadLibrarySymbol(library, symbol, member)) \
        { \
            error = std::string("missing librashader symbol: ") + symbol; \
            return false; \
        }

        LOAD_LIBRA_SYMBOL(instance_abi_version, "libra_instance_abi_version");
        LOAD_LIBRA_SYMBOL(preset_ctx_create, "libra_preset_ctx_create");
        LOAD_LIBRA_SYMBOL(preset_ctx_free, "libra_preset_ctx_free");
        LOAD_LIBRA_SYMBOL(preset_ctx_set_runtime, "libra_preset_ctx_set_runtime");
        LOAD_LIBRA_SYMBOL(preset_create_with_context, "libra_preset_create_with_context");
        LOAD_LIBRA_SYMBOL(gl_filter_chain_create, "libra_gl_filter_chain_create");
        LOAD_LIBRA_SYMBOL(gl_filter_chain_frame, "libra_gl_filter_chain_frame");
        LOAD_LIBRA_SYMBOL(gl_filter_chain_free, "libra_gl_filter_chain_free");
        LOAD_LIBRA_SYMBOL(error_free, "libra_error_free");
        LOAD_LIBRA_SYMBOL(error_write, "libra_error_write");
        LOAD_LIBRA_SYMBOL(error_free_string, "libra_error_free_string");
#undef LOAD_LIBRA_SYMBOL

        const size_t abi = instance_abi_version();
        if (abi != kLibrashaderAbi)
        {
            error = "unsupported librashader ABI " + std::to_string(abi) +
                    " (AGS shader backend expects ABI " + std::to_string(kLibrashaderAbi) + ")";
            return false;
        }

        return true;
    }

    void DestroyTargets()
    {
        if (output_fbo && DeleteFramebuffers)
            DeleteFramebuffers(1, &output_fbo);
        if (output_texture)
            glDeleteTextures(1, &output_texture);
        if (input_texture)
            glDeleteTextures(1, &input_texture);

        output_fbo = 0;
        output_texture = 0;
        input_texture = 0;
        target_width = 0;
        target_height = 0;
    }

    bool EnsureTargets(int width, int height, std::string &error)
    {
        if (input_texture && output_texture && output_fbo &&
            target_width == width && target_height == height)
            return true;

        DestroyTargets();

        glGenTextures(1, &input_texture);
        glBindTexture(GL_TEXTURE_2D, input_texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

        glGenTextures(1, &output_texture);
        glBindTexture(GL_TEXTURE_2D, output_texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

        GenFramebuffers(1, &output_fbo);
        BindFramebuffer(GL_FRAMEBUFFER, output_fbo);
        FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                             GL_TEXTURE_2D, output_texture, 0);

        if (CheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        {
            error = "failed to create librashader output framebuffer";
            DestroyTargets();
            return false;
        }

        target_width = width;
        target_height = height;
        return true;
    }

    void CloseLibrary()
    {
        if (library)
            dlclose(library);
        library = nullptr;
    }
};

#else

struct AGSShaderPipeline::Impl
{
};

#endif

AGSShaderPipeline::AGSShaderPipeline()
    : _impl(new Impl())
{
}

AGSShaderPipeline::~AGSShaderPipeline()
{
    Clear();
    delete _impl;
    _impl = nullptr;
}

bool AGSShaderPipeline::IsLoaded() const
{
#if defined(__linux__) && !AGS_OPENGL_ES2
    return _impl && _impl->chain != nullptr;
#else
    return false;
#endif
}

bool AGSShaderPipeline::Load(const std::string &path, std::string &error)
{
    Clear();

#if !defined(__linux__) || AGS_OPENGL_ES2
    (void)path;
    error = "librashader backend is currently available only on Linux desktop OpenGL";
    return false;
#else
    if (!HasSuffix(path, ".slangp"))
    {
        error = "librashader backend expects a RetroArch .slangp preset";
        return false;
    }

    std::string gl_version;
    if (!CurrentOpenGLAtLeast(3, 3, gl_version))
    {
        error = "librashader requires OpenGL 3.3+ (current context: " + gl_version + ")";
        return false;
    }

    if (!_impl->LoadModernGL(error))
        return false;

    if (!_impl->OpenLibrary(error))
    {
        _impl->CloseLibrary();
        return false;
    }

    libra_preset_ctx_t context = nullptr;
    libra_error_t libra_error = _impl->preset_ctx_create(&context);
    if (libra_error)
    {
        error = _impl->ConsumeError(libra_error);
        _impl->CloseLibrary();
        return false;
    }

    libra_error = _impl->preset_ctx_set_runtime(
        &context, LIBRA_PRESET_CTX_RUNTIME_GL_CORE);
    if (libra_error)
    {
        error = _impl->ConsumeError(libra_error);
        _impl->preset_ctx_free(&context);
        _impl->CloseLibrary();
        return false;
    }

    libra_shader_preset_t preset = nullptr;
    libra_error = _impl->preset_create_with_context(
        path.c_str(), &context, &preset);
    if (libra_error)
    {
        error = _impl->ConsumeError(libra_error);
        if (context)
            _impl->preset_ctx_free(&context);
        _impl->CloseLibrary();
        return false;
    }

    // preset_create_with_context consumes the context.
    context = nullptr;

    libra_error = _impl->gl_filter_chain_create(
        &preset, LoadOpenGLProc, nullptr, &_impl->chain);
    if (libra_error)
    {
        error = _impl->ConsumeError(libra_error);
        _impl->chain = nullptr;
        _impl->CloseLibrary();
        return false;
    }

    _impl->frame_count = 0;
    _impl->frame_error_reported = false;
    return true;
#endif
}

void AGSShaderPipeline::Clear()
{
#if defined(__linux__) && !AGS_OPENGL_ES2
    if (!_impl)
        return;

    if (_impl->chain && _impl->gl_filter_chain_free)
    {
        libra_error_t error = _impl->gl_filter_chain_free(&_impl->chain);
        if (error)
            _impl->ConsumeError(error);
    }
    _impl->chain = nullptr;

    _impl->DestroyTargets();
    _impl->CloseLibrary();
    _impl->frame_count = 0;
    _impl->frame_error_reported = false;
#endif
}

void AGSShaderPipeline::Apply(int input_width, int input_height,
                              int output_width, int output_height)
{
#if defined(__linux__) && !AGS_OPENGL_ES2
    if (!_impl || !_impl->chain ||
        input_width <= 0 || input_height <= 0 ||
        output_width <= 0 || output_height <= 0)
        return;

    // The current AGS integration feeds the completed backbuffer, therefore
    // input and output dimensions are expected to match. Keep the guard here
    // until the renderer exposes a separate logical-source texture.
    if (input_width != output_width || input_height != output_height)
    {
        if (!_impl->frame_error_reported)
        {
            std::fprintf(stderr,
                         "AGS librashader: source/output size mismatch (%dx%d -> %dx%d)\n",
                         input_width, input_height, output_width, output_height);
            _impl->frame_error_reported = true;
        }
        return;
    }

    GLint old_draw_fbo = 0;
    GLint old_read_fbo = 0;
    GLint old_read_buffer = 0;
    GLint old_active_texture = GL_TEXTURE0;
    GLint old_texture = 0;
    GLint old_viewport[4] = {0, 0, 0, 0};

    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &old_draw_fbo);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &old_read_fbo);
    glGetIntegerv(GL_READ_BUFFER, &old_read_buffer);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &old_active_texture);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &old_texture);
    glGetIntegerv(GL_VIEWPORT, old_viewport);

    std::string target_error;
    if (!_impl->EnsureTargets(output_width, output_height, target_error))
    {
        if (!_impl->frame_error_reported)
        {
            std::fprintf(stderr, "AGS librashader: %s\n", target_error.c_str());
            _impl->frame_error_reported = true;
        }
        _impl->BindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(old_draw_fbo));
        _impl->BindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(old_read_fbo));
        glReadBuffer(static_cast<GLenum>(old_read_buffer));
        glActiveTexture(static_cast<GLenum>(old_active_texture));
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(old_texture));
        glViewport(old_viewport[0], old_viewport[1],
                   old_viewport[2], old_viewport[3]);
        return;
    }

    // Capture the already-rendered AGS backbuffer as librashader's source.
    _impl->BindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(old_read_fbo));
    glReadBuffer(static_cast<GLenum>(old_read_buffer));
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, _impl->input_texture);
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0,
                        output_width, output_height);

    const libra_image_gl_t input = {
        static_cast<uint32_t>(_impl->input_texture),
        static_cast<uint32_t>(GL_RGBA8),
        static_cast<uint32_t>(output_width),
        static_cast<uint32_t>(output_height)
    };
    const libra_image_gl_t output = {
        static_cast<uint32_t>(_impl->output_texture),
        static_cast<uint32_t>(GL_RGBA8),
        static_cast<uint32_t>(output_width),
        static_cast<uint32_t>(output_height)
    };
    const libra_viewport_t viewport = {
        0.0f, 0.0f,
        static_cast<uint32_t>(output_width),
        static_cast<uint32_t>(output_height)
    };

    libra_error_t libra_error = _impl->gl_filter_chain_frame(
        &_impl->chain, _impl->frame_count++, input, output,
        &viewport, nullptr, nullptr);

    if (!libra_error)
    {
        // librashader renders to a caller-owned texture. Copy the completed
        // texture back to the framebuffer AGS is about to present.
        _impl->BindFramebuffer(GL_READ_FRAMEBUFFER, _impl->output_fbo);
        glReadBuffer(GL_COLOR_ATTACHMENT0);
        _impl->BindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(old_draw_fbo));
        _impl->BlitFramebuffer(0, 0, output_width, output_height,
                               0, 0, output_width, output_height,
                               GL_COLOR_BUFFER_BIT, GL_NEAREST);
    }
    else if (!_impl->frame_error_reported)
    {
        const std::string message = _impl->ConsumeError(libra_error);
        std::fprintf(stderr, "AGS librashader frame failed: %s\n", message.c_str());
        _impl->frame_error_reported = true;
    }
    else
    {
        _impl->ConsumeError(libra_error);
    }

    _impl->BindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(old_draw_fbo));
    _impl->BindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(old_read_fbo));
    glReadBuffer(static_cast<GLenum>(old_read_buffer));
    glActiveTexture(static_cast<GLenum>(old_active_texture));
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(old_texture));
    glViewport(old_viewport[0], old_viewport[1],
               old_viewport[2], old_viewport[3]);
#else
    (void)input_width;
    (void)input_height;
    (void)output_width;
    (void)output_height;
#endif
}

} // namespace OGL
} // namespace Engine
} // namespace AGS
