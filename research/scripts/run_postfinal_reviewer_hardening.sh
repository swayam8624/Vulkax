#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"; cd "$ROOT"

BUILD="build"
VENV=""
PROFILE="full"
NO_VIDEO=0
WITH_FREEFALL_VALIDATION=0

while [[ $# -gt 0 ]]; do
  case "$1" in
    --build-dir) BUILD="$2"; shift 2;;
    --venv) VENV="$2"; shift 2;;
    --profile) PROFILE="$2"; shift 2;;
    --no-video) NO_VIDEO=1; shift;;
    --with-freefall-validation) WITH_FREEFALL_VALIDATION=1; shift;;
    *) echo "Unknown option: $1" >&2; exit 2;;
  esac
done

if [[ "$PROFILE" != "quick" && "$PROFILE" != "full" ]]; then
  echo "--profile must be quick or full" >&2; exit 2
fi

VENV="${VENV:-$BUILD/.venv-reviewer-hardening}"
[[ -x "$VENV/bin/python" ]] || python3 -m venv "$VENV"
"$VENV/bin/python" -m pip install --quiet --disable-pip-version-check   "numpy>=1.26,<3" "opencv-python-headless>=4.10,<5" "huggingface_hub>=0.34,<2"

echo "=== Reviewer hardening: cluster-correct statistics ==="
"$VENV/bin/python" research/analysis/analyze_iris_clustered_statistics.py   --records "$BUILD/publication-validation/iris-pendulum-final-test/validation_records.csv"   --out "$BUILD/publication-validation/iris-pendulum-final-test/clustered_statistics"

echo "=== Reviewer hardening: stronger post-final baselines ==="
"$VENV/bin/python" research/analysis/analyze_iris_strong_baselines.py   --campaign "$BUILD/publication-validation/iris-pendulum-final-test"   --out "$BUILD/publication-validation/iris-pendulum-postfinal-baselines"

echo "=== Reviewer hardening: GT-hidden proposal -> verification ==="
"$VENV/bin/python" research/analysis/run_iris_gt_hidden_proposal_gate.py   --campaign "$BUILD/publication-validation/iris-pendulum-final-test"   --out "$BUILD/publication-validation/iris-pendulum-postfinal-proposals"

echo "=== Reviewer hardening: measured-truth uncertainty ==="
"$VENV/bin/python" research/analysis/analyze_iris_truth_uncertainty.py   --records "$BUILD/publication-validation/iris-pendulum-final-test/validation_records.csv"   --adapted-root "$BUILD/publication-validation/public-data/adapted"   --out "$BUILD/publication-validation/iris-pendulum-postfinal-truth-uncertainty"

echo "=== Reviewer hardening: proposal/probe dependence ==="
"$VENV/bin/python" research/analysis/run_iris_channel_dependence.py   --campaign "$BUILD/publication-validation/iris-pendulum-final-test"   --out "$BUILD/publication-validation/iris-pendulum-postfinal-dependence"

echo "=== Reviewer hardening: corruption robustness ($PROFILE) ==="
"$VENV/bin/python" research/analysis/run_iris_postfinal_robustness.py \
  --campaign "$BUILD/publication-validation/iris-pendulum-final-test" \
  --out "$BUILD/publication-validation/iris-pendulum-postfinal-robustness" \
  --profile "$PROFILE" \
  --backend auto

echo "=== Reviewer hardening: pinned official IRIS references ==="
"$VENV/bin/python" research/scripts/fetch_iris_reference_baselines.py   --out "$BUILD/publication-validation/iris-official-reference"

echo "=== Reviewer hardening: deterministic figures ==="
"$VENV/bin/python" visualization/scripts/render_reviewer_hardening.py   --build-root "$BUILD"   --out "$BUILD/visualization/reviewer-hardening"

if [[ "$NO_VIDEO" -eq 1 ]]; then
  "$VENV/bin/python" visualization/scripts/render_gt_hidden_rewrite_demo.py \
    --campaign "$BUILD/publication-validation/iris-pendulum-final-test" \
    --proposals "$BUILD/publication-validation/iris-pendulum-postfinal-proposals/evaluated_proposals.csv" \
    --out "$BUILD/visualization/reviewer-hardening" \
    --no-video
else
  "$VENV/bin/python" visualization/scripts/render_gt_hidden_rewrite_demo.py \
    --campaign "$BUILD/publication-validation/iris-pendulum-final-test" \
    --proposals "$BUILD/publication-validation/iris-pendulum-postfinal-proposals/evaluated_proposals.csv" \
    --out "$BUILD/visualization/reviewer-hardening"
fi

if [[ "$WITH_FREEFALL_VALIDATION" -eq 1 ]]; then
  echo "=== Reviewer hardening: second blind domain development+validation ==="
  bash research/scripts/run_iris_freefall_blind_validation.sh --build-dir "$BUILD" --venv "$VENV"
fi

"$VENV/bin/python" - "$BUILD" <<'PY'
import hashlib,json,pathlib,sys
b=pathlib.Path(sys.argv[1])
paths=[
 b/"publication-validation/iris-pendulum-final-test/clustered_statistics/summary.json",
 b/"publication-validation/iris-pendulum-postfinal-baselines/summary.json",
 b/"publication-validation/iris-pendulum-postfinal-proposals/summary.json",
 b/"publication-validation/iris-pendulum-postfinal-truth-uncertainty/summary.json",
 b/"publication-validation/iris-pendulum-postfinal-dependence/summary.json",
 b/"publication-validation/iris-pendulum-postfinal-robustness/summary.json",
 b/"publication-validation/iris-official-reference/manifest.json",
 b/"visualization/reviewer-hardening/manifest.json",
]
missing=[str(p) for p in paths if not p.is_file()]
if missing: raise SystemExit("missing hardening outputs:\n"+"\n".join(missing))
items=[]
for p in paths:
 h=hashlib.sha256(p.read_bytes()).hexdigest()
 items.append({"path":str(p),"sha256":h,"bytes":p.stat().st_size})
out=b/"publication-validation/reviewer-hardening-manifest.json"
out.write_text(json.dumps({"schema":"vulkax.reviewer_hardening_manifest","version":1,
 "post_final":True,"locked_result_replaced":False,"artifacts":items},indent=2)+"\n")
print("\n=== REVIEWER HARDENING COMPLETE (post-final analyses) ===")
print("manifest:",out)
print("Locked IRIS result remains unchanged.")
PY

"$VENV/bin/python" research/analysis/summarize_iris_postfinal_hardening.py \
  --build-dir "$BUILD" \
  --out "$BUILD/publication-validation/iris-pendulum-postfinal-hardening-summary.json"
