#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="${VULKAX_VIS_OUT:-$ROOT/build/visualization}"
WITH_BLENDER=0

for arg in "$@"; do
  case "$arg" in
    --blender) WITH_BLENDER=1 ;;
    *)
      echo "usage: $0 [--blender]" >&2
      exit 2
      ;;
  esac
done

python3 "$ROOT/visualization/scripts/verify_frozen_evidence.py"
python3 "$ROOT/visualization/scripts/render_paper_visuals.py" --out "$OUT"

if command -v magick >/dev/null 2>&1; then
  for svg in "$OUT"/*.svg; do
    [ -e "$svg" ] || continue
    magick "$svg" "${svg%.svg}.png"
  done
elif command -v rsvg-convert >/dev/null 2>&1; then
  for svg in "$OUT"/*.svg; do
    [ -e "$svg" ] || continue
    rsvg-convert "$svg" -o "${svg%.svg}.png"
  done
fi

if [ "$WITH_BLENDER" -eq 1 ]; then
  if ! command -v blender >/dev/null 2>&1; then
    echo "error: --blender requested but Blender is not on PATH" >&2
    exit 1
  fi
  blender -b -P "$ROOT/visualization/blender/reality_inspector_scene.py" -- \
    --repo-root "$ROOT" \
    --output "$OUT/reality_inspector"
fi

echo
echo "VULKAX visualization package complete:"
echo "  $OUT"
