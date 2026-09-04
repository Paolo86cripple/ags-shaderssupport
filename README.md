# AGS Shader Support

Native Linux runtime shader injection for Adventure Game Studio games, with
classic Libretro/RetroArch GLSL and GLSLP compatibility.

The project is runtime-only: AGS game data does not need to be modified and a
Windows-distributed AGS game may be run by a native Linux AGS runtime using its
`.ags` data file. Wine/Proton is not required for the shader injector.

## Current architecture

```text
AGS native Linux runtime
        |
        | SDL2 / OpenGL
        v
LD_PRELOAD injector
        |
        +-- AGS framebuffer capture
        +-- optional hardware source resample
        +-- RetroArch-style GL2/GLSL renderchain
        |     +-- GLSL / GLSLP passes
        |     +-- aliases and parameters
        |     +-- LUTs
        |     +-- history / feedback
        |     +-- float / sRGB FBOs
        |     +-- mipmaps / wrap / filtering
        |     +-- RetroArch FBO image-vs-texture sizing
        |     +-- cached uniforms / texture state
        |
        +-- F12 pixel-perfect framebuffer screenshots
        +-- optional per-pass framebuffer diagnostics
        v
AGS backbuffer / display
```

The active implementation lives under `injector/`.

## Libretro GLSL compatibility

CI pins a known `libretro/glsl-shaders` revision and strictly validates the
classic GLSL ecosystem. At the current checkpoint the loader passes every
usable pinned GLSLP preset and every pinned standalone GLSL source; one preset
with an obsolete path in the pinned upstream tree is tracked separately as an
upstream XFAIL.

Compile/load compatibility is only one layer. Real AGS render tests are also
used to validate framebuffer semantics and performance.

## RetroArch-derived renderchain

The OpenGL/GLSL compatibility layer is being aligned directly with RetroArch's
GL2 renderchain and GLSL backend. The adapted code preserves upstream copyright
notices and focuses on the desktop OpenGL behavior needed by AGS, rather than
copying unrelated RetroArch frontend, menu, console or GLES code.

Important imported semantics include:

- separate FBO image size and texture backing size;
- power-of-two backing textures used by the classic GL2 renderchain;
- matching `InputSize`, `TextureSize` and texture-coordinate ratios;
- float/sRGB framebuffer behavior;
- cached uniform lookups and texture state;
- classic RetroArch global uniforms and frame references.

See `THIRD_PARTY.md` for attribution.

## Performance

RetroArch normally receives a core's native content resolution. AGS may already
have scaled its content to the desktop backbuffer before the injector sees it.
For scale-heavy presets such as ScaleFX this can create unnecessarily huge
intermediate targets.

`AGS_SHADER_SOURCE_SIZE=WxH` performs an optional source resample entirely on
the GPU before the shader chain. This is intended to converge toward automatic
use of the AGS game's logical/native resolution.

## Injector documentation

See [`injector/README.md`](injector/README.md) for environment variables,
screenshots, diagnostics and launch examples.

## License

AGS Shader Support is free software released under the GNU General Public
License version 3 or later (`GPL-3.0-or-later`). The complete GPLv3 text is in
`COPYING`.

Portions adapted from RetroArch remain GPL-3.0-or-later and retain attribution
in source headers and `THIRD_PARTY.md`.
