# AGS Shader Injector (Linux)

This is the runtime add-on for the project. It does **not** replace the AGS engine and does **not** require Wine, Proton, RetroArch or libretro.

The first backend targets AGS games using the SDL2 OpenGL renderer on Linux. The injector is loaded with `LD_PRELOAD` and intercepts `SDL_GL_SwapWindow()`, captures the current backbuffer, applies an external GLSL shader or multipass `.agschain`, then lets SDL present the final frame.

## Per-game layout

A completed game directory can contain only:

```text
game/
├── game.exe
├── libags-shader.so
├── shaders/
│   └── crt.agschain
└── run-with-shader.sh
```

The game itself is not modified.

## Example

```bash
AGS_SHADER_CHAIN="$PWD/shaders/crt.agschain" \
LD_PRELOAD="$PWD/libags-shader.so" \
./game.exe
```

`run-with-shader.sh` will wrap this command for convenience.

## Environment variables

- `AGS_SHADER_CHAIN`: absolute or relative path to a `.glsl` or `.agschain` file.
- `AGS_SHADER_DEBUG=1`: print shader loading errors and diagnostics to stderr.

## Shader interface

The runtime supplies these uniforms when present:

```glsl
uniform sampler2D uTexture;
uniform vec2 uInputSize;
uniform vec2 uOutputSize;
uniform vec2 uOriginalSize;
uniform vec2 uTexelSize;
uniform float uTime;
uniform float uFrameCount;
```

Phase 1 keeps all intermediate passes at the final drawable size. Per-pass scaling, Libretro `.glslp` compatibility, LUTs/textures and more advanced state handling are planned next.
