#!/usr/bin/env bash
set -euo pipefail

# Vulkax end-to-end research reproduction runner.
#
# Default:
#   ./run_everything.sh
#
# Optional:
#   ./run_everything.sh --backend Metal
#   ./run_everything.sh --backend Vulkan
#   ./run_everything.sh --backend none
#   ./run_everything.sh --jobs 8
#   ./run_everything.sh --skip-gauge
#   ./run_everything.sh --skip-performance
#   ./run_everything.sh --clean
#   ./run_everything.sh --exhaustive
#
# The default run reproduces the current paper-facing evidence stack:
# build + full ctest + release validation + backend conformance + captured-world
# controlled run + DCS D1/D2/D3/D4V + GAUGE retrospective + evidence packaging.
#
# --exhaustive additionally runs selected historical falsification probes that are
# preserved for provenance but are not part of the final canonical result claim.

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT"

BUILD_DIR="${VULKAX_BUILD_DIR:-build}"
PAPER_DIR="${VULKAX_PAPER_DIR:-$BUILD_DIR/paper-evidence}"
JOBS=""
BACKEND="auto"
SKIP_GAUGE=0
SKIP_PERFORMANCE=0
CLEAN=0
EXHAUSTIVE=0

usage() {
  cat <<'EOF'
usage: ./run_everything.sh [options]

Options:
  --backend <auto|Metal|Vulkan|none>  Render/backend conformance target.
  --jobs <N>                         Parallel build jobs.
  --skip-gauge                       Skip public GAUGE download/retrospective.
  --skip-performance                 Skip captured-world timing benchmark.
  --clean                            Remove build/ before configuring.
  --exhaustive                       Run selected historical falsification probes.
  -h, --help                         Show this help.

Environment:
  VULKAX_BUILD_DIR   Override build directory (default: build)
  VULKAX_PAPER_DIR   Override evidence-package directory
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --backend)
      BACKEND="$2"; shift 2;;
    --jobs)
      JOBS="$2"; shift 2;;
    --skip-gauge)
      SKIP_GAUGE=1; shift;;
    --skip-performance)
      SKIP_PERFORMANCE=1; shift;;
    --clean)
      CLEAN=1; shift;;
    --exhaustive)
      EXHAUSTIVE=1; shift;;
    -h|--help)
      usage; exit 0;;
    *)
      echo "unknown option: $1" >&2
      usage >&2
      exit 2;;
  esac
done

case "$BACKEND" in
  auto|Metal|Vulkan|none) ;;
  *) echo "--backend must be auto, Metal, Vulkan, or none" >&2; exit 2;;
esac

if [[ -z "$JOBS" ]]; then
  if command -v sysctl >/dev/null 2>&1 && sysctl -n hw.logicalcpu >/dev/null 2>&1; then
    JOBS="$(sysctl -n hw.logicalcpu)"
  elif command -v nproc >/dev/null 2>&1; then
    JOBS="$(nproc)"
  else
    JOBS=2
  fi
fi

if ! [[ "$JOBS" =~ ^[1-9][0-9]*$ ]]; then
  echo "--jobs must be a positive integer" >&2
  exit 2
fi

if [[ "$BACKEND" == "auto" ]]; then
  case "$(uname -s)" in
    Darwin) BACKEND="Metal";;
    Linux)
      if command -v vulkaninfo >/dev/null 2>&1; then BACKEND="Vulkan"; else BACKEND="none"; fi
      ;;
    *) BACKEND="none";;
  esac
fi

timestamp() { date '+%Y-%m-%d %H:%M:%S'; }
stage() {
  echo
  echo "================================================================================"
  echo "[$(timestamp)] $*"
  echo "================================================================================"
}

run_logged() {
  local name="$1"; shift
  mkdir -p "$PAPER_DIR/logs"
  echo "+ $*"
  "$@" 2>&1 | tee "$PAPER_DIR/logs/$name.log"
}

