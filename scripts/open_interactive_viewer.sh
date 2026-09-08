#!/usr/bin/env bash

# Build and open the Vulkax interactive viewer without changing scientific output.
# Intentionally avoids `set -e` so a failed viewer build does not close an
# interactive user's shell.

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
RUN_DIR="${1:-$ROOT/build/captured-world-run}"
PARTICLES="${2:-$ROOT/build/captured-example/particles.csv}"
VIEWER="$RUN_DIR/render/interactive/viewer.html"

fail() {
  printf '\n[Vulkax viewer] FAILED: %s\n' "$1" >&2
  printf '[Vulkax viewer] Shell remains open.\n\n' >&2
  return 1
}

if [ ! -f "$RUN_DIR/appearance/before.ply" ] || [ ! -f "$RUN_DIR/appearance/rewritten.ply" ]; then
  fail "captured-world run is missing appearance/before.ply or appearance/rewritten.ply"
  exit 1
fi

ARGS=("$ROOT/scripts/build_interactive_viewer_app.py" "$RUN_DIR")
if [ -f "$PARTICLES" ]; then
  ARGS+=(--particles-csv "$PARTICLES")
else
  printf '[Vulkax viewer] Particle CSV not found; surface/particle modes may be unavailable: %s\n' "$PARTICLES"
fi

python3 "${ARGS[@]}" || { fail "viewer generation failed"; exit 1; }

printf '\n[Vulkax viewer] Ready: %s\n' "$VIEWER"

case "$(uname -s)" in
  Darwin) open "$VIEWER" ;;
  Linux) command -v xdg-open >/dev/null 2>&1 && xdg-open "$VIEWER" >/dev/null 2>&1 & ;;
  *) printf '[Vulkax viewer] Open the HTML file above in a WebGL2-capable browser.\n' ;;
esac

exit 0
