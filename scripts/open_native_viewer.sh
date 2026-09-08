#!/usr/bin/env bash
set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT" || exit 1

fail() {
  echo
  echo "============================================================"
  echo "VULKAX NATIVE VIEWER FAILED: $1"
  echo "============================================================"
  return 1
}

if [[ "$(uname -s)" != "Darwin" ]]; then
  fail "The native Metal viewer currently requires macOS."
  exit 1
fi

RUN_DIR="${1:-build/captured-world-run}"
PARTICLES="${2:-build/captured-example/particles.csv}"

if [[ ! -f "$RUN_DIR/appearance/before.ply" ]]; then
  fail "Missing $RUN_DIR/appearance/before.ply. Generate a captured-world run first."
  exit 1
fi

if [[ ! -f "$RUN_DIR/appearance/rewritten.ply" ]]; then
  fail "Missing $RUN_DIR/appearance/rewritten.ply."
  exit 1
fi

echo "Configuring Vulkax native viewer..."
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DVULKAX_BUILD_TESTS=ON || {
  fail "CMake configure"
  exit 1
}

echo "Building Vulkax native viewer..."
cmake --build build --target vulkax_viewer --parallel "$(sysctl -n hw.logicalcpu)" || {
  fail "Native viewer build"
  exit 1
}

echo
echo "Launching native Metal viewer"
echo "  run:       $RUN_DIR"
if [[ -f "$PARTICLES" ]]; then
  echo "  particles: $PARTICLES"
  exec ./build/vulkax_viewer --run "$RUN_DIR" --particles "$PARTICLES"
else
  echo "  particles: not found (Gaussian-only viewer will still open)"
  exec ./build/vulkax_viewer --run "$RUN_DIR"
fi
