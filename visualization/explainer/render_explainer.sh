#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"
FILM_PY="${FILM_PY:-python3}"
if ! "$FILM_PY" -c 'import numpy, PIL' >/dev/null 2>&1; then
    BUNDLED_PY="$HOME/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/bin/python3"
    if [[ -x "$BUNDLED_PY" ]] && "$BUNDLED_PY" -c 'import numpy, PIL' >/dev/null 2>&1; then
        FILM_PY="$BUNDLED_PY"
    else
        echo 'Use FILM_PY to select an existing Python with NumPy and Pillow.' >&2
        exit 1
    fi
fi
command -v ffmpeg >/dev/null
MODE="${1:-final}"
case "$MODE" in preview|final|stills|check) ;; *) echo 'Usage: render_explainer.sh [preview|final|stills|check]' >&2; exit 1;; esac
OUT=build/reality-probe-explainer
cmake -S . -B build-vis -DCMAKE_BUILD_TYPE=Release -DVULKAX_BUILD_TESTS=OFF
cmake --build build-vis --target vulkax_visualization_trajectory_probe --parallel 4
./build-vis/vulkax_visualization_trajectory_probe "$OUT/replay"
"$FILM_PY" visualization/explainer/test_explainer.py
"$FILM_PY" visualization/explainer/render.py --mode "$MODE"
cp visualization/explainer/review.html "$OUT/index.html"
cp visualization/explainer/README.md "$OUT/README.md"
cp visualization/explainer/narration.srt "$OUT/narration.srt"
