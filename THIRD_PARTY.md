# Third-party notices

## RetroArch

Parts of the OpenGL/GLSL renderchain compatibility and optimization work in
this repository are adapted from RetroArch.

Upstream project: RetroArch
Repository: https://github.com/libretro/RetroArch
License: GNU General Public License version 3 or later (GPL-3.0-or-later)

Relevant upstream implementation areas include:

- `gfx/drivers/gl2.c`
- `gfx/drivers_shader/shader_glsl.c`

Original RetroArch copyright notices include:

- Copyright (C) 2010-2014 Hans-Kristian Arntzen
- Copyright (C) 2011-2017 Daniel De Matteis
- Copyright (C) 2012-2015 Michael Lelli
- Copyright (C) 2016-2019 Brad Parker

The AGS integration, SDL preload hook, per-game injector, diagnostics and
adaptations in this repository are modified work and are not endorsed by the
RetroArch project.

All RetroArch-derived/adapted code remains licensed under GPL-3.0-or-later.

## ScummVM

Host-side OpenGL execution and Libretro-pipeline optimizations are also studied
and, where noted in source files, adapted from ScummVM. ScummVM is used as a
reference for efficiently integrating classic Libretro GLSL pipelines with
adventure-game renderers; the injector does not link to, load, or require
ScummVM at runtime.

Upstream project: ScummVM
Repository: https://github.com/scummvm/scummvm
License: GNU General Public License version 3 or later (GPL-3.0-or-later)

Relevant upstream implementation areas include:

- `backends/graphics/opengl/pipelines/libretro.cpp`
- `backends/graphics/opengl/pipelines/libretro.h`
- `graphics/opengl/shader.cpp`
- `backends/graphics/opengl/opengl-graphics.cpp`
- `engines/ags/engine/gfx/ali_3d_scummvm.cpp`

ScummVM is the legal property of its developers; please refer to ScummVM's
`COPYRIGHT` file for its complete copyright list.

The standalone AGS injector, native AGS runtime integration, compatibility
extensions and project-specific modifications in this repository are not
endorsed by the ScummVM project.
