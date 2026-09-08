#include "gfx/ags_shader_pipeline.h"

#include "gfx/ogl_headers.h"
#include "glad/glad.h"

#include <SDL.h>

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
typedef size_t (*PFN_libra_instance_api_version)(void);
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

template <typename T>
bool LoadSymbol(void *library, const char *name, T &out)
{
    dlerror();
    out = reinterpret_cast<T>(dlsym(library, name));
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
    PFN_libra_instance_api_version instance_api_version = nullptr;
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
        if (!LoadSymbol(library, symbol, member)) \
        { \
            error = std::string("missing librashader symbol: ") + symbol; \
            return false; \
        }

        LOAD_LIBRA_SYMBOL(instance_abi_version, "libra_instance_abi_version");
        LOAD_LIBRA_SYMBOL(instance_api_version, "libra_instance_api_version");
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
        if (output_fbo)
            glDeleteFramebuffers(1, &output_fbo);
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

        glGenFramebuffers(1, &output_fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, output_fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, output_texture, 0);

        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
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

    if (!GLAD_GL_VERSION_3_3)
    {
        const char *version = reinterpret_cast<const char *>(glGetString(GL_VERSION));
        error = "librashader requires OpenGL 3.3+";
        if (version)
        {
            error += " (current context: ";
            error += version;
            error += ")";
        }
        return false;
    }

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
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, old_draw_fbo);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, old_read_fbo);
        glReadBuffer(old_read_buffer);
        glActiveTexture(old_active_texture);
        glBindTexture(GL_TEXTURE_2D, old_texture);
        glViewport(old_viewport[0], old_viewport[1],
                   old_viewport[2], old_viewport[3]);
        return;
    }

    // Capture the already-rendered AGS backbuffer as librashader's source texture.
    glBindFramebuffer(GL_READ_FRAMEBUFFER, old_read_fbo);
    glReadBuffer(old_read_buffer);
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
        // librashader renders to a caller-owned texture. Copy that texture back
        // to the framebuffer AGS is about to present.
        glBindFramebuffer(GL_READ_FRAMEBUFFER, _impl->output_fbo);
        glReadBuffer(GL_COLOR_ATTACHMENT0);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, old_draw_fbo);
        glBlitFramebuffer(0, 0, output_width, output_height,
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

    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, old_draw_fbo);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, old_read_fbo);
    glReadBuffer(old_read_buffer);
    glActiveTexture(old_active_texture);
    glBindTexture(GL_TEXTURE_2D, old_texture);
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
