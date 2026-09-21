#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="${VULKAX_VIS_OUT:-$ROOT/build/visualization}"
TRAJ="${VULKAX_TRAJECTORY_OUT:-$ROOT/build/visualization-trajectory}"
MOTION_SCALE="${VULKAX_MOTION_SCALE:-400}"
DIRECTION="${VULKAX_AESTHETIC_DIRECTION:-px}"
ENGINE="${VULKAX_AESTHETIC_ENGINE:-eevee}"
SAMPLES="${VULKAX_CYCLES_SAMPLES:-128}"
VIDEO=0

resolve_blender() {
  if [ -n "${BLENDER_BIN:-}" ] && [ -x "$BLENDER_BIN" ]; then printf '%s\n' "$BLENDER_BIN"; return 0; fi
  if command -v blender >/dev/null 2>&1; then command -v blender; return 0; fi
  for candidate in "/Applications/Blender.app/Contents/MacOS/Blender" "$HOME/Applications/Blender.app/Contents/MacOS/Blender"; do
    if [ -x "$candidate" ]; then printf '%s\n' "$candidate"; return 0; fi
  done
  return 1
}

for arg in "$@"; do
  case "$arg" in
    --video) VIDEO=1 ;;
    --cycles) ENGINE="cycles" ;;
    --eevee) ENGINE="eevee" ;;
    *) echo "usage: $0 [--cycles|--eevee] [--video]" >&2; exit 2 ;;
  esac
done

BLENDER="$(resolve_blender)" || { echo "error: Blender not found" >&2; exit 1; }
test -s "$TRAJ/particle_trajectories.csv" || {
  echo "error: no solver trajectories found; run ./visualization/run_full_visualization.sh once first" >&2
  exit 1
}
BUNNY="$("$ROOT/visualization/assets/fetch_stanford_bunny.sh")"
test -s "$BUNNY" || { echo "error: bunny mesh not found: $BUNNY" >&2; exit 1; }

BASE="$OUT/vulkax_bunny_hero_${DIRECTION}"
rm -f "${BASE}.blend" "${BASE}.png" "${BASE}.mp4"

args=(
  -b
  -P "$ROOT/visualization/blender/aesthetic_bunny_scene.py"
  --
  --trajectory-dir "$TRAJ"
  --bunny "$BUNNY"
  --direction "$DIRECTION"
  --motion-scale "$MOTION_SCALE"
  --engine "$ENGINE"
  --cycles-samples "$SAMPLES"
  --output "$BASE"
  --render-still
)
if [ "$VIDEO" -eq 1 ]; then args+=(--render-animation); fi

echo "Rendering VULKAX bunny hero: engine=$ENGINE direction=$DIRECTION motion=${MOTION_SCALE}x"
"$BLENDER" "${args[@]}"

test -s "${BASE}.blend" || { echo "error: missing ${BASE}.blend" >&2; exit 1; }
test -s "${BASE}.png" || { echo "error: missing ${BASE}.png" >&2; exit 1; }
if [ "$VIDEO" -eq 1 ]; then
  test -s "${BASE}.mp4" || { echo "error: missing ${BASE}.mp4" >&2; exit 1; }
fi

echo "PASS"
echo "  ${BASE}.png"
echo "  ${BASE}.blend"
[ "$VIDEO" -eq 0 ] || echo "  ${BASE}.mp4"