require() {
  command -v "$1" >/dev/null 2>&1 || {
    echo "required command not found: $1" >&2
    exit 1
  }
}

stage "Preflight"
require git
require cmake
require python3
require c++
mkdir -p "$PAPER_DIR"/{logs,system,canonical,generated}

if [[ "$CLEAN" == "1" ]]; then
  echo "Removing $BUILD_DIR"
  rm -rf "$BUILD_DIR"
  mkdir -p "$PAPER_DIR"/{logs,system,canonical,generated}
fi

# Capture environment before any experiment.
{
  echo "VULKAX_REPRODUCTION_SYSTEM_INFO"
  echo "date=$(date -u '+%Y-%m-%dT%H:%M:%SZ')"
  echo "commit=$(git rev-parse HEAD)"
  echo "branch=$(git branch --show-current)"
  echo "dirty=$(git status --porcelain | wc -l | tr -d ' ')"
  echo
  echo "=== git ==="
  git status --short --branch
  echo
  echo "=== uname ==="
  uname -a
  echo
  echo "=== compiler ==="
  c++ --version || true
  echo
  echo "=== cmake ==="
  cmake --version
  echo
  echo "=== python ==="
  python3 --version
  echo
  echo "=== CPU ==="
  if command -v lscpu >/dev/null 2>&1; then lscpu; else sysctl -a 2>/dev/null | grep -E 'machdep.cpu|hw.(model|memsize|ncpu|physicalcpu|logicalcpu)' || true; fi
  echo
  echo "=== GPU ==="
  nvidia-smi || true
  echo
  echo "=== Vulkan ==="
  vulkaninfo --summary || true
  echo
  echo "=== Metal ==="
  system_profiler SPDisplaysDataType 2>/dev/null || true
} > "$PAPER_DIR/system/system-info.txt"

stage "Validate Python research/release tooling"
python3 -m py_compile   scripts/validate_evidence_registry.py   scripts/audit_release_claims.py   scripts/test_release_cli_failures.py   scripts/benchmark_captured_world_run.py   research/analysis/dcs_confirmatory_replay.py   research/analysis/export_dcs_spatial_map.py   research/analysis/export_dcs_evidence_pack.py   research/analysis/gauge_dcs_retrospective.py   research/analysis/gauge_shearing_effective_span.py   research/analysis/validate_gauge_effective_span.py   research/probes/analyze_dcs_darkfield.py   research/probes/analyze_dcs_solver_native.py   research/probes/analyze_dcs_active_selection.py   research/probes/analyze_dcs_d2_validation.py   research/probes/analyze_dcs_d3_witness_space.py   research/probes/analyze_dcs_d4v_repair_veto.py   research/probes/export_dcs_d3_tables.py   research/analysis/assemble_paper_evidence.py \
  research/analysis/generate_paper_assets.py \
  research/analysis/validate_paper_reproduction.py \
  research/analysis/analyze_information_frontier.py

python3 research/analysis/dcs_confirmatory_replay.py --self-test
python3 research/analysis/export_dcs_spatial_map.py --self-test
python3 research/analysis/export_dcs_evidence_pack.py --self-test
python3 research/analysis/assemble_paper_evidence.py --self-test
python3 research/analysis/generate_paper_assets.py --self-test
python3 research/analysis/validate_paper_reproduction.py --self-test
python3 research/analysis/analyze_information_frontier.py --self-test

stage "Configure Release + tests"
cmake -S . -B "$BUILD_DIR"   -DCMAKE_BUILD_TYPE=Release   -DVULKAX_BUILD_TESTS=ON

stage "Build entire repository"
cmake --build "$BUILD_DIR" --parallel "$JOBS"

stage "Full CTest suite"
run_logged ctest-full   ctest --test-dir "$BUILD_DIR" --output-on-failure

