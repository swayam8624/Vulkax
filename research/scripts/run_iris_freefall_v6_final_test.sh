#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"

BUILD="build"
VENV=""
CONFIG="research/validation/iris_freefall_rescue_v6.json"
LOCK=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --build-dir) BUILD="$2"; shift 2;;
    --venv) VENV="$2"; shift 2;;
    --config) CONFIG="$2"; shift 2;;
    --lock) LOCK="$2"; shift 2;;
    *) echo "Unknown option $1" >&2; exit 2;;
  esac
done

VENV="${VENV:-$BUILD/.venv-iris-freefall}"
LOCK="${LOCK:-$BUILD/publication-validation/iris-freefall-v6-final-lock.json}"

DATA="$BUILD/public-datasets"
PUBLIC="$BUILD/publication-validation/public-data"
VAL="$BUILD/publication-validation/iris-freefall-v6-validation"
FINAL="$BUILD/publication-validation/iris-freefall-v6-final-test"
COMBINED="$BUILD/publication-validation/combined-freefall-v6-final"

[[ -x "$VENV/bin/python" ]] || python3 -m venv "$VENV"
"$VENV/bin/python" -m pip install --quiet --disable-pip-version-check   "huggingface_hub>=0.34,<2" "numpy>=1.26,<3" "opencv-python-headless>=4.10,<5"

if [[ ! -s "$VAL/summary.json" ]]; then
  echo "[freefall-v6-final] STOP: fresh V6 validation summary is missing. Final data remains unopened." >&2
  exit 2
fi

"$VENV/bin/python" - "$VAL/summary.json" "$CONFIG" <<'PY'
import json,sys
s=json.load(open(sys.argv[1]))
cfg=json.load(open(sys.argv[2]))
if s.get("split")!="validation" or not s.get("gate_pass"):
    raise SystemExit("[freefall-v6-final] STOP: fresh V6 validation gate is not PASS; final data remains unopened.")
if s.get("tracker_revision")!=cfg["tracker"]["revision"] or int(s.get("version",0))!=6:
    raise SystemExit("[freefall-v6-final] STOP: validation is not the frozen V6 protocol.")
print("[freefall-v6-final] fresh validation PASS; creating/checking final lock")
PY

if [[ ! -s "$LOCK" ]]; then
  "$VENV/bin/python" research/analysis/freeze_iris_freefall_v6_final.py     --out "$LOCK" --require-clean
else
  "$VENV/bin/python" research/analysis/freeze_iris_freefall_v6_final.py     --check "$LOCK"
fi

"$VENV/bin/python" research/analysis/freeze_iris_freefall_v6_final.py   --check "$LOCK"

echo "[freefall-v6-final] V6 lock valid; final population may now be requested"
"$VENV/bin/python" research/scripts/fetch_iris_freefall_v6_final.py   --data-root "$DATA" --config "$CONFIG" --lock "$LOCK"

python3 research/analysis/prepare_public_validation_datasets.py   --root "$DATA" --out "$PUBLIC"
python3 research/analysis/adapt_public_validation_inputs.py   --repo-root . --data-root "$DATA" --prepared-root "$PUBLIC"

rm -rf "$FINAL"
"$VENV/bin/python" research/analysis/run_iris_freefall_validation.py   --adapted-root "$PUBLIC/adapted"   --config "$CONFIG"   --split final_test   --out "$FINAL"   --require-validation-summary "$VAL/summary.json"   --lock "$LOCK"

EXTRA=(
  --extra "$FINAL/validation_records.csv"
  --extra "$VAL/validation_records.csv"
)
for p in   "$BUILD/publication-validation/iris-pendulum-final-test/validation_records.csv"   "$BUILD/publication-validation/iris-pendulum-validation/validation_records.csv"   "$BUILD/publication-validation/gauge-prospective-validation/validation_records.csv"; do
  [[ -s "$p" ]] && EXTRA+=(--extra "$p")
done

bash research/scripts/run_publication_validation.sh   --skip-probes --lock "$LOCK" --out "$COMBINED" "${EXTRA[@]}"

"$VENV/bin/python" - "$FINAL" "$COMBINED" "$LOCK" <<'PY'
import csv,json,pathlib,sys
f=pathlib.Path(sys.argv[1]);c=pathlib.Path(sys.argv[2]);lock=sys.argv[3]
s=json.loads((f/"summary.json").read_text())
rows=list(csv.DictReader((f/"validation_records.csv").open()))
print("\n=== IRIS FREE-FALL V6 LOCKED FINAL TEST ===")
print("lock:",lock)
print("quality:",s["quality_pass_videos"],"/",s["expected_videos"])
print("median acceleration rel error:",s["median_acceleration_relative_error"])
for method in sorted({r["method"] for r in rows}):
    q=[r for r in rows if r["method"]==method]
    print(method,"strict accuracy",sum(r["decision"]==r["ground_truth"] for r in q)/len(q))
    for truth in ("support","veto","unresolved"):
        z=[r for r in q if r["ground_truth"]==truth]
        print(" ",truth,f'{sum(r["decision"]==truth for r in z)}/{len(z)} correct')
a=json.loads((c/"analysis/summary.json").read_text())
print("combined confirmatory records:",a["confirmatory_record_count"])
print("V6 FINAL TEST COMPLETE. No post-final retuning.")
PY
