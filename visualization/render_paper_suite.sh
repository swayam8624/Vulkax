#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="${VULKAX_VIS_OUT:-$ROOT/build/paper-visuals}"
TRAJ="${VULKAX_TRAJECTORY_OUT:-$ROOT/build/visualization-trajectory}"
BUILD="${VULKAX_VIS_BUILD:-$ROOT/build-vis}"
MOTION_SCALE="${VULKAX_MOTION_SCALE:-400}"
ENGINE="${VULKAX_AESTHETIC_ENGINE:-eevee}"
SAMPLES="${VULKAX_CYCLES_SAMPLES:-128}"
DIRECTION="${VULKAX_AESTHETIC_DIRECTION:-px}"
VIDEO=0

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

for arg in "$@"; do
  case "$arg" in
    --video) VIDEO=1 ;;
    --cycles) ENGINE=cycles ;;
    --eevee) ENGINE=eevee ;;
    *)
      echo "usage: $0 [--eevee|--cycles] [--video]" >&2
      exit 2
      ;;
  esac
done

BLENDER="$(resolve_blender)" || { echo "error: Blender not found" >&2; exit 1; }
mkdir -p "$OUT/plates" "$OUT/vector" "$OUT/video"

if [ ! -s "$TRAJ/particle_trajectories.csv" ]; then
  echo "[prep] frozen solver trajectory missing; building exact replay"
  cmake -S "$ROOT" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release -DVULKAX_BUILD_TESTS=OFF
  cmake --build "$BUILD" --target vulkax_visualization_trajectory_probe --parallel
  "$BUILD/vulkax_visualization_trajectory_probe" "$TRAJ"
fi

BUNNY="$("$ROOT/visualization/assets/fetch_stanford_bunny.sh")"
test -s "$BUNNY" || { echo "error: Stanford Bunny fetch failed" >&2; exit 1; }

for panel in observe repair interrogate xray wireframe; do
  base="$OUT/plates/$panel"
  rm -f "${base}.png" "${base}.blend"
  echo "[plate] $panel"
  "$BLENDER" -b -P "$ROOT/visualization/blender/paper_plate_scene.py" -- \
    --trajectory-dir "$TRAJ" \
    --bunny "$BUNNY" \
    --panel "$panel" \
    --direction "$DIRECTION" \
    --motion-scale "$MOTION_SCALE" \
    --engine "$ENGINE" \
    --cycles-samples "$SAMPLES" \
    --output "$base" \
    --render-still
  test -s "${base}.png" || { echo "error: missing ${base}.png" >&2; exit 1; }
done

echo "[vector] hero composite"
python3 "$ROOT/visualization/scripts/compose_hero_svg.py" \
  --repo-root "$ROOT" \
  --observe "$OUT/plates/observe.png" \
  --repair "$OUT/plates/repair.png" \
  --interrogate "$OUT/plates/interrogate.png" \
  --motion-scale "$MOTION_SCALE" \
  --out "$OUT/vector/reality_probe_hero.svg"

echo "[vector] mechanism X-ray"
python3 "$ROOT/visualization/scripts/compose_xray_svg.py" \
  --repo-root "$ROOT" \
  --xray "$OUT/plates/xray.png" \
  --motion-scale "$MOTION_SCALE" \
  --out "$OUT/vector/reality_probe_mechanism_darkfield.svg"

echo "[vector] 2D/3D method explainer"
python3 "$ROOT/visualization/scripts/compose_explainer_svg.py" \
  --repo-root "$ROOT" \
  --observe "$OUT/plates/wireframe.png" \
  --repair "$OUT/plates/repair.png" \
  --xray "$OUT/plates/xray.png" \
  --out "$OUT/vector/reality_probe_method_explainer.svg"

for svg in reality_probe_hero reality_probe_mechanism_darkfield reality_probe_method_explainer; do
  python3 "$ROOT/visualization/scripts/embed_svg_assets.py" \
    --svg "$OUT/vector/$svg.svg" \
    --out "$OUT/vector/${svg}_standalone.svg"
done

echo "[video] build verification animation scene"
video_args=(
  -b
  -P "$ROOT/visualization/blender/verification_animation_scene.py"
  --
  --trajectory-dir "$TRAJ"
  --bunny "$BUNNY"
  --direction "$DIRECTION"
  --motion-scale "$MOTION_SCALE"
  --output "$OUT/video/reality_probe_verification_animation"
)
if [ "$VIDEO" -eq 1 ]; then
  video_args+=(--render)
fi
"$BLENDER" "${video_args[@]}"
test -s "$OUT/video/reality_probe_verification_animation.blend" || { echo "error: missing animation blend" >&2; exit 1; }
if [ "$VIDEO" -eq 1 ]; then
  test -s "$OUT/video/reality_probe_verification_animation.mp4" || { echo "error: missing animation mp4" >&2; exit 1; }
fi

echo
echo "PAPER VISUAL SUITE PASS"
echo "Plates:      $OUT/plates/"
echo "Illustrator: $OUT/vector/reality_probe_hero.svg"
echo "             $OUT/vector/reality_probe_mechanism_darkfield.svg"
echo "             $OUT/vector/reality_probe_method_explainer.svg"
echo "Animation:   $OUT/video/reality_probe_verification_animation.blend"
if [ "$VIDEO" -eq 1 ]; then
  echo "             $OUT/video/reality_probe_verification_animation.mp4"
fi