stage "Release/evidence contract validation"
run_logged evidence-registry   python3 scripts/validate_evidence_registry.py .
run_logged release-claims   python3 scripts/audit_release_claims.py . --expected-project-version 1.0.0
run_logged release-cli   python3 scripts/test_release_cli_failures.py --executable "$BUILD_DIR/vulkax"

stage "Native backend check: $BACKEND"
if [[ "$BACKEND" != "none" ]]; then
  run_logged backend-required "$BUILD_DIR/vulkax" --require-backend "$BACKEND"
  run_logged backend-conformance "$BUILD_DIR/vulkax" --conformance "$BACKEND"
else
  echo "Skipping native backend conformance (--backend none)."
fi

stage "Controlled captured-world run"
rm -rf "$BUILD_DIR/paper-captured-example" "$BUILD_DIR/paper-captured-world-run"
run_logged captured-example   "$BUILD_DIR/vulkax" captured-deformable-generate-example "$BUILD_DIR/paper-captured-example"
run_logged captured-world   "$BUILD_DIR/vulkax" captured-world-run   "$BUILD_DIR/paper-captured-example/capture.vkcap"   "$BUILD_DIR/paper-captured-world-run"   m4 0.003 1 1 1   "$BACKEND" 0.08 0.01 0.02 12345

if [[ "$SKIP_PERFORMANCE" == "0" ]]; then
  stage "Controlled captured-world timing benchmark"
  run_logged captured-world-performance     python3 scripts/benchmark_captured_world_run.py       --executable "$BUILD_DIR/vulkax"       --output "$BUILD_DIR/paper-performance"       --iterations 3       --backend none
fi

stage "DCS D1 deterministic deceptive-repair positive control"
rm -rf "$BUILD_DIR/dcs-positive-control"
run_logged dcs-d1-run   "$BUILD_DIR/vulkax_dcs_darkfield_synthetic_probe" "$BUILD_DIR/dcs-positive-control"
run_logged dcs-d1-analyze   python3 research/probes/analyze_dcs_darkfield.py "$BUILD_DIR/dcs-positive-control"

stage "DCS solver-native fixed-witness discovery"
rm -rf "$BUILD_DIR/dcs-solver-native"
run_logged dcs-solver-native-run   "$BUILD_DIR/vulkax_dcs_solver_native_probe" "$BUILD_DIR/dcs-solver-native"
run_logged dcs-solver-native-analyze   python3 research/probes/analyze_dcs_solver_native.py "$BUILD_DIR/dcs-solver-native"

stage "DCS automatic active-selection discovery"
rm -rf "$BUILD_DIR/dcs-active-selection"
run_logged dcs-active-run   "$BUILD_DIR/vulkax_dcs_active_selection_probe" "$BUILD_DIR/dcs-active-selection"
run_logged dcs-active-analyze   python3 research/probes/analyze_dcs_active_selection.py "$BUILD_DIR/dcs-active-selection"

stage "D2 frozen fresh validation"
rm -rf "$BUILD_DIR/dcs-d2-validation"
run_logged dcs-d2-run   "$BUILD_DIR/vulkax_dcs_d2_validation_probe" "$BUILD_DIR/dcs-d2-validation"
run_logged dcs-d2-analyze   python3 research/probes/analyze_dcs_d2_validation.py "$BUILD_DIR/dcs-d2-validation"

stage "D3 witness-space/adaptive-order discovery"
rm -rf "$BUILD_DIR/dcs-d3-discovery"
run_logged dcs-d3-run   "$BUILD_DIR/vulkax_dcs_d3_witness_space_probe" "$BUILD_DIR/dcs-d3-discovery"
run_logged dcs-d3-analyze   python3 research/probes/analyze_dcs_d3_witness_space.py "$BUILD_DIR/dcs-d3-discovery"
python3 research/probes/export_dcs_d3_tables.py "$BUILD_DIR/dcs-d3-discovery"

