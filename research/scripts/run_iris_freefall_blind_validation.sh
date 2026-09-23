#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"; cd "$ROOT"
BUILD="build"; VENV=""
while [[ $# -gt 0 ]]; do
  case "$1" in
    --build-dir) BUILD="$2"; shift 2;;
    --venv) VENV="$2"; shift 2;;
    *) echo "Unknown option $1" >&2; exit 2;;
  esac
done
VENV="${VENV:-$BUILD/.venv-iris-freefall}"
DATA="$BUILD/public-datasets"
PUBLIC="$BUILD/publication-validation/public-data"
DEV="$BUILD/publication-validation/iris-freefall-development"
VAL="$BUILD/publication-validation/iris-freefall-validation"
COMBINED="$BUILD/publication-validation/combined-freefall-validation"
[[ -x "$VENV/bin/python" ]] || python3 -m venv "$VENV"
"$VENV/bin/python" -m pip install --quiet --disable-pip-version-check "huggingface_hub>=0.34,<2" "numpy>=1.26,<3" "opencv-python-headless>=4.10,<5"
"$VENV/bin/python" research/scripts/fetch_iris_freefall_blind.py --data-root "$DATA" --phase development_validation
python3 research/analysis/prepare_public_validation_datasets.py --root "$DATA" --out "$PUBLIC"
python3 research/analysis/adapt_public_validation_inputs.py --repo-root . --data-root "$DATA" --prepared-root "$PUBLIC"
rm -rf "$DEV" "$VAL"
"$VENV/bin/python" research/analysis/run_iris_freefall_validation.py --adapted-root "$PUBLIC/adapted" --split development --out "$DEV"
"$VENV/bin/python" research/analysis/run_iris_freefall_validation.py --adapted-root "$PUBLIC/adapted" --split validation --out "$VAL" --require-development-summary "$DEV/summary.json"
EXTRA=(--extra "$VAL/validation_records.csv")
for p in  "$BUILD/publication-validation/iris-pendulum-validation/validation_records.csv"  "$BUILD/publication-validation/gauge-prospective-validation/validation_records.csv"; do
 [[ -s "$p" ]] && EXTRA+=(--extra "$p")
done
bash research/scripts/run_publication_validation.sh --skip-probes --out "$COMBINED" "${EXTRA[@]}"
"$VENV/bin/python" - "$DEV" "$VAL" <<'PY'
import json,sys,pathlib
d=json.loads((pathlib.Path(sys.argv[1])/"summary.json").read_text())
v=json.loads((pathlib.Path(sys.argv[2])/"summary.json").read_text())
print("\n=== IRIS FREE-FALL BLIND DEVELOPMENT/VALIDATION ===")
for name,s in (("development",d),("validation",v)):
 print(name, "quality",s["quality_pass_videos"],"/",s["expected_videos"],
       "median acceleration rel error",s["median_acceleration_relative_error"],
       "truth-control",s["truth_control_accuracy"],
       "placebo FAR",s["placebo_false_assertion_rate"],
       "direction sign",s["direction_sign_rate"],
       "gate",s["gate_pass"])
print("FINAL drop_150/02..10 STATUS: NOT DOWNLOADED BY THIS COMMAND.")
PY
