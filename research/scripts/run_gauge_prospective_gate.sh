#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"

SPLIT="development"
PROFILE="truth"
MODE="pilot"
BUILD_DIR="build"
GAUGE_ROOT=""
OUT=""
FINAL_LOCK=""
MAX_WORLDS=""

usage() {
  cat <<'EOF'
Usage: bash research/scripts/run_gauge_prospective_gate.sh [options]

Options:
  --split development|validation|final_test
  --profile truth|dose
  --mode pilot|definitive
  --build-dir DIR
  --gauge-root DIR
  --out DIR
  --final-lock JSON      Required for final_test
  --max-worlds N         Debug/smoke limit only
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --split) SPLIT="$2"; shift 2 ;;
    --profile) PROFILE="$2"; shift 2 ;;
    --mode) MODE="$2"; shift 2 ;;
    --build-dir) BUILD_DIR="$2"; shift 2 ;;
    --gauge-root) GAUGE_ROOT="$2"; shift 2 ;;
    --out) OUT="$2"; shift 2 ;;
    --final-lock) FINAL_LOCK="$2"; shift 2 ;;
    --max-worlds) MAX_WORLDS="$2"; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *) echo "unknown option: $1" >&2; usage >&2; exit 2 ;;
  esac
done

GAUGE_ROOT="${GAUGE_ROOT:-$BUILD_DIR/gauge-prospective-download}"
OUT="${OUT:-$BUILD_DIR/gauge-prospective-gate}"

case "$SPLIT" in
  development|validation|final_test) ;;
  *) echo "invalid split: $SPLIT" >&2; exit 2 ;;
esac
case "$PROFILE" in
  truth|dose) ;;
  *) echo "invalid profile: $PROFILE" >&2; exit 2 ;;
esac
case "$MODE" in
  pilot|definitive) ;;
  *) echo "invalid mode: $MODE" >&2; exit 2 ;;
esac

if [[ "$SPLIT" == "final_test" && -z "$FINAL_LOCK" ]]; then
  echo "final_test requires --final-lock" >&2
  exit 2
fi

python3 -m py_compile   research/analysis/gauge_prospective_gate.py   research/analysis/normalize_publication_validation.py   research/analysis/analyze_publication_validation.py
python3 research/analysis/gauge_prospective_gate.py --self-test

if [[ ! -s "$GAUGE_ROOT/data/foam stretching/soft/1.json" ]]; then
  echo "[GAUGE prospective] downloading 60-trial measured foam subset"
  python3 research/scripts/fetch_gauge_foam_subset.py "$GAUGE_ROOT"
else
  echo "[GAUGE prospective] reusing measured subset at $GAUGE_ROOT"
fi

cmake -S . -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release -DVULKAX_BUILD_TESTS=ON
cmake --build "$BUILD_DIR" --parallel --target vulkax_gauge_shearing_forward_probe

RUN_ARGS=(
  --root "$GAUGE_ROOT"
  --exe "$BUILD_DIR/vulkax_gauge_shearing_forward_probe"
  --out "$OUT"
  --split "$SPLIT"
  --profile "$PROFILE"
  --mode "$MODE"
)
if [[ -n "$FINAL_LOCK" ]]; then
  RUN_ARGS+=(--final-lock "$FINAL_LOCK")
fi
if [[ -n "$MAX_WORLDS" ]]; then
  RUN_ARGS+=(--max-worlds "$MAX_WORLDS")
fi

python3 research/analysis/gauge_prospective_gate.py "${RUN_ARGS[@]}"

RAW="$OUT/records_${SPLIT}_${PROFILE}_${MODE}.csv"
NORMALIZED="$OUT/records_${SPLIT}_${PROFILE}_${MODE}_normalized.csv"
ANALYSIS="$OUT/analysis_${SPLIT}_${PROFILE}_${MODE}"

python3 research/analysis/normalize_publication_validation.py   --extra "$RAW"   --out "$NORMALIZED"

python3 research/analysis/analyze_publication_validation.py   --records "$NORMALIZED"   --protocol research/validation/protocol_v1.json   --out "$ANALYSIS"

echo
echo "=== GAUGE prospective gate complete ==="
echo "raw records: $RAW"
echo "normalized records: $NORMALIZED"
echo "analysis: $ANALYSIS"
