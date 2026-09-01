# AGS native GLSL runtime-only shader support — phase 1

Target: `Paolo86cripple/ags-shaderssupport`.

This is deliberately a **runtime-only** feature. No AGS Editor changes are
needed and the shipped AGS game data does not need to be modified.

## Files to add

Copy these two files into the AGS source tree:

- `Engine/gfx/ags_shader_pipeline.h`
- `Engine/gfx/ags_shader_pipeline.cpp`

The files under `shaders/` are sample external shaders and can remain anywhere
on disk; they are not required to be compiled into the runtime.

## Runtime wiring

### `Engine/ac/gamesetup.h`

Inside `GameConfig`, next to the existing graphics fields, add:

```cpp
String  ShaderPath;
```

### `Engine/main/config.cpp`

In the graphics configuration read section, after the existing
`software_driver` read, add:

```cpp
setup.ShaderPath = CfgReadString(cfg, "graphics", "shader");
```

### `Engine/main/main.cpp`

Next to the existing `--gfxfilter` command-line handling, add:

```cpp
else if ((ags_stricmp(arg, "--shader") == 0) && (argc > ee + 1))
{
    cfg["graphics"]["shader"] = argv[++ee];
}
```

### `Engine/gfx/ali3dogl.h`

Add:

```cpp
#include "gfx/ags_shader_pipeline.h"
```

In the private section of `OGLGraphicsDriver`, add:

```cpp
void ApplyShaderPipeline();
std::unique_ptr<AGSShaderPipeline> _shaderPipeline;
```

### `Engine/gfx/ali3dogl.cpp`

Add:

```cpp
#include "ac/gamesetup.h"
#include "gfx/ags_shader_pipeline.h"
```

At the end of `FirstTimeInit()`, after the existing built-in shader creation:

```cpp
_shaderPipeline.reset(new AGSShaderPipeline());

if (!usetup.ShaderPath.IsEmpty())
{
    String shader_error;
    if (!_shaderPipeline->Load(usetup.ShaderPath, shader_error))
    {
        Debug::Printf(kDbgMsg_Error,
            "AGS shader pipeline: %s", shader_error.GetCStr());
        _shaderPipeline->Clear();
    }
}
```

Add this method:

```cpp
void OGLGraphicsDriver::ApplyShaderPipeline()
{
    if (!_shaderPipeline || !_shaderPipeline->IsLoaded() || !_nativeSurface)
        return;

    OGLTexture *texture = _nativeSurface->GetTexture();
    if (!texture || texture->_numTiles != 1)
    {
        Debug::Printf(kDbgMsg_Warn,
            "AGS shader pipeline disabled: native surface uses multiple texture tiles.");
        return;
    }

    _shaderPipeline->Apply(
        texture->_tiles[0].texture,
        _nativeSurface->GetSize(),
        _screenBackbuffer.Fbo,
        _screenBackbuffer.SurfSize,
        _screenBackbuffer.Viewport);
}
```

Replace the start of `RenderImpl()` so an active shader forces native rendering
to the FBO and then post-processes it:

```cpp
void OGLGraphicsDriver::RenderImpl(bool clearDrawListAfterwards)
{
    if (_shaderPipeline && _shaderPipeline->IsLoaded() && _nativeSurface)
    {
        const bool old_render_to_texture = _doRenderToTexture;
        _doRenderToTexture = true;

        RenderToSurface(&_nativeBackbuffer, clearDrawListAfterwards);

        _doRenderToTexture = old_render_to_texture;
        ApplyShaderPipeline();
        glFinish();
        return;
    }

    // Keep the existing AGS implementation below this block unchanged.
    ...
}
```

In `UnInit()`, before destroying the OpenGL context:

```cpp
_shaderPipeline.reset();
```

### `Engine/CMakeLists.txt`

Add to `target_sources(engine PRIVATE ...)`:

```cmake
gfx/ags_shader_pipeline.cpp
gfx/ags_shader_pipeline.h
```

## Usage

Single pass:

```bash
ags --gfxdriver ogl --shader /path/to/shader.glsl game.exe
```

Multipass:

```bash
ags --gfxdriver ogl --shader /path/to/crt-blur.agschain game.exe
```

Example chain:

```text
pass=blur-pass.glsl
pass=crt-simple.glsl
```

Shader paths inside a chain are relative to the `.agschain` file.

## Shader interface

The fragment shader can use:

```glsl
uniform sampler2D uTexture;
uniform vec2 uInputSize;
uniform vec2 uOutputSize;
uniform vec2 uOriginalSize;
uniform vec2 uTexelSize;
uniform float uTime;
uniform float uFrameCount;
```

The pipeline uses the AGS runtime's existing OpenGL context (currently
OpenGL 2.1 / GLSL 1.20), rather than introducing a new renderer.

## Deliberate phase-1 limitations

- GLSL only; no HLSL/SPIR-V yet.
- Intermediate passes use the final output resolution.
- The native AGS surface must be represented by one GL texture tile.
- No RetroArch/libretro dependency.
- No Editor integration.
- No changes to the shipped game data are required when using `--shader`.
