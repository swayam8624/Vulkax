#!/usr/bin/env bash
set -Eeuo pipefail
trap 'rc=$?; echo "[freefall-v66-smoke] ERROR rc=$rc line=$LINENO command=$BASH_COMMAND" >&2; exit $rc' ERR

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"

BUILD="build"
TAKE="06"
CONFIG="research/validation/iris_freefall_rescue_v6.json"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --build-dir) BUILD="$2"; shift 2;;
    --take) TAKE="$2"; shift 2;;
    --config) CONFIG="$2"; shift 2;;
    *) echo "Unknown option $1" >&2; exit 2;;
  esac
done

case "$TAKE" in
  06|07|08|09|10) ;;
  *) echo "[freefall-v66-smoke] development take must be one of 06..10" >&2; exit 2;;
esac

VENV="$BUILD/.venv-iris-freefall"
PUBLIC="$BUILD/publication-validation/public-data"
OUT="$BUILD/publication-validation/iris-freefall-v66-smoke-$TAKE"
TMP_CFG="$(mktemp -t vulkax-v66-smoke.XXXXXX.json)"
trap 'rm -f "$TMP_CFG"' EXIT

[[ -x "$VENV/bin/python" ]] || python3 -m venv "$VENV"
"$VENV/bin/python" -m pip install --quiet --disable-pip-version-check \
  "numpy>=1.26,<3" "opencv-python-headless>=4.10,<5" "pyflakes>=3.2,<4"

"$VENV/bin/python" -m py_compile research/analysis/run_iris_freefall_validation.py
"$VENV/bin/python" -m pyflakes research/analysis/run_iris_freefall_validation.py

"$VENV/bin/python" - "$CONFIG" "$TMP_CFG" "$TAKE" <<'PY'
import json,sys
src,dst,take=sys.argv[1:]
cfg=json.load(open(src,encoding="utf-8"))
assert cfg["tracker"]["revision"]=="ball_identity_v6_6", cfg["tracker"]["revision"]
cfg["dataset"]["development"]["takes"]=[take]
cfg["development_gate"]["minimum_quality_videos"]=1
json.dump(cfg,open(dst,"w",encoding="utf-8"),indent=2)
PY

if [[ ! -d "$PUBLIC/adapted/iris" ]]; then
  echo "[freefall-v66-smoke] adapted development data missing; this smoke command never downloads data." >&2
  echo "[freefall-v66-smoke] run the normal development materialization once, then retry." >&2
  exit 2
fi

rm -rf "$OUT"
echo "[freefall-v66-smoke] analyzing development-only drop_50/$TAKE; validation is forbidden"

trap - ERR
set +e
"$VENV/bin/python" research/analysis/run_iris_freefall_validation.py \
  --adapted-root "$PUBLIC/adapted" \
  --config "$TMP_CFG" \
  --split development \
  --out "$OUT"
RC=$?
set -e
trap 'rc=$?; echo "[freefall-v66-smoke] ERROR rc=$rc line=$LINENO command=$BASH_COMMAND" >&2; exit $rc' ERR

if [[ ! -s "$OUT/summary.json" ]]; then
  echo "[freefall-v66-smoke] analyzer failed before summary generation (rc=$RC)" >&2
  [[ -s "$OUT/failure_details.json" ]] && cat "$OUT/failure_details.json" >&2
  exit "$RC"
fi

"$VENV/bin/python" - "$OUT" <<'PY'
import csv,json,pathlib,sys
out=pathlib.Path(sys.argv[1])
s=json.loads((out/"summary.json").read_text())
print("\n=== V6.6 ONE-TAKE DEVELOPMENT SMOKE ===")
for k in (
    "tracker_revision","expected_videos","quality_pass_videos",
    "implementation_errors","median_acceleration_relative_error","gate_pass"
):
    print(k.upper(),s.get(k))
if (out/"take_summary.csv").exists():
    for r in csv.DictReader((out/"take_summary.csv").open()):
        print(
            "TAKE",r["scene"],
            "QUALITY",r["quality_ok"],
            "G",r["direct_acceleration_m_s2"],
            "G_REL_ERR",r["acceleration_relative_error"],
            "T",r["full_fall_time_s"],
            "TIMING",r["timing_fit_rms_frames"],
            "SHAPE",r["trajectory_shape_rms_fraction"],
            "RELEASE_RATIO",r["release_speed_ratio"],
        )
if (out/"candidate_audit.csv").exists():
    print("\nTOP CANDIDATES")
    for r in list(csv.DictReader((out/"candidate_audit.csv").open()))[:8]:
        print(
            "rank",r["event_rank"],
            "selected",r["selected"],
            "track",r["track_id"],
            "T",r["full_fall_time_s"],
            "g",r["direct_acceleration_m_s2"],
            "err",r["acceleration_relative_error"],
            "release_extrap_frames",r.get("release_extrapolation_frames",""),
            "impact_speed_ratio",r.get("impact_speed_ratio",""),
            "impact",r.get("impact_boundary_kind",""),
            "a_stab",r.get("acceleration_stability",""),
        )
print("\nVALIDATION_STATUS NOT_REQUESTED")
PY

IMPL="$("$VENV/bin/python" - "$OUT/summary.json" <<'PY'
import json,sys
print(int(json.load(open(sys.argv[1])).get("implementation_errors",0)))
PY
)"
if [[ "$IMPL" -ne 0 ]]; then
  exit 1
fi

echo "[freefall-v66-smoke] completed; scientific gate may pass or fail without being treated as a software crash."
exit 0