stage "D4V pair-specific repair-veto discovery"
rm -rf "$BUILD_DIR/dcs-d4v-discovery"
run_logged dcs-d4v-run   "$BUILD_DIR/vulkax_dcs_d4v_repair_veto_probe" "$BUILD_DIR/dcs-d4v-discovery"
run_logged dcs-d4v-analyze   python3 research/probes/analyze_dcs_d4v_repair_veto.py "$BUILD_DIR/dcs-d4v-discovery"

if [[ "$SKIP_GAUGE" == "0" ]]; then
  stage "GAUGE public measured-data fetch"
  run_logged gauge-fetch     python3 research/scripts/fetch_gauge_foam_subset.py "$BUILD_DIR/gauge-download"

  stage "GAUGE measured-only effective-span inference"
  rm -rf "$BUILD_DIR/gauge-effective-span"
  run_logged gauge-effective-span     python3 research/analysis/gauge_shearing_effective_span.py       --root "$BUILD_DIR/gauge-download"       --out "$BUILD_DIR/gauge-effective-span"
  run_logged gauge-effective-span-validation     python3 research/analysis/validate_gauge_effective_span.py       "$BUILD_DIR/gauge-effective-span"

  stage "GAUGE DCS retrospective (20 definitive MPM forward simulations)"
  rm -rf "$BUILD_DIR/gauge-dcs-retrospective"
  run_logged gauge-dcs-retrospective     python3 -u research/analysis/gauge_dcs_retrospective.py       --root "$BUILD_DIR/gauge-download"       --exe "$BUILD_DIR/vulkax_gauge_shearing_forward_probe"       --effective-span-dir "$BUILD_DIR/gauge-effective-span"       --out "$BUILD_DIR/gauge-dcs-retrospective"
else
  echo "Skipping GAUGE measured retrospective (--skip-gauge)."
fi

if [[ "$EXHAUSTIVE" == "1" ]]; then
  stage "Selected historical falsification probes"
  HIST="$BUILD_DIR/historical-evidence"
  rm -rf "$HIST"
  mkdir -p "$HIST"

  # These runs preserve research provenance. They are not current flagship claims.
  "$BUILD_DIR/vulkax_solver_counterfactual_probe" "$HIST/solver-counterfactual" || true
  "$BUILD_DIR/vulkax_solver_active_design_probe" "$HIST/solver-active-design" || true
  "$BUILD_DIR/vulkax_solver_certificate_probe" "$HIST/solver-certificate" || true
  "$BUILD_DIR/vulkax_refusal_repair_probe" "$HIST/refusal-repair" || true
  "$BUILD_DIR/vulkax_refusal_validation_probe" "$HIST/refusal-validation" || true
  "$BUILD_DIR/vulkax_refusal_validation2_probe" "$HIST/refusal-validation2" || true
  "$BUILD_DIR/vulkax_refusal_validation3_probe" "$HIST/refusal-validation3" || true
  "$BUILD_DIR/vulkax_refusal_validation4_probe" "$HIST/refusal-validation4" || true
  "$BUILD_DIR/vulkax_refusal_validation5_probe" "$HIST/refusal-validation5" || true

  cat > "$HIST/README.txt" <<'EOF'
These historical probes are retained for provenance. Some intentionally terminate
with negative/falsification outcomes and are therefore executed best-effort.
They are not used to override the canonical D2/D3/D4V/GAUGE evidence.
EOF
fi

stage "Post-hoc information-frontier diagnostics"
DIAG_ARGS=(
  --d4v "$BUILD_DIR/dcs-d4v-discovery/proposals.csv"
  --gauge "$BUILD_DIR/gauge-dcs-retrospective/per_trial.csv"
  --out "$BUILD_DIR/paper-diagnostics"
)
if [[ "$SKIP_GAUGE" == "1" ]]; then DIAG_ARGS+=(--allow-missing-gauge); fi
python3 research/analysis/analyze_information_frontier.py "${DIAG_ARGS[@]}"

