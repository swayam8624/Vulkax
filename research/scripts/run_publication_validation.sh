#!/usr/bin/env bash
set -euo pipefail

# Reviewer-facing Reality Probe validation entrypoint.
#
# Default behavior is conservative: reuse existing frozen D4V/OFC outputs when
# available. Build/run those probes only when their proposal tables are missing.
# Prospective trials from new datasets are supplied with repeated --extra RECORDS.
# This script never fabricates unexecuted robustness or measured evidence.

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"

BUILD_DIR="build"
OUT_DIR=""
REFRESH_PROBES=0
SKIP_PROBES=0
LOCK=""
BOOTSTRAP_REPS=""
PUBLIC_DATA_PROFILE=""
PUBLIC_DATA_ROOT=""
EXTRA_COUNT=0

usage() {
  cat <<'EOF'
Usage: bash research/scripts/run_publication_validation.sh [options]

Options:
  --build-dir DIR       Build directory (default: build)
  --out DIR             Validation output directory (default: BUILD/publication-validation)
  --refresh-probes      Re-run frozen D4V/OFC probes even if outputs already exist
  --skip-probes         Do not build/run D4V/OFC; use only available/--extra records
  --extra CSV           Add standardized prospective/multi-dataset records (repeatable)
  --public-data-profile smoke|core|full
                        Download + prepare public GAUGE/IRIS/RGBench inputs before analysis
  --public-data-root DIR Dataset cache root (default: BUILD/public-datasets)
  --lock JSON           Check a previously frozen final-test lock before analysis
  --bootstrap-reps N    Override bootstrap replicate count for quick diagnostics
  --diagnostic          Alias for default conservative behavior
  -h, --help            Show this message

Examples:
  bash research/scripts/run_publication_validation.sh --diagnostic
  bash research/scripts/run_publication_validation.sh --refresh-probes
  bash research/scripts/run_publication_validation.sh --public-data-profile core
  bash research/scripts/run_publication_validation.sh --extra build/new-dataset/records.csv
  bash research/scripts/run_publication_validation.sh \
    --lock build/publication-validation/final_test_lock.json \
    --extra build/final-test/records.csv
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --build-dir) BUILD_DIR="$2"; shift 2 ;;
    --out) OUT_DIR="$2"; shift 2 ;;
    --refresh-probes) REFRESH_PROBES=1; shift ;;
    --skip-probes) SKIP_PROBES=1; shift ;;
    --extra)
      var="EXTRA_${EXTRA_COUNT}"
      printf -v "$var" "%s" "$2"
      EXTRA_COUNT=$((EXTRA_COUNT + 1))
      shift 2
      ;;
    --public-data-profile) PUBLIC_DATA_PROFILE="$2"; shift 2 ;;
    --public-data-root) PUBLIC_DATA_ROOT="$2"; shift 2 ;;
    --lock) LOCK="$2"; shift 2 ;;
    --bootstrap-reps) BOOTSTRAP_REPS="$2"; shift 2 ;;
    --diagnostic) shift ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown option: $1" >&2; usage >&2; exit 2 ;;
  esac
done

OUT_DIR="${OUT_DIR:-$BUILD_DIR/publication-validation}"
PUBLIC_DATA_ROOT="${PUBLIC_DATA_ROOT:-$BUILD_DIR/public-datasets}"
mkdir -p "$OUT_DIR"

if [[ -n "$LOCK" ]]; then
  python3 research/analysis/freeze_publication_validation.py --check "$LOCK"
fi

python3 -m py_compile \
  research/analysis/normalize_publication_validation.py \
  research/analysis/analyze_publication_validation.py \
  research/analysis/generate_controlled_validation_plan.py \
  research/analysis/freeze_publication_validation.py \
  research/analysis/plan_validation_sample_size.py \
  research/analysis/analyze_transaction_utility.py \
  research/analysis/prepare_public_validation_datasets.py \
  research/analysis/adapt_public_validation_inputs.py \
  research/scripts/fetch_public_validation_datasets.py

if [[ -n "$PUBLIC_DATA_PROFILE" ]]; then
  case "$PUBLIC_DATA_PROFILE" in
    smoke|core|full) ;;
    *) echo "invalid --public-data-profile: $PUBLIC_DATA_PROFILE" >&2; exit 2 ;;
  esac
  bash research/scripts/prepare_public_validation_data.sh \
    --profile "$PUBLIC_DATA_PROFILE" \
    --data-root "$PUBLIC_DATA_ROOT" \
    --out "$OUT_DIR/public-data"
