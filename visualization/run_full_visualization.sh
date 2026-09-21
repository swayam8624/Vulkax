#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="${VULKAX_VIS_OUT:-$ROOT/build/visualization}"
TRAJ="${VULKAX_TRAJECTORY_OUT:-$ROOT/build/visualization-trajectory}"
BUILD="${VULKAX_VIS_BUILD:-$ROOT/build-vis}"
MOTION_SCALE="${VULKAX_MOTION_SCALE:-400}"
RENDER_VIDEOS=0

resolve_blender() {
  if [ -n "${BLENDER_BIN:-}" ]; then
    if [ -x "$BLENDER_BIN" ]; then
      printf '%s\n' "$BLENDER_BIN"
      return 0
    fi
    echo "error: BLENDER_BIN is set but is not executable: $BLENDER_BIN" >&2
    return 1
  fi

  if command -v blender >/dev/null 2>&1; then
    command -v blender
    return 0
  fi

  if [ "$(uname -s)" = "Darwin" ]; then
    local candidates=(
      "/Applications/Blender.app/Contents/MacOS/Blender"
      "$HOME/Applications/Blender.app/Contents/MacOS/Blender"
    )
    local candidate
    for candidate in "${candidates[@]}"; do
      if [ -x "$candidate" ]; then
        printf '%s\n' "$candidate"
        return 0
      fi
    done
  fi

  return 1
}

for arg in "$@"; do
  case "$arg" in
    --render-videos) RENDER_VIDEOS=1 ;;
    *)
      echo "usage: $0 [--render-videos]" >&2
      echo "optional environment: VULKAX_MOTION_SCALE=400 BLENDER_BIN=/path/to/Blender" >&2
      exit 2
      ;;
  esac
done

if ! BLENDER="$(resolve_blender)"; then
  echo "error: Blender could not be found." >&2
  echo "On macOS install Blender.app in /Applications or set:" >&2
  echo '  export BLENDER_BIN="/Applications/Blender.app/Contents/MacOS/Blender"' >&2
  exit 1
fi

echo "============================================================"
echo "VULKAX FULL VISUALIZATION BUILD"
echo "Blender:      $BLENDER"
echo "Motion scale: ${MOTION_SCALE}x"
echo "Figures:      $OUT"
echo "Trajectories: $TRAJ"
echo "============================================================"

echo
echo "[1/6] Paper figures + schematic Reality Inspector"
"$ROOT/visualization/render_all.sh" --blender

echo
echo "[2/6] Configure solver-replay build"
cmake -S "$ROOT" -B "$BUILD" \
  -DCMAKE_BUILD_TYPE=Release \
  -DVULKAX_BUILD_TESTS=OFF

echo
echo "[3/6] Build frozen hero trajectory exporter"
cmake --build "$BUILD" \
  --target vulkax_visualization_trajectory_probe \
  --parallel

echo
echo "[4/6] Replay exact frozen deceptive repair and export raw solver state"
"$BUILD/vulkax_visualization_trajectory_probe" "$TRAJ"

echo
echo "[5/6] Generate natural-scale and residual X-ray figures"
python3 "$ROOT/visualization/scripts/render_solver_trajectory.py" \
  --trajectory-dir "$TRAJ" \
  --out "$OUT"

python3 "$ROOT/visualization/scripts/render_solver_residual_xray.py" \
  --trajectory-dir "$TRAJ" \
  --out "$OUT"

echo
echo "[6/6] Build solver-driven Blender scenes"
for direction in px nx py pz; do
  output="$OUT/vulkax_solver_${direction}"
  echo "  -> $direction : ${output}.blend"

  args=(
    -b
    -P "$ROOT/visualization/blender/solver_trajectory_scene.py"
    --
    --trajectory-dir "$TRAJ"
    --direction "$direction"
    --motion-scale "$MOTION_SCALE"
    --output "$output"
  )
  if [ "$RENDER_VIDEOS" -eq 1 ]; then
    args+=(--render)
  fi
  "$BLENDER" "${args[@]}"
done

echo
echo "============================================================"
echo "DONE"
echo "============================================================"
echo
echo "Paper / evidence visuals:"
echo "  $OUT/fig_deceptive_repair_hero.svg"
echo "  $OUT/fig_information_frontier.svg"
echo "  $OUT/fig_evidence_story.svg"
echo "  $OUT/fig_reality_inspector_storyboard.svg"
echo "  $OUT/fig_solver_state_force_trajectories.svg"
echo "  $OUT/fig_solver_state_residual_xray.svg"
echo
echo "Blender scenes:"
echo "  $OUT/reality_inspector.blend"
echo "  $OUT/vulkax_solver_px.blend"
echo "  $OUT/vulkax_solver_nx.blend"
echo "  $OUT/vulkax_solver_py.blend"
echo "  $OUT/vulkax_solver_pz.blend"
if [ "$RENDER_VIDEOS" -eq 1 ]; then
  echo
  echo "Rendered solver videos:"
  echo "  $OUT/vulkax_solver_px.mp4"
  echo "  $OUT/vulkax_solver_nx.mp4"
  echo "  $OUT/vulkax_solver_py.mp4"
  echo "  $OUT/vulkax_solver_pz.mp4"
fi
echo
echo "Open the main scene on macOS:"
echo "  open -a Blender \"$OUT/vulkax_solver_px.blend\""
