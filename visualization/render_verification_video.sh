#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
echo "Reality Probe: rendering canonical deterministic mathematical explainer."
echo "The previous Blender/cinematic verification animation is deprecated."
exec bash "$ROOT/visualization/explainer/render_explainer.sh" "${1:-final}"