fi

D4V="$BUILD_DIR/dcs-d4v-discovery/proposals.csv"
OFC="$BUILD_DIR/orthogonal-force-compliance/proposals.csv"

if [[ "$SKIP_PROBES" == "0" ]]; then
  if [[ "$REFRESH_PROBES" == "1" || ! -s "$D4V" || ! -s "$OFC" ]]; then
    echo "[publication-validation] frozen D4V/OFC outputs missing or refresh requested"
    cmake -S . -B "$BUILD_DIR" -DVULKAX_BUILD_TESTS=ON
    cmake --build "$BUILD_DIR" --parallel \
      --target vulkax_dcs_d4v_repair_veto_probe vulkax_orthogonal_force_compliance_probe

    rm -rf "$BUILD_DIR/dcs-d4v-discovery" "$BUILD_DIR/orthogonal-force-compliance"
    "$BUILD_DIR/vulkax_dcs_d4v_repair_veto_probe" "$BUILD_DIR/dcs-d4v-discovery"
    python3 research/probes/analyze_dcs_d4v_repair_veto.py "$BUILD_DIR/dcs-d4v-discovery"

    "$BUILD_DIR/vulkax_orthogonal_force_compliance_probe" "$BUILD_DIR/orthogonal-force-compliance"
    python3 research/probes/analyze_orthogonal_force_compliance.py "$BUILD_DIR/orthogonal-force-compliance"
  else
    echo "[publication-validation] reusing existing frozen D4V/OFC proposal tables"
  fi
fi

NORMALIZE_ARGS=(--out "$OUT_DIR/records.csv")
if [[ -s "$D4V" ]]; then NORMALIZE_ARGS+=(--d4v "$D4V"); fi
if [[ -s "$OFC" ]]; then NORMALIZE_ARGS+=(--ofc "$OFC"); fi
i=0
while [[ "$i" -lt "$EXTRA_COUNT" ]]; do
  var="EXTRA_$i"
  extra="${!var}"
  [[ -s "$extra" ]] || { echo "missing/empty --extra record file: $extra" >&2; exit 1; }
  NORMALIZE_ARGS+=(--extra "$extra")
  i=$((i + 1))
done

if [[ ${#NORMALIZE_ARGS[@]} -eq 2 ]]; then
  echo "No D4V/OFC or --extra records are available." >&2
  exit 1
fi

python3 research/analysis/normalize_publication_validation.py "${NORMALIZE_ARGS[@]}"

ANALYZE_ARGS=(
  --records "$OUT_DIR/records.csv"
  --protocol research/validation/protocol_v1.json
  --out "$OUT_DIR/analysis"
)
if [[ -n "$BOOTSTRAP_REPS" ]]; then
  ANALYZE_ARGS+=(--bootstrap-reps "$BOOTSTRAP_REPS")
fi
python3 research/analysis/analyze_publication_validation.py "${ANALYZE_ARGS[@]}"

python3 research/analysis/analyze_transaction_utility.py \
  --records "$OUT_DIR/records.csv" \
  --threshold 2.0 \
  --out "$OUT_DIR/analysis/transaction_utility.csv"

python3 research/analysis/plan_validation_sample_size.py \
  --out "$OUT_DIR/sample_size_plan.json" >/dev/null

python3 - "$OUT_DIR" <<'PY'
import json
import pathlib
import sys
root = pathlib.Path(sys.argv[1])
summary = json.loads((root / "analysis/summary.json").read_text())
print()
print("=== Reality Probe publication-validation summary ===")
print("records:", summary["record_count"])
print("datasets:", summary["dataset_count"])
print("methods:", summary["method_count"])
print("confirmatory records:", summary["confirmatory_record_count"])
print("historical diagnostic records:", summary["retrospective_or_followon_record_count"])
if summary["confirmatory_record_count"] == 0:
    print("STATUS: diagnostic only; no new prospective validation evidence has been supplied.")
else:
    print("STATUS: prospective records present; interpret by split/evidence_class and lock status.")
print("outputs:", root)
public_manifest = root / "public-data/world_manifest.csv"
if public_manifest.is_file():
    print("public dataset world manifest:", public_manifest)
    print("public dataset trial plan:", root / "public-data/trial_plan.csv")
PY
