# AGS Shader Injector (Linux)

This directory contains a **per-game native Linux post-processing add-on**. It does not replace the AGS engine and does not require Wine, Proton, RetroArch or libretro.

The current backend targets AGS games using SDL2/OpenGL on Linux. The shared library is loaded with `LD_PRELOAD`, intercepts `SDL_GL_SwapWindow()`, captures the current OpenGL backbuffer, applies an external shader pipeline, and then lets SDL present the processed frame.

The original game files are not modified.

## Per-game layout

```text
game/
├── game executable / native AGS runtime
├── libags-shader.so
├── shaders/
│   ├── identity.glsl
│   ├── invert.glsl
│   └── presets...
└── run-with-shader.sh
```

## Single GLSL shader

```bash
AGS_SHADER_CHAIN="$PWD/shaders/invert.glsl" \
LD_PRELOAD="$PWD/libags-shader.so" \
./game
```

## Multipass chain

```text
# shaders/invert-chain.agschain
pass=identity.glsl
pass=invert.glsl
```

Run it with:

```bash
AGS_SHADER_CHAIN="$PWD/shaders/invert-chain.agschain" \
LD_PRELOAD="$PWD/libags-shader.so" \
./game
```

## Libretro / RetroArch classic GLSL presets

The pipeline supports classic Libretro/RetroArch `.glsl` and `.glslp` pipelines, including multipass presets, aliases, parameters, pass/history/feedback textures, PNG/JPEG LUTs, mipmaps, wrap/filter/scaling controls, float/sRGB framebuffers, recursive `#reference` presets and the classic RetroArch GLSL uniform/attribute ABI.

CI pins a known `libretro/glsl-shaders` revision and strictly load-tests every usable `.glslp` preset plus every standalone `.glsl` source in that revision.

## GPU source resampling for retro upscalers

Some classic upscalers assume their `Source` is a low-resolution emulator/core image. An AGS game may already have been scaled to the full window before the injector sees it. Feeding a 1920x1080 source to a preset that internally scales by 3x and then 2x can therefore create extremely large render targets.

`AGS_SHADER_SOURCE_SIZE=WIDTHxHEIGHT` enables an **all-GPU pre-resample** before the Libretro pipeline. The injector attaches the captured AGS texture to an OpenGL read framebuffer and uses `glBlitFramebuffer` into a reusable source texture; there is no CPU readback in normal rendering.

Example:

```bash
export AGS_SHADER_SOURCE_SIZE=640x360
export AGS_SHADER_SOURCE_FILTER=nearest
```

`AGS_SHADER_SOURCE_FILTER` accepts `nearest` (default, useful for pixel art) or `linear`.

With `AGS_SHADER_DEBUG=1`, the injector prints the active OpenGL vendor, renderer, version and GLSL version. This makes hardware/software rendering explicit (`radeonsi`/Radeon versus `llvmpipe`, for example), and reports an active source resample such as:

```text
AGS shader: hardware source resample 1920x1080 -> 640x360 (nearest)
```

## Per-pass framebuffer diagnostics

Set `AGS_SHADER_DUMP_PASSES_DIR` to capture the first rendered shader frame **after every pipeline pass**. This mode intentionally uses `glReadPixels` and PNG encoding and is therefore for diagnostics only; it has no cost when the variable is unset.

Example for a 12-pass CRT Royale preset:

```bash
export AGS_SHADER_DUMP_PASSES_DIR=/home/paolo/pictures/crt-royale-passes
export AGS_SHADER_DUMP_PASS_COUNT=12
```

The directory will contain files such as:

```text
pass-00-1920x1080.png
pass-01-1920x1080.png
pass-02-320x240.png
...
pass-11-1920x1080.png
```

The dump normally stops automatically when the final pass returns to AGS' default framebuffer. `AGS_SHADER_DUMP_PASS_COUNT` is a fallback limit for hosts that keep a non-zero presentation framebuffer bound.

## Pixel-perfect screenshots

Press **F12** while the AGS game window has keyboard focus. The injector reads the final OpenGL backbuffer **after the shader pipeline has been applied and before `SDL_GL_SwapWindow()` presents it**, then writes an RGBA PNG. This avoids compositor/window-manager scaling and is suitable for pixel comparisons between shader runs.

The screenshot directory is selected in this order:

1. `AGS_SHADER_SCREENSHOT_DIR`, if set;
2. `XDG_PICTURES_DIR` from `~/.config/user-dirs.dirs` (or `$XDG_CONFIG_HOME/user-dirs.dirs`);
3. fallback: `$HOME/Pictures`.

Example forcing an exact directory:

```bash
export AGS_SHADER_SCREENSHOT_DIR=/home/paolo/pictures
```

Linux paths are case-sensitive, so `/home/paolo/pictures` and `/home/paolo/Pictures` are different directories.

Files are named like:

```text
ags-shader-20260904-193500-123-001.png
```

On success the injector also prints the full path to stderr:

```text
AGS shader screenshot: /path/to/ags-shader-....png
```

## Environment variables

- `AGS_SHADER_CHAIN`: path to `.glsl`, `.agschain` or `.glslp`.
- `AGS_SHADER`: legacy single-shader fallback if `AGS_SHADER_CHAIN` is unset.
- `AGS_SHADER_DEBUG=1`: enable diagnostics on stderr, including OpenGL GPU/driver information.
- `AGS_SHADER_SCREENSHOT_DIR`: override the F12 screenshot directory.
- `AGS_SHADER_SOURCE_SIZE=WIDTHxHEIGHT`: optional GPU-side source resample before the shader pipeline.
- `AGS_SHADER_SOURCE_FILTER=nearest|linear`: filter for the GPU source resample; default is `nearest`.
- `AGS_SHADER_DUMP_PASSES_DIR`: opt-in directory for one-frame per-pass framebuffer dumps.
- `AGS_SHADER_DUMP_PASS_COUNT`: optional maximum number of pass dumps; default 64.

## Standard uniforms

In addition to the classic RetroArch GLSL ABI, the injector provides its native convenience uniforms when declared by a shader:

```glsl
uniform sampler2D uTexture;
uniform vec2 uInputSize;
uniform vec2 uOutputSize;
uniform vec2 uOriginalSize;
uniform vec2 uTexelSize;
uniform vec2 TextureSize;
uniform float uTime;
uniform float uFrameCount;
uniform int FrameCount;
uniform int FrameDirection;
```