stage "Validate reproduced numbers against frozen paper ledger"
VALIDATION_ARGS=(
  --repo-root .
  --build-root "$BUILD_DIR"
  --out "$BUILD_DIR/paper-reproduction-validation.json"
)
if [[ "$SKIP_GAUGE" == "1" ]]; then VALIDATION_ARGS+=(--allow-missing-gauge); fi
python3 research/analysis/validate_paper_reproduction.py "${VALIDATION_ARGS[@]}"

stage "Generate deterministic paper figures and tables"
rm -rf "$BUILD_DIR/paper-figures"
python3 research/analysis/generate_paper_assets.py \
  --results research/results/DCS_FINAL_RESULTS_2026-09-20.json \
  --out "$BUILD_DIR/paper-figures"

stage "Assemble paper evidence bundle"
ARGS=(
  --repo-root .
  --build-root "$BUILD_DIR"
  --out "$PAPER_DIR"
)
if [[ "$SKIP_GAUGE" == "1" ]]; then ARGS+=(--allow-missing-gauge); fi
python3 research/analysis/assemble_paper_evidence.py "${ARGS[@]}"

stage "Final evidence integrity checks"
EVIDENCE_INPUTS=(
  "$BUILD_DIR/dcs-positive-control/analysis.json"
  "$BUILD_DIR/dcs-solver-native/analysis.json"
  "$BUILD_DIR/dcs-active-selection/analysis.json"
  "$BUILD_DIR/dcs-d2-validation/analysis.json"
  "$BUILD_DIR/dcs-d3-discovery/analysis.json"
  "$BUILD_DIR/dcs-d4v-discovery/analysis.json"
)
if [[ "$SKIP_GAUGE" == "0" ]]; then
  EVIDENCE_INPUTS+=("$BUILD_DIR/gauge-dcs-retrospective/summary.json")
fi
python3 research/analysis/export_dcs_evidence_pack.py \
  "$PAPER_DIR/generated/dcs-evidence-index.csv" \
  "${EVIDENCE_INPUTS[@]}"

python3 - <<PY
import json, pathlib
p=pathlib.Path("$PAPER_DIR/manifest.json")
d=json.loads(p.read_text())
assert d["schema"]=="vulkax.paper_evidence_bundle"
assert d["version"]==1
assert d["complete"] is True
print("VALID final paper-evidence bundle:", p)
print("artifacts:", len(d["artifacts"]))
PY

stage "Archive paper evidence bundle"
PAPER_ARCHIVE="$BUILD_DIR/vulkax-paper-evidence-$(git rev-parse --short=12 HEAD).tar.gz"
python3 - <<PY
import hashlib, pathlib, tarfile
root=pathlib.Path("$PAPER_DIR")
archive=pathlib.Path("$PAPER_ARCHIVE")
with tarfile.open(archive,"w:gz") as tf:
    tf.add(root,arcname="paper-evidence")
h=hashlib.sha256()
with archive.open("rb") as f:
    for chunk in iter(lambda:f.read(1024*1024),b""):
        h.update(chunk)
digest=h.hexdigest()
archive.with_suffix(archive.suffix+".sha256").write_text(f"{digest}  {archive.name}\n")
print("WROTE",archive)
print("SHA256",digest)
PY

stage "COMPLETE"
cat <<EOF
Vulkax full research reproduction completed.

Branch:  $(git branch --show-current)
Commit:  $(git rev-parse HEAD)
Backend: $BACKEND
Build:   $BUILD_DIR
Evidence package:
  $PAPER_DIR

Canonical paper-data entrypoint:
  $PAPER_DIR/README.md

Portable archive:
  $PAPER_ARCHIVE
  $PAPER_ARCHIVE.sha256

Important scientific status:
  D2/D3/D4V remain negative/frozen.
  GAUGE is retrospective.
  This run reproduces evidence; it does not convert negative results into a
  prospective positive flagship claim.
EOF
