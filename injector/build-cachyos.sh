#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
BUILD="$ROOT/build"

command -v cmake >/dev/null 2>&1 || { echo "cmake not found" >&2; exit 1; }
command -v pkg-config >/dev/null 2>&1 || { echo "pkg-config not found" >&2; exit 1; }
pkg-config --exists sdl2 || { echo "SDL2 development package not found" >&2; exit 1; }

cmake -S "$ROOT" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD" --parallel

printf '\nBuilt: %s\n' "$BUILD/libags-shader.so"
