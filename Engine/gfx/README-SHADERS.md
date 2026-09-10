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

`AGS_SHADER_CHAIN` takes precedence. AGS passes the selected preset path directly to librashader; preset parsing, validation, compilation, multipass behavior, LUTs, history/feedback and shader parameters are librashader responsibilities.

The engine dynamically loads the host-installed librashader C ABI 2 at runtime. It prefers the canonical `librashader.so.2` SONAME and then accepts the unversioned `librashader.so` only when it reports ABI 2. librashader is not linked or bundled into AGS, so ABI-compatible host/package-manager updates are picked up without rebuilding AGS. If the host installs only an incompatible future ABI, AGS continues without the external shader pipeline.

The OpenGL backend requires an OpenGL 3.3+ context. If the active context is older, AGS logs a shader-load warning and continues without the external shader chain.

## Rendering path

1. AGS renders the game normally with its native OpenGL renderer.
2. The completed backbuffer is copied into a caller-owned RGBA8 texture.
3. librashader evaluates the `.slangp` filter chain into a second caller-owned texture.
4. The result is blitted back to the AGS framebuffer immediately before `SDL_GL_SwapWindow`.

The launcher is expected to store the selected preset in the per-game profile and export the environment variable when starting the shared AGS runtime.

## Prototype formats

The previous custom `.glslp`, `.agschain` and raw-GLSL parser/compiler were development scaffolding. The librashader backend removes that shading logic from AGS entirely. The launcher may normally select `.slangp` presets, but the engine itself does not enforce a shader file extension: it delegates format support and validation to the installed librashader.
