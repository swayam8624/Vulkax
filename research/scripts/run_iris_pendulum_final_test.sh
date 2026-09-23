#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"

BUILD_DIR="build"
VENV=""
LOCK=""
REPRODUCE=0

usage() {
  cat <<'EOF'
Usage: bash research/scripts/run_iris_pendulum_final_test.sh [options]

Options:
  --build-dir DIR   Build root (default: build)
  --venv DIR        Python venv (default: BUILD/.venv-iris-pendulum)
  --lock FILE       Existing IRIS final-test lock. If omitted, create one first.
  --reproduce       Allow deterministic rerun with the same valid lock if final output exists.
  -h, --help        Show help

This command:
1. freezes/checks the exact IRIS final-test method before opening final videos;
2. downloads only pendulum_90 takes at the pinned dataset revision;
3. refreshes manifests/adapters;
4. runs the frozen final-test method;
5. writes a descriptive summary;
6. combines final records with the publication-validation analysis.

No threshold, tracker, candidate schedule, or exclusion retuning is performed.
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --build-dir) BUILD_DIR="$2"; shift 2 ;;
    --venv) VENV="$2"; shift 2 ;;
    --lock) LOCK="$2"; shift 2 ;;
    --reproduce) REPRODUCE=1; shift ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown option: $1" >&2; usage >&2; exit 2 ;;
  esac
done

VENV="${VENV:-$BUILD_DIR/.venv-iris-pendulum}"
LOCK="${LOCK:-$BUILD_DIR/publication-validation/iris-pendulum-final-lock.json}"
DATA_ROOT="$BUILD_DIR/public-datasets"
PUBLIC_ROOT="$BUILD_DIR/publication-validation/public-data"
FINAL_OUT="$BUILD_DIR/publication-validation/iris-pendulum-final-test"
COMBINED="$BUILD_DIR/publication-validation/combined-iris-final"

if [[ ! -x "$VENV/bin/python" ]]; then
  echo "[iris-final] creating Python environment: $VENV"
  python3 -m venv "$VENV"
fi

echo "[iris-final] installing/updating frozen runtime dependencies"
"$VENV/bin/python" -m pip install --quiet --disable-pip-version-check \
  "huggingface_hub>=0.34,<2" \
  "numpy>=1.26,<3" \
  "opencv-python-headless>=4.10,<5"

if [[ -s "$FINAL_OUT/final_summary.json" && "$REPRODUCE" -ne 1 ]]; then
  echo "Locked IRIS final-test output already exists: $FINAL_OUT/final_summary.json" >&2
  echo "Refusing to overwrite. Use --reproduce only for an exact-lock deterministic rerun." >&2
  exit 2
fi

if [[ ! -s "$LOCK" ]]; then
  echo "[iris-final] creating final-test lock BEFORE final-test download/analysis"
  "$VENV/bin/python" research/analysis/freeze_iris_pendulum_final_test.py \
    --out "$LOCK" \
    --require-clean
else
  echo "[iris-final] checking supplied/existing final-test lock"
  "$VENV/bin/python" research/analysis/freeze_iris_pendulum_final_test.py \
    --check "$LOCK"
fi

# Re-check the exact coverage immediately before opening the final-test download.
"$VENV/bin/python" research/analysis/freeze_iris_pendulum_final_test.py \
  --check "$LOCK"

echo "[iris-final] lock valid; opening reserved pendulum_90 split"
"$VENV/bin/python" research/scripts/fetch_iris_pendulum_repeats.py \
  --data-root "$DATA_ROOT" \
  --phase final_test \
  --lock "$LOCK"

python3 research/analysis/prepare_public_validation_datasets.py \
  --root "$DATA_ROOT" \
  --out "$PUBLIC_ROOT"

python3 research/analysis/adapt_public_validation_inputs.py \
  --repo-root . \
  --data-root "$DATA_ROOT" \
  --prepared-root "$PUBLIC_ROOT"

if [[ "$REPRODUCE" -ne 1 ]]; then
  rm -rf "$FINAL_OUT"
fi

"$VENV/bin/python" research/analysis/run_iris_pendulum_validation.py \
  --adapted-root "$PUBLIC_ROOT/adapted" \
  --out "$FINAL_OUT" \
  --split final_test \
  --lock "$LOCK"

"$VENV/bin/python" research/analysis/summarize_iris_pendulum_final_test.py \
  --records "$FINAL_OUT/validation_records.csv" \
  --take-summary "$FINAL_OUT/take_summary.csv" \
  --out "$FINAL_OUT/final_summary.json"

EXTRA_ARGS=(
  --extra "$FINAL_OUT/validation_records.csv"
)

IRIS_VAL="$BUILD_DIR/publication-validation/iris-pendulum-validation/validation_records.csv"
GAUGE_VAL="$BUILD_DIR/publication-validation/gauge-prospective-validation/validation_records.csv"
if [[ -s "$IRIS_VAL" ]]; then EXTRA_ARGS+=(--extra "$IRIS_VAL"); fi
if [[ -s "$GAUGE_VAL" ]]; then EXTRA_ARGS+=(--extra "$GAUGE_VAL"); fi

bash research/scripts/run_publication_validation.sh \
  --skip-probes \
  --lock "$LOCK" \
  --out "$COMBINED" \
  "${EXTRA_ARGS[@]}"

"$VENV/bin/python" - "$FINAL_OUT" "$COMBINED" "$LOCK" <<'PY'
import json
import pathlib
import sys

final = pathlib.Path(sys.argv[1])
combined = pathlib.Path(sys.argv[2])
lock = pathlib.Path(sys.argv[3])

s = json.loads((final / "final_summary.json").read_text())
a = json.loads((combined / "analysis/summary.json").read_text())

print()
print("=== IRIS pendulum LOCKED FINAL TEST ===")
print("lock:", lock)
print("takes:", s["take_count"], "quality-pass:", s["quality_pass_take_count"])
print("median length rel error:", s["median_period_inferred_length_relative_error"])
for key in ("primary", "baseline"):
    m = s[key]
    print(m["method"], "strict accuracy:", m["strict_accuracy"])
    for truth in ("support", "veto", "unresolved"):
        q = m["truth"][truth]
        print(" ", truth, f'{q["correct_n"]}/{q["n"]} correct')
print(
    "paired primary wins:", s["paired_primary_only_correct"],
    "baseline wins:", s["paired_baseline_only_correct"],
    "ties:", s["paired_ties"],
)
print("combined confirmatory records:", a["confirmatory_record_count"])
print("final records:", final / "validation_records.csv")
print("combined analysis:", combined / "analysis")
print()
print("FINAL TEST COMPLETE. Do not retune the frozen method against these results.")
PY
