# AGS Shader Injector (Linux)

This directory contains a **per-game native Linux post-processing add-on**. It does not replace the AGS engine and does not require Wine, Proton, RetroArch or libretro.

The current backend targets AGS games using SDL2/OpenGL on Linux. The shared library is loaded with `LD_PRELOAD`, intercepts `SDL_GL_SwapWindow()`, captures the current OpenGL backbuffer, applies an external shader pipeline, and then lets SDL present the processed frame.

## Per-game layout

```text
game/
├── game.exe
├── libags-shader.so
├── shaders/
│   ├── identity.glsl
│   ├── invert.glsl
│   └── invert-chain.agschain
└── run-with-shader.sh
```

The original game files are not modified.

## Single GLSL shader

```bash
AGS_SHADER_CHAIN="$PWD/shaders/invert.glsl" \
LD_PRELOAD="$PWD/libags-shader.so" \
./game.exe
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
./game.exe
```

## Basic Libretro GLSLP preset support

The pipeline can load a basic `.glslp` preset using:

- `shaders = N`
- `shader0 = ...`, `shader1 = ...`, ...
- `filter_linearN`
- `scaleN`, `scale_xN`, `scale_yN`
- `scale_typeN`, `scale_type_xN`, `scale_type_yN`

It also understands the common Libretro combined GLSL source form using `VERTEX` and `FRAGMENT` sections.

Advanced Libretro features such as LUT/auxiliary textures, shader parameters, frame history and feedback are not yet implemented.

## Environment variables

- `AGS_SHADER_CHAIN`: path to `.glsl`, `.agschain` or `.glslp`.
- `AGS_SHADER_DEBUG=1`: enable diagnostics on stderr.

## Standard uniforms

When the shader declares them, the injector provides:

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

The next development milestone is a real-game smoke test with a native Linux AGS title, followed by complete `.glslp` resource handling and shader-library integration.