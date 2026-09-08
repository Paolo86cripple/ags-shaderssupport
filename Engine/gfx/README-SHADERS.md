# Native librashader pipeline

The Linux OpenGL runtime can apply RetroArch-compatible `.slangp` presets directly inside AGS. RetroArch, libretro, Wine and LD_PRELOAD injection are not part of the final runtime architecture.

## Runtime

Set either of these before launching the native AGS runtime:

```sh
AGS_SHADER_CHAIN=/absolute/path/to/preset.slangp ./ags game.exe
```

or, for compatibility with the earlier prototype:

```sh
AGS_SHADER=/absolute/path/to/preset.slangp ./ags game.exe
```

`AGS_SHADER_CHAIN` takes precedence.

The engine dynamically loads `librashader.so` at runtime and currently expects librashader C ABI 2. The library is not linked into AGS and is not required when no external shader preset is selected.

The OpenGL backend requires an OpenGL 3.3+ context. If the active context is older, AGS logs a shader-load warning and continues without the external shader chain.

## Rendering path

1. AGS renders the game normally with its native OpenGL renderer.
2. The completed backbuffer is copied into a caller-owned RGBA8 texture.
3. librashader evaluates the `.slangp` filter chain into a second caller-owned texture.
4. The result is blitted back to the AGS framebuffer immediately before `SDL_GL_SwapWindow`.

The launcher is expected to store the selected preset in the per-game profile and export the environment variable when starting the shared AGS runtime.

## Prototype formats

The previous custom `.glslp`, `.agschain` and raw-GLSL parser/compiler were development scaffolding. The librashader backend intentionally replaces them with standard `.slangp` presets so AGS does not maintain its own RetroArch preset implementation.
