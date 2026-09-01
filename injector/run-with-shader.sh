#!/bin/sh
set -eu

HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
GAME=${1:?usage: $0 /path/to/game [game args...]}
shift || true

SHADER=${AGS_SHADER_CHAIN:-"$HERE/shaders/invert.glsl"}

export AGS_SHADER_CHAIN="$SHADER"
export AGS_SHADER_DEBUG="${AGS_SHADER_DEBUG:-1}"
export LD_PRELOAD="$HERE/libags-shader.so${LD_PRELOAD:+:$LD_PRELOAD}"

exec "$GAME" "$@"
