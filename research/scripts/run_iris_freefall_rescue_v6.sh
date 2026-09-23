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
"$VENV/bin/python" -m pip install --quiet --disable-pip-version-check   "huggingface_hub>=0.34,<2" "numpy>=1.26,<3" "opencv-python-headless>=4.10,<5" "pyflakes>=3.2,<4"

echo "[freefall-v6] preflight: compile/config/contracts/synthetic tracker"
"$VENV/bin/python" -m py_compile   research/analysis/run_iris_freefall_validation.py   research/scripts/fetch_iris_freefall_rescue_v6.py
"$VENV/bin/python" - "$CONFIG" <<'PY'
import json,sys
p=sys.argv[1]
cfg=json.load(open(p))
assert int(cfg.get("version",0))==6, cfg.get("version")
rev=cfg["tracker"]["revision"]
assert isinstance(rev,str) and rev.startswith("ball_identity_v6_"), rev
assert cfg["dataset"]["development"]["takes"]==["06","07","08","09","10"]
assert cfg["dataset"]["validation"]["takes"]==["06","07","08","09","10"]
assert not (set(cfg["dataset"]["prior_failed_validation"]["takes"])
            & set(cfg["dataset"]["validation"]["takes"]))
print("VALID",cfg["tracker"]["revision"],"config preflight")
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

echo "[freefall-v6] analyzing development split"
trap - ERR
set +e
"$VENV/bin/python" research/analysis/run_iris_freefall_validation.py \
  --adapted-root "$PUBLIC/adapted" \
  --config "$CONFIG" \
  --split development \
  --out "$DEV"
DEV_RC=$?
set -e
trap 'rc=$?; echo "[freefall-v6] ERROR rc=$rc line=$LINENO command=$BASH_COMMAND" >&2; exit $rc' ERR

if [[ ! -s "$DEV/summary.json" ]]; then
  echo "[freefall-v6] ERROR: analyzer exited rc=$DEV_RC without summary.json" >&2
  exit "$DEV_RC"
fi

DEV_CLASS="$("$VENV/bin/python" - "$DEV/summary.json" <<'PY'
import json,sys
s=json.load(open(sys.argv[1]))
impl=int(s.get("implementation_errors",0))
gate=bool(s.get("gate_pass"))
if impl:
    print("implementation_error")
elif gate:
    print("gate_pass")
else:
    print("gate_fail")
PY
)"

print_candidate_audit() {
  if [[ ! -s "$DEV/candidate_audit.csv" ]]; then
    echo "[freefall-v6] candidate audit unavailable (no candidate rows)"
    return 0
  fi
  "$VENV/bin/python" - "$DEV/candidate_audit.csv" "$CONFIG" <<'PY'
import csv,json,sys
from collections import defaultdict
rows=list(csv.DictReader(open(sys.argv[1],newline="",encoding="utf-8")))
cfg=json.load(open(sys.argv[2],encoding="utf-8"))
revision=cfg["tracker"]["revision"]
by=defaultdict(list)
for r in rows:
    by[r["scene"]].append(r)
print(f"\n=== {revision} DEVELOPMENT CANDIDATE AUDIT ===")
for scene in sorted(by):
    q=sorted(by[scene],key=lambda r:int(r["event_rank"]))[:6]
    print(scene)
    for r in q:
        mark="*" if r["selected"].lower()=="true" else " "
        print(
          f" {mark} rank={r['event_rank']} track={r['track_id']}"
          f" sign={float(r['sign']):+.0f}"
          f" span_rel={float(r['relative_span']):.3f}"
          f" pspan={float(r['global_progress_span']):.3f}"
          f" T={float(r['full_fall_time_s']):.4f}s"
          f" t0={float(r['t0_s']):.3f}s"
          f" frames={r['interval_frames']}"
          f" timing={float(r['timing_fit_rms_frames']):.3f}"
          f" shape={float(r['trajectory_shape_rms_fraction']):.3f}"
          f" identity={float(r['identity_score']):.3f}"
          + (f" a_stab={float(r['acceleration_stability']):.3f}"
             if r.get("acceleration_stability","") not in ("",None) else "")
          + (f" roots={r['roots_complete']}"
             if r.get("roots_complete","") not in ("",None) else "")
          + f" g_rel_err={float(r['acceleration_relative_error']):.3f}"
        )
PY
}

case "$DEV_CLASS" in
  implementation_error)
    echo "[freefall-v6] ERROR: development analyzer reported implementation/I/O failure" >&2
    [[ -s "$DEV/failure_details.json" ]] && echo "[freefall-v6] details: $DEV/failure_details.json" >&2
    exit "$DEV_RC"
    ;;
  gate_fail)
    echo
    echo "[freefall-v6] DEVELOPMENT_GATE_FAIL (scientific outcome, not software error)"
    print_candidate_audit
    echo "[freefall-v6] candidate audit: $DEV/candidate_audit.csv"
    echo "[freefall-v6] validation drop_100/06..10 was NOT requested."
    if [[ "$DEVELOPMENT_ONLY" -eq 1 ]]; then
      exit 0
    fi
    echo "[freefall-v6] STOP: development gate failed; validation remains unopened."
    exit 0
    ;;
  gate_pass)
    echo
    echo "[freefall-v6] DEVELOPMENT_GATE_PASS"
    if [[ "$DEVELOPMENT_ONLY" -eq 1 ]]; then
      print_candidate_audit
      echo "[freefall-v6] candidate audit: $DEV/candidate_audit.csv"
      echo "[freefall-v6] validation drop_100/06..10 was NOT requested by --development-only."
      exit 0
    fi
    ;;
  *)
    echo "[freefall-v6] ERROR: unknown development classification: $DEV_CLASS" >&2
    exit 3
    ;;
esac

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
