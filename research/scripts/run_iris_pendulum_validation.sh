#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"

BUILD_DIR="build"
VENV=""
LOCK=""
PHASE="validation"

usage() {
  cat <<'EOF'
Usage: bash research/scripts/run_iris_pendulum_validation.sh [options]

Options:
  --build-dir DIR       Build root (default: build)
  --venv DIR            Python venv (default: BUILD/.venv-iris-pendulum)
  --phase validation    Run development gate then validation (default)
  --lock FILE           Reserved for a future locked final-test run
  -h, --help            Show help

The validation phase downloads only IRIS pendulum_20 and pendulum_45 repeats.
It does not request additional pendulum_90 final-test videos.
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --build-dir) BUILD_DIR="$2"; shift 2 ;;
    --venv) VENV="$2"; shift 2 ;;
    --phase) PHASE="$2"; shift 2 ;;
    --lock) LOCK="$2"; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown option: $1" >&2; usage >&2; exit 2 ;;
  esac
done

if [[ "$PHASE" != "validation" ]]; then
  echo "Only --phase validation is enabled before the final-test freeze." >&2
  exit 2
fi

VENV="${VENV:-$BUILD_DIR/.venv-iris-pendulum}"
DATA_ROOT="$BUILD_DIR/public-datasets"
PUBLIC_ROOT="$BUILD_DIR/publication-validation/public-data"
DEV_OUT="$BUILD_DIR/publication-validation/iris-pendulum-development"
VAL_OUT="$BUILD_DIR/publication-validation/iris-pendulum-validation"
COMBINED="$BUILD_DIR/publication-validation/combined-iris-validation"

if [[ ! -x "$VENV/bin/python" ]]; then
  echo "[iris-pendulum] creating Python environment: $VENV"
  python3 -m venv "$VENV"
fi

echo "[iris-pendulum] installing/updating lightweight validation dependencies"
"$VENV/bin/python" -m pip install --quiet --disable-pip-version-check \
  "huggingface_hub>=0.34,<2" \
  "numpy>=1.26,<3" \
  "opencv-python-headless>=4.10,<5"

"$VENV/bin/python" research/scripts/fetch_iris_pendulum_repeats.py \
  --data-root "$DATA_ROOT" \
  --phase validation

python3 research/analysis/prepare_public_validation_datasets.py \
  --root "$DATA_ROOT" \
  --out "$PUBLIC_ROOT"

python3 research/analysis/adapt_public_validation_inputs.py \
  --repo-root . \
  --data-root "$DATA_ROOT" \
  --prepared-root "$PUBLIC_ROOT"

rm -rf "$DEV_OUT" "$VAL_OUT"

echo "[iris-pendulum] running frozen development gate on pendulum_20 takes"
"$VENV/bin/python" research/analysis/run_iris_pendulum_validation.py \
  --adapted-root "$PUBLIC_ROOT/adapted" \
  --out "$DEV_OUT" \
  --split development

echo "[iris-pendulum] development passed; opening pendulum_45 validation takes"
"$VENV/bin/python" research/analysis/run_iris_pendulum_validation.py \
  --adapted-root "$PUBLIC_ROOT/adapted" \
  --out "$VAL_OUT" \
  --split validation \
  --require-development-summary "$DEV_OUT/summary.json"

EXTRA_ARGS=(
  --extra "$VAL_OUT/validation_records.csv"
)
GAUGE_RECORDS="$BUILD_DIR/publication-validation/gauge-prospective-validation/validation_records.csv"
if [[ -s "$GAUGE_RECORDS" ]]; then
  EXTRA_ARGS+=(--extra "$GAUGE_RECORDS")
fi

bash research/scripts/run_publication_validation.sh \
  --skip-probes \
  --out "$COMBINED" \
  "${EXTRA_ARGS[@]}"

"$VENV/bin/python" - "$DEV_OUT" "$VAL_OUT" "$COMBINED" <<'PY'
import csv
import json
import pathlib
import sys

dev = pathlib.Path(sys.argv[1])
val = pathlib.Path(sys.argv[2])
combined = pathlib.Path(sys.argv[3])

d = json.loads((dev / "summary.json").read_text())
v = json.loads((val / "summary.json").read_text())
rows = list(csv.DictReader((val / "validation_records.csv").open()))

print()
print("=== IRIS pendulum prospective validation ===")
print(
    "development:",
    d["quality_pass_take_count"],
    "/",
    d["expected_take_count"],
    "quality-pass; median length rel error",
    d["median_period_inferred_length_relative_error"],
)
print(
    "validation:",
    v["quality_pass_take_count"],
    "/",
    v["expected_take_count"],
    "quality-pass; records",
    v["record_count"],
)
for method in ("finite_amplitude_period_probe", "small_angle_period_baseline"):
    mr = [r for r in rows if r["method"] == method]
    print(method)
    for truth in ("support", "veto", "unresolved"):
        q = [r for r in mr if r["ground_truth"] == truth]
        if not q:
            continue
        correct = sum(r["decision"] == truth for r in q)
        print(" ", truth, f"{correct}/{len(q)} correct")
print("validation records:", val / "validation_records.csv")
print("combined analysis:", combined / "analysis")
print()
print("FINAL-TEST STATUS: pendulum_90 remains unopened by this command.")
PY
