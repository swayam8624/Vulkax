#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"

PROFILE="core"
DATA_ROOT="build/public-datasets"
OUT_ROOT="build/publication-validation/public-data"
VENV="build/.venv-public-validation"
DATASETS="gauge iris rgbench"

usage() {
  cat <<'EOF'
Usage: bash research/scripts/prepare_public_validation_data.sh [options]

Options:
  --profile smoke|core|full   Download profile (default: core)
  --data-root DIR            Dataset cache root (default: build/public-datasets)
  --out DIR                  Prepared manifest root
  --venv DIR                 Python venv for Hugging Face downloader
  --datasets "..."           Space-separated subset of: gauge iris rgbench
  -h, --help                 Show help

Profiles:
  smoke  Minimal network/integrity test.
  core   Recommended Reality Probe validation subset:
         - GAUGE: 60 foam trials (stretch/compression/shear; soft+hard)
         - IRIS: 24 videos, one take from all 24 physical settings
         - RGBench: one grasp/fold/fling capture for each of
           green_tshirt, grey_pleat_skirt, white_shirt + meshes/reference data
  full   Complete public releases. This is multi-gigabyte.
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --profile) PROFILE="$2"; shift 2 ;;
    --data-root) DATA_ROOT="$2"; shift 2 ;;
    --out) OUT_ROOT="$2"; shift 2 ;;
    --venv) VENV="$2"; shift 2 ;;
    --datasets) DATASETS="$2"; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown option: $1" >&2; usage >&2; exit 2 ;;
  esac
done

case "$PROFILE" in
  smoke|core|full) ;;
  *) echo "invalid --profile: $PROFILE" >&2; exit 2 ;;
esac

if [[ ! -x "$VENV/bin/python" ]]; then
  echo "[public-data] creating Python environment: $VENV"
  python3 -m venv "$VENV"
fi

echo "[public-data] installing/updating downloader dependency"
"$VENV/bin/python" -m pip install --quiet --disable-pip-version-check   "huggingface_hub>=0.34,<2"

# shellcheck disable=SC2086
"$VENV/bin/python" research/scripts/fetch_public_validation_datasets.py   --root "$DATA_ROOT"   --profile "$PROFILE"   --datasets $DATASETS

python3 research/analysis/prepare_public_validation_datasets.py   --root "$DATA_ROOT"   --out "$OUT_ROOT"

python3 research/analysis/adapt_public_validation_inputs.py   --repo-root .   --data-root "$DATA_ROOT"   --prepared-root "$OUT_ROOT"

python3 research/analysis/generate_controlled_validation_plan.py   --worlds "$OUT_ROOT/world_manifest.csv"   --profile "$([[ "$PROFILE" == "smoke" ]] && echo smoke || echo full)"   --out "$OUT_ROOT/trial_plan.csv"

python3 - "$DATA_ROOT" "$OUT_ROOT" <<'PY'
import json
import pathlib
import sys
data = pathlib.Path(sys.argv[1])
out = pathlib.Path(sys.argv[2])
download = json.loads((data / "campaign_download_manifest.json").read_text())
prep = json.loads((out / "preparation_report.json").read_text())
print()
print("=== Public validation data ready ===")
print("profile:", download["profile"])
for ds in download["datasets"]:
    print(
        f"{ds['dataset']}: {ds['file_count']} files, "
        f"{ds['total_bytes']/(1024**2):.1f} MiB, revision={ds['revision'][:12]}"
    )
print("inventory scenes:", prep["inventory_scene_count"])
print("prospective-ready worlds:", prep["prospective_world_count"])
print("world manifest:", out / "world_manifest.csv")
print("adapted inputs:", out / "adapted/adapter_summary.json")
print("trial plan:", out / "trial_plan.csv")
print()
print("NOTE: files are downloaded, integrity-hashed, split, and adapted into Vulkax-facing inputs.")
print("They become Reality Probe result evidence only after the planned method trials execute.")
PY
