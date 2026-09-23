#!/usr/bin/env bash
set -Eeuo pipefail
trap 'rc=$?; echo "[freefall-v6] ERROR rc=$rc line=$LINENO command=$BASH_COMMAND" >&2; exit $rc' ERR

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"

BUILD="build"
VENV=""
CONFIG="research/validation/iris_freefall_rescue_v6.json"
DEVELOPMENT_ONLY=0

while [[ $# -gt 0 ]]; do
  case "$1" in
    --build-dir) BUILD="$2"; shift 2;;
    --venv) VENV="$2"; shift 2;;
    --config) CONFIG="$2"; shift 2;;
    --development-only) DEVELOPMENT_ONLY=1; shift;;
    *) echo "Unknown option $1" >&2; exit 2;;
  esac
done

VENV="${VENV:-$BUILD/.venv-iris-freefall}"
DATA="$BUILD/public-datasets"
PUBLIC="$BUILD/publication-validation/public-data"
DEV="$BUILD/publication-validation/iris-freefall-v6-development"
VAL="$BUILD/publication-validation/iris-freefall-v6-validation"
COMBINED="$BUILD/publication-validation/combined-freefall-v6-validation"

[[ -x "$VENV/bin/python" ]] || python3 -m venv "$VENV"
"$VENV/bin/python" -m pip install --quiet --disable-pip-version-check   "huggingface_hub>=0.34,<2" "numpy>=1.26,<3" "opencv-python-headless==4.14.0.94"

"$VENV/bin/python" - <<'PY'
import cv2,numpy,sys
print("[freefall-v6] runtime Python",sys.version.split()[0],
      "OpenCV",cv2.__version__,"NumPy",numpy.__version__)
PY
echo "[freefall-v6] preflight: compile/config/contracts/synthetic tracker"
"$VENV/bin/python" -m py_compile   research/analysis/run_iris_freefall_validation.py   research/scripts/fetch_iris_freefall_rescue_v6.py
"$VENV/bin/python" - "$CONFIG" <<'PY'
import json,sys
p=sys.argv[1]
cfg=json.load(open(p))
assert int(cfg.get("version",0))==6, cfg.get("version")
assert cfg["tracker"]["revision"]=="ball_identity_v6_3", cfg["tracker"]["revision"]
assert cfg["dataset"]["development"]["takes"]==["06","07","08","09","10"]
assert cfg["dataset"]["validation"]["takes"]==["06","07","08","09","10"]
assert not (set(cfg["dataset"]["prior_failed_validation"]["takes"])
            & set(cfg["dataset"]["validation"]["takes"]))
print("VALID V6.3 config preflight")
PY
"$VENV/bin/python" research/analysis/run_iris_freefall_validation.py --self-test
"$VENV/bin/python" research/analysis/run_iris_freefall_validation.py --self-test-video
echo "[freefall-v6] preflight PASS"

if [[ -d "$DEV" ]]; then
  STAMP="$(date -u +%Y%m%dT%H%M%SZ)"
  ARCHIVE="$BUILD/publication-validation/archive/iris-freefall-v6-development-$STAMP"
  mkdir -p "$(dirname "$ARCHIVE")"
  mv "$DEV" "$ARCHIVE"
  echo "[freefall-v6] archived previous development output -> $ARCHIVE"
fi

echo "[freefall-v6] materializing FRESH development split only"
"$VENV/bin/python" research/scripts/fetch_iris_freefall_rescue_v6.py   --data-root "$DATA" --config "$CONFIG" --phase development

python3 research/analysis/prepare_public_validation_datasets.py   --root "$DATA" --out "$PUBLIC"
python3 research/analysis/adapt_public_validation_inputs.py   --repo-root . --data-root "$DATA" --prepared-root "$PUBLIC"

DEV_ARGS=(
  --adapted-root "$PUBLIC/adapted"
  --config "$CONFIG"
  --split development
  --out "$DEV"
)
if [[ "$DEVELOPMENT_ONLY" -eq 1 ]]; then
  DEV_ARGS+=(--allow-gate-failure)
fi
"$VENV/bin/python" research/analysis/run_iris_freefall_validation.py "${DEV_ARGS[@]}"

if [[ "$DEVELOPMENT_ONLY" -eq 1 ]]; then
  echo
  echo "[freefall-v6] DEVELOPMENT-ONLY COMPLETE"
  "$VENV/bin/python" - "$DEV/summary.json" <<'PY'
import json,sys
s=json.load(open(sys.argv[1]))
print("[freefall-v6] development scientific gate:", "PASS" if s["gate_pass"] else "FAIL")
print("[freefall-v6] implementation errors:", s.get("implementation_errors",0))
PY
  echo "[freefall-v6] candidate audit: $DEV/candidate_audit.csv"
  echo "[freefall-v6] validation drop_100/06..10 was NOT requested by this command."
  exit 0
fi

rm -rf "$VAL"

echo "[freefall-v6] development gate PASS; materializing FRESH validation split"
"$VENV/bin/python" research/scripts/fetch_iris_freefall_rescue_v6.py   --data-root "$DATA" --config "$CONFIG" --phase validation

python3 research/analysis/prepare_public_validation_datasets.py   --root "$DATA" --out "$PUBLIC"
python3 research/analysis/adapt_public_validation_inputs.py   --repo-root . --data-root "$DATA" --prepared-root "$PUBLIC"

"$VENV/bin/python" research/analysis/run_iris_freefall_validation.py   --adapted-root "$PUBLIC/adapted"   --config "$CONFIG"   --split validation   --out "$VAL"   --require-development-summary "$DEV/summary.json"

EXTRA=(--extra "$VAL/validation_records.csv")
for p in   "$BUILD/publication-validation/iris-pendulum-validation/validation_records.csv"   "$BUILD/publication-validation/gauge-prospective-validation/validation_records.csv"; do
  [[ -s "$p" ]] && EXTRA+=(--extra "$p")
done

bash research/scripts/run_publication_validation.sh   --skip-probes --out "$COMBINED" "${EXTRA[@]}"

"$VENV/bin/python" - "$DEV" "$VAL" <<'PY'
import json,pathlib,sys
d=json.loads((pathlib.Path(sys.argv[1])/"summary.json").read_text())
v=json.loads((pathlib.Path(sys.argv[2])/"summary.json").read_text())
print("\n=== IRIS FREE-FALL V6 FRESH DEVELOPMENT/VALIDATION ===")
for name,s in (("development",d),("validation",v)):
    print(name,
          "tracker",s["tracker_revision"],
          "quality",s["quality_pass_videos"],"/",s["expected_videos"],
          "median acceleration rel error",s["median_acceleration_relative_error"],
          "truth-control",s["truth_control_accuracy"],
          "placebo FAR",s["placebo_false_assertion_rate"],
          "direction sign",s["direction_sign_rate"],
          "gate",s["gate_pass"])
print("FAILED V5 validation remains nonconfirmatory.")
print("FINAL drop_150/02..10 STATUS: NOT DOWNLOADED BY THIS COMMAND.")
PY
