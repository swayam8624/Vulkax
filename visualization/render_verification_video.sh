#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TRAJ="${VULKAX_TRAJECTORY_OUT:-$ROOT/build/visualization-trajectory}"
OUT="${VULKAX_VIS_OUT:-$ROOT/build/paper-visuals}"
MOTION_SCALE="${VULKAX_MOTION_SCALE:-400}"
DIRECTION="${VULKAX_AESTHETIC_DIRECTION:-px}"

resolve_blender() {
  if [ -n "${BLENDER_BIN:-}" ] && [ -x "${BLENDER_BIN:-}" ]; then
    printf '%s\n' "${BLENDER_BIN:-}"
    return 0
  fi
  if command -v blender >/dev/null 2>&1; then
    command -v blender
    return 0
  fi
  for candidate in "/Applications/Blender.app/Contents/MacOS/Blender" "$HOME/Applications/Blender.app/Contents/MacOS/Blender"; do
    if [ -x "$candidate" ]; then
      printf '%s\n' "$candidate"
      return 0
    fi
  done
  return 1
}

BLENDER="$(resolve_blender)" || { echo "error: Blender not found" >&2; exit 1; }
test -s "$TRAJ/particle_trajectories.csv" || { echo "error: missing frozen trajectory replay" >&2; exit 1; }
BUNNY="$("$ROOT/visualization/assets/fetch_stanford_bunny.sh")"
test -s "$BUNNY" || { echo "error: Stanford Bunny fetch failed" >&2; exit 1; }

mkdir -p "$OUT/video"
BASE="$OUT/video/reality_probe_verification_animation"
rm -f "${BASE}.mp4" "${BASE}.blend"

"$BLENDER" -b -P "$ROOT/visualization/blender/verification_animation_scene.py" -- \
  --trajectory-dir "$TRAJ" \
  --bunny "$BUNNY" \
  --direction "$DIRECTION" \
  --motion-scale "$MOTION_SCALE" \
  --output "$BASE" \
  --render

test -s "${BASE}.blend" || { echo "error: missing ${BASE}.blend" >&2; exit 1; }
test -s "${BASE}.mp4" || { echo "error: missing ${BASE}.mp4" >&2; exit 1; }

echo "REALITY PROBE VIDEO PASS"
echo "  ${BASE}.mp4"
