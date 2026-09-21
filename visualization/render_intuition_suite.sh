#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="${VULKAX_VIS_OUT:-$ROOT/build/paper-visuals}"
TRAJ="${VULKAX_TRAJECTORY_OUT:-$ROOT/build/visualization-trajectory}"
MOTION_SCALE="${VULKAX_MOTION_SCALE:-400}"
BLENDER="${BLENDER_BIN:-/Applications/Blender.app/Contents/MacOS/Blender}"

if [ ! -x "$BLENDER" ]; then
  echo "error: Blender not found at $BLENDER" >&2
  exit 1
fi
if [ ! -s "$TRAJ/particle_trajectories.csv" ]; then
  echo "error: missing solver trajectories; run the frozen replay first" >&2
  exit 1
fi

BUNNY="$("$ROOT/visualization/assets/fetch_stanford_bunny.sh")"
mkdir -p "$OUT/intuition" "$OUT/vector"

for panel in brightfield texture_truth texture_repair darkfield; do
  "$BLENDER" -b -P "$ROOT/visualization/blender/paper_plate_scene.py" -- \
    --trajectory-dir "$TRAJ" \
    --bunny "$BUNNY" \
    --panel "$panel" \
    --direction px \
    --motion-scale "$MOTION_SCALE" \
    --engine eevee \
    --output "$OUT/intuition/$panel" \
    --render-still
done

python3 "$ROOT/visualization/scripts/compose_darkfield_intuition_svg.py" \
  --repo-root "$ROOT" \
  --brightfield "$OUT/intuition/brightfield.png" \
  --truth "$OUT/intuition/texture_truth.png" \
  --repair "$OUT/intuition/texture_repair.png" \
  --darkfield "$OUT/intuition/darkfield.png" \
  --out "$OUT/vector/reality_probe_darkfield_intuition.svg"

for d in px nx py pz; do
  "$BLENDER" -b -P "$ROOT/visualization/blender/paper_plate_scene.py" -- \
    --trajectory-dir "$TRAJ" \
    --bunny "$BUNNY" \
    --panel xray \
    --direction "$d" \
    --motion-scale "$MOTION_SCALE" \
    --engine eevee \
    --output "$OUT/intuition/xray_$d" \
    --render-still
done

python3 "$ROOT/visualization/scripts/compose_fingerprints_svg.py" \
  --summary "$TRAJ/direction_summary.csv" \
  --px "$OUT/intuition/xray_px.png" \
  --nx "$OUT/intuition/xray_nx.png" \
  --py "$OUT/intuition/xray_py.png" \
  --pz "$OUT/intuition/xray_pz.png" \
  --out "$OUT/vector/reality_probe_mechanism_fingerprints.svg"

echo "INTUITION SUITE PASS"
echo "  $OUT/vector/reality_probe_darkfield_intuition.svg"
echo "  $OUT/vector/reality_probe_mechanism_fingerprints.svg"
