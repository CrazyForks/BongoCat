#!/usr/bin/env bash

set -euo pipefail

if [[ "$(uname -s)" == "Linux" ]]; then
  # SDL window lifecycle tests need an X server and an OpenGL context.
  export SDL_VIDEODRIVER=x11
  export LIBGL_ALWAYS_SOFTWARE=1
  # Avoid X_ShmPutImage BadMatch failures in the headless SDL surface path.
  exec xvfb-run --auto-servernum \
    --server-args="-screen 0 1280x1024x24 -extension MIT-SHM" "$@"
fi

exec "$@"
