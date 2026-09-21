#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="${VULKAX_VIS_OUT:-$ROOT/build/visualization}"
WITH_BLENDER=0

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
    --blender) WITH_BLENDER=1 ;;
    *)
      echo "usage: $0 [--blender]" >&2
      exit 2
      ;;
  esac
done

python3 "$ROOT/visualization/scripts/verify_frozen_evidence.py"
python3 "$ROOT/visualization/scripts/render_paper_visuals.py" --out "$OUT"
python3 "$ROOT/visualization/scripts/render_hero_case.py" --out "$OUT"

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
  if ! BLENDER="$(resolve_blender)"; then
    echo "error: --blender requested but Blender could not be found." >&2
    echo "Set BLENDER_BIN=/full/path/to/Blender or install Blender in /Applications." >&2
    exit 1
  fi

  echo "Using Blender: $BLENDER"
  "$BLENDER" -b -P "$ROOT/visualization/blender/reality_inspector_scene.py" -- \
    --repo-root "$ROOT" \
    --output "$OUT/reality_inspector"
fi

echo
echo "VULKAX visualization package complete:"
echo "  $OUT"
