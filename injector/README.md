# AGS Shader Injector (Linux)

This directory contains a **per-game native Linux post-processing add-on**. It does not replace the AGS engine and does not require Wine, Proton, RetroArch, libretro or ScummVM at runtime.

The current backend targets AGS games using SDL2/OpenGL on Linux. The shared library is loaded with `LD_PRELOAD`, applies an external shader pipeline immediately before SDL presents the processed frame, and leaves the original game files untouched.

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

## RetroArch GL2 renderchain compatibility

The active v4 backend is wrapped by a desktop OpenGL adapter derived from RetroArch's classic GL2 renderchain and GLSL backend. This is not a runtime dependency on RetroArch: the relevant behavior is adapted into the injector and built into `libags-shader.so`.

Important imported behavior includes:

- separate logical image size and backing texture size for FBO passes;
- power-of-two backing textures used by the classic GL2 renderchain;
- matching `InputSize`, `TextureSize` and valid-image UV ratios;
- RetroArch-style float framebuffer allocation and sRGB behavior;
- classic frame/global uniforms;
- cached `glGetUniformLocation()` results;
- cached wrap/filter texture parameters;
- pass/reference texture coordinates tied to the referenced backing texture.

A dedicated CI fixture verifies that a logical 3x5 intermediate pass is exposed to the next shader as `InputSize=3x5`, `TextureSize=4x8`, with UVs restricted to the valid 3x5 image area.

## ScummVM-inspired execution optimizations

ScummVM's OpenGL LibRetroPipeline is used as a second reference for **host-side execution**, especially because it runs Libretro GLSL pipelines efficiently on adventure-game content. There is no ScummVM runtime dependency.

The injector moves the same kinds of work out of the hot render loop:

- auxiliary `*TexCoord` attributes and their corresponding sampler uniforms are discovered once per linked GL program, instead of by `glGetActiveAttrib()` / `glGetAttribLocation()` every draw;
- sampler-unit values written with `glUniform1i()` are cached, avoiding repeated `glGetUniformiv()` queries during pass rendering;
- program cache entries are discarded when a program is destroyed, so shader reloads cannot reuse stale GL metadata;
- existing texture-unit allocation already deduplicates multiple references to the same GL texture;
- frame history rotates reusable targets rather than copying the whole history chain.

The relevant ScummVM notices are recorded in the repository root `THIRD_PARTY.md`.

## Native AGS logical source (experimental)

This is the key performance path for retro upscalers such as ScaleFX.

AGS' OpenGL renderer can render the game to an internal `_nativeSurface` at the logical game resolution and then scale that texture to the real screen backbuffer. This is the same architectural point at which ScummVM feeds its Libretro pipeline. The standalone injector can discover that AGS FBO through the OpenGL functions returned by `SDL_GL_GetProcAddress` and use its color texture directly as the shader `Source`.

Enable it with:

```bash
export AGS_SHADER_NATIVE_SOURCE=auto
```

For AGS to create the logical render target, the normal AGS configuration must use:

```ini
[graphics]
render_at_screenres=0
```

The injector does **not** depend on a particular hard-coded game resolution. It observes the native FBO viewport and its attached texture. If the texture backing is larger than the logical image, the valid logical area is copied/cropped with a GPU framebuffer blit before the shader chain. If no suitable native AGS target is observed, the existing captured-backbuffer path remains available as fallback.

With `AGS_SHADER_DEBUG=1`, a successful native-source discovery looks like:

```text
AGS shader: native AGS source fbo=3 texture=17 logical=640x360 backing=640x360 -> output=1920x1080
```

This mode is opt-in until identity/ScaleFX/CRT render tests verify its orientation and semantics on real AGS runtimes.

## GPU source resampling fallback

`AGS_SHADER_SOURCE_SIZE=WIDTHxHEIGHT` remains available as an all-GPU manual fallback for games/runtimes where a logical AGS target cannot be observed. The injector attaches the source texture to an OpenGL read framebuffer and uses `glBlitFramebuffer` into a reusable source texture; there is no CPU readback in normal rendering.

Example:

```bash
export AGS_SHADER_SOURCE_SIZE=640x360
export AGS_SHADER_SOURCE_FILTER=nearest
```

`AGS_SHADER_SOURCE_FILTER` accepts `nearest` (default, useful for pixel art) or `linear`.

With `AGS_SHADER_DEBUG=1`, the injector also prints the active OpenGL vendor, renderer, version and GLSL version, making hardware/software rendering explicit (`radeonsi`/Radeon versus `llvmpipe`, for example).

## Per-pass framebuffer diagnostics

Set `AGS_SHADER_DUMP_PASSES_DIR` to capture the first rendered shader frame **after every pipeline pass**. This mode intentionally uses `glReadPixels` and PNG encoding and is therefore for diagnostics only; it has no cost when the variable is unset.

Example for a 12-pass CRT Royale preset:

```bash
export AGS_SHADER_DUMP_PASSES_DIR=/home/paolo/pictures/crt-royale-passes
export AGS_SHADER_DUMP_PASS_COUNT=12
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
- `AGS_SHADER_NATIVE_SOURCE=auto`: opt-in discovery/use of AGS' logical native FBO texture.
- `AGS_SHADER_SCREENSHOT_DIR`: override the F12 screenshot directory.
- `AGS_SHADER_SOURCE_SIZE=WIDTHxHEIGHT`: optional GPU-side source resample/crop before the shader pipeline.
- `AGS_SHADER_SOURCE_FILTER=nearest|linear`: filter for GPU source preparation; default is `nearest`.
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

## License

The injector and the repository are released under GNU GPL version 3 or later (`GPL-3.0-or-later`). The complete license text is in `COPYING`; RetroArch- and ScummVM-derived/adapted portions retain attribution in `THIRD_PARTY.md` and in the relevant source files.
