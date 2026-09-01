# Native external shader pipeline

This repository's shader work targets per-game runtime post-processing. It does not require RetroArch/libretro and does not require Wine.

The runtime-side implementation is designed to load external GLSL shaders and, in the next phase, Libretro-style GLSL preset chains without modifying the game data.
