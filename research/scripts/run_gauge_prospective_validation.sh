#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"

SPLIT="validation"
BUILD_DIR="build"
LOCK=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --split) SPLIT="$2"; shift 2 ;;
    --build-dir) BUILD_DIR="$2"; shift 2 ;;
    --lock) LOCK="$2"; shift 2 ;;
    -h|--help)
      echo "Usage: bash research/scripts/run_gauge_prospective_validation.sh [--split development|validation|final_test] [--lock FILE]"
      exit 0 ;;
    *) echo "Unknown option: $1" >&2; exit 2 ;;
  esac
done

ADAPTED="$BUILD_DIR/publication-validation/public-data/adapted"
[[ -d "$ADAPTED/gauge" ]] || {
  echo "Missing adapted GAUGE inputs. Run:" >&2
  echo "  bash research/scripts/run_publication_validation.sh --public-data-profile core" >&2
  exit 1
}

cmake -S . -B "$BUILD_DIR" -DVULKAX_BUILD_TESTS=ON
cmake --build "$BUILD_DIR" --parallel --target vulkax_gauge_shearing_forward_probe

OUT="$BUILD_DIR/publication-validation/gauge-prospective-$SPLIT"
ARGS=(
  --exe "$BUILD_DIR/vulkax_gauge_shearing_forward_probe"
  --adapted-root "$ADAPTED"
  --out "$OUT"
  --split "$SPLIT"
)
if [[ -n "$LOCK" ]]; then ARGS+=(--lock "$LOCK"); fi

python3 research/analysis/run_gauge_prospective_validation.py "${ARGS[@]}"

bash research/scripts/run_publication_validation.sh \
  --skip-probes \
  --extra "$OUT/validation_records.csv" \
  --out "$BUILD_DIR/publication-validation/combined-$SPLIT"

python3 - "$OUT" "$BUILD_DIR/publication-validation/combined-$SPLIT" <<'PY'
import csv,json,pathlib,sys
out=pathlib.Path(sys.argv[1]); combined=pathlib.Path(sys.argv[2])
meta=json.loads((out/"summary.json").read_text())
rows=list(csv.DictReader((out/"case_summary.csv").open()))
print()
print("=== GAUGE prospective validation ===")
print("split:",meta["split"])
print("cases:",meta["case_count"],"records:",meta["record_count"])
for truth in ("support","veto","unresolved"):
    q=[r for r in rows if r["truth"]==truth]
    if not q: continue
    w=sum(r["witness_decision"]==truth for r in q)
    raw=sum(r["raw_decision"]==truth for r in q)
    print(f"{truth}: witness {w}/{len(q)} correct, raw {raw}/{len(q)} correct")
print("records:",out/"validation_records.csv")
print("combined analysis:",combined/"analysis")
PY
