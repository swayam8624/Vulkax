#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="${VULKAX_VIS_OUT:-$ROOT/build/visualization}"
TRAJ="${VULKAX_TRAJECTORY_OUT:-$ROOT/build/visualization-trajectory}"
BUILD="${VULKAX_VIS_BUILD:-$ROOT/build-vis}"
MOTION_SCALE="${VULKAX_MOTION_SCALE:-400}"
AESTHETIC_DIRECTION="${VULKAX_AESTHETIC_DIRECTION:-px}"
AESTHETIC_ENGINE="${VULKAX_AESTHETIC_ENGINE:-eevee}"
RENDER_VIDEOS=0

resolve_blender() {
  if [ -n "${BLENDER_BIN:-}" ]; then
    [ -x "$BLENDER_BIN" ] || { echo "error: BLENDER_BIN is not executable: $BLENDER_BIN" >&2; return 1; }
    printf '%s\n' "$BLENDER_BIN"; return 0
  fi
  if command -v blender >/dev/null 2>&1; then command -v blender; return 0; fi
  if [ "$(uname -s)" = "Darwin" ]; then
    for candidate in "/Applications/Blender.app/Contents/MacOS/Blender" "$HOME/Applications/Blender.app/Contents/MacOS/Blender"; do
      if [ -x "$candidate" ]; then printf '%s\n' "$candidate"; return 0; fi
    done
  fi
  return 1
}

for arg in "$@"; do
  case "$arg" in
    --render-videos) RENDER_VIDEOS=1 ;;
    *)
      echo "usage: $0 [--render-videos]" >&2
      exit 2
      ;;
  esac
done

if ! BLENDER="$(resolve_blender)"; then
  echo "error: Blender could not be found." >&2
  exit 1
fi

echo "============================================================"
echo "VULKAX PAPER VISUALIZATION BUILD"
echo "Blender:            $BLENDER"
echo "Motion scale:       ${MOTION_SCALE}x"
echo "Aesthetic direction:$AESTHETIC_DIRECTION"
echo "Aesthetic engine:   $AESTHETIC_ENGINE"
echo "============================================================"

echo
echo "[1/8] Frozen paper/evidence figures"
"$ROOT/visualization/render_all.sh" --blender

echo
echo "[2/8] Configure solver-replay build"
cmake -S "$ROOT" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release -DVULKAX_BUILD_TESTS=OFF

echo
echo "[3/8] Build exact frozen hero replay target"
cmake --build "$BUILD" --target vulkax_visualization_trajectory_probe --parallel

echo
echo "[4/8] Replay exact APIC -> PIC deceptive case"
"$BUILD/vulkax_visualization_trajectory_probe" "$TRAJ"

echo
echo "[5/8] Scientific natural-scale + mechanism X-ray figures"
python3 "$ROOT/visualization/scripts/render_solver_trajectory.py" --trajectory-dir "$TRAJ" --out "$OUT"
python3 "$ROOT/visualization/scripts/render_solver_residual_xray.py" --trajectory-dir "$TRAJ" --out "$OUT"

echo
echo "[6/8] Fetch publication-licensed Stanford Bunny visualization carrier"
BUNNY="$("$ROOT/visualization/assets/fetch_stanford_bunny.sh")"
echo "Bunny mesh: $BUNNY"

echo
echo "[7/8] Build SIGGRAPH-style solver-driven Stanford Bunny hero"
hero_args=(
  -b
  -P "$ROOT/visualization/blender/aesthetic_bunny_scene.py"
  --
  --trajectory-dir "$TRAJ"
  --bunny "$BUNNY"
  --direction "$AESTHETIC_DIRECTION"
  --motion-scale "$MOTION_SCALE"
  --engine "$AESTHETIC_ENGINE"
  --output "$OUT/vulkax_bunny_hero_${AESTHETIC_DIRECTION}"
  --render-still
)
if [ "$RENDER_VIDEOS" -eq 1 ]; then hero_args+=(--render-animation); fi
hero_base="$OUT/vulkax_bunny_hero_${AESTHETIC_DIRECTION}"
rm -f "${hero_base}.blend" "${hero_base}.png" "${hero_base}.mp4"
"$BLENDER" "${hero_args[@]}"
test -s "${hero_base}.blend" || { echo "error: aesthetic hero .blend was not produced" >&2; exit 1; }
test -s "${hero_base}.png" || { echo "error: aesthetic hero .png was not produced" >&2; exit 1; }
if [ "$RENDER_VIDEOS" -eq 1 ]; then
  test -s "${hero_base}.mp4" || { echo "error: aesthetic hero .mp4 was not produced" >&2; exit 1; }
fi

echo
echo "[8/8] Build compact raw-solver Blender scenes"
for direction in px nx py pz; do
  output="$OUT/vulkax_solver_${direction}"
  args=(
    -b
    -P "$ROOT/visualization/blender/solver_trajectory_scene.py"
    --
    --trajectory-dir "$TRAJ"
    --direction "$direction"
    --motion-scale "$MOTION_SCALE"
    --output "$output"
  )
  if [ "$RENDER_VIDEOS" -eq 1 ]; then args+=(--render); fi
  rm -f "${output}.blend" "${output}.mp4"
  "$BLENDER" "${args[@]}"
  test -s "${output}.blend" || { echo "error: missing solver scene ${output}.blend" >&2; exit 1; }
  if [ "$RENDER_VIDEOS" -eq 1 ]; then
    test -s "${output}.mp4" || { echo "error: missing solver video ${output}.mp4" >&2; exit 1; }
  fi
done

echo
echo "============================================================"
echo "DONE — PRIMARY PAPER VISUAL"
echo "============================================================"
echo "  $OUT/vulkax_bunny_hero_${AESTHETIC_DIRECTION}.png"
echo "  $OUT/vulkax_bunny_hero_${AESTHETIC_DIRECTION}.blend"
if [ "$RENDER_VIDEOS" -eq 1 ]; then
  echo "  $OUT/vulkax_bunny_hero_${AESTHETIC_DIRECTION}.mp4"
fi
echo
echo "Open hero:"
echo "  open '$OUT/vulkax_bunny_hero_${AESTHETIC_DIRECTION}.png'"
echo "  open -a Blender '$OUT/vulkax_bunny_hero_${AESTHETIC_DIRECTION}.blend'"
