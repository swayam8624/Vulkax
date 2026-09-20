#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${1:-$ROOT/build-native-checkpoint}"
OUT="$BUILD/research-checkpoint"

if [[ "$(uname -s)" != "Darwin" ]]; then
  echo "This checkpoint requires macOS + Metal."
  exit 2
fi

echo "== Vulkax Native Viewer Research Checkpoint 1 =="
echo "root : $ROOT"
echo "build: $BUILD"

cmake -S "$ROOT" -B "$BUILD"   -DCMAKE_BUILD_TYPE=Release   -DVULKAX_BUILD_TESTS=ON

cmake --build "$BUILD"   --target     vulkax_viewer     vulkax_viewer_benchmark     vulkax_viewer_scene_tests     vulkax_viewer_asset_import_tests   --parallel "$(sysctl -n hw.logicalcpu)"

ctest --test-dir "$BUILD"   -R '^vulkax_viewer_(scene|asset_import)$'   --output-on-failure

CXX="${CXX:-c++}"
"$CXX" -std=c++20 -Wall -Wextra -Wpedantic -I"$ROOT/include"   "$ROOT/tests/gpu_sort_session_tests.cpp"   -o "$BUILD/vulkax_gpu_sort_session_tests"
"$BUILD/vulkax_gpu_sort_session_tests"

"$CXX" -std=c++20 -Wall -Wextra -Wpedantic -I"$ROOT/include"   "$ROOT/tests/visibility_gpu_handoff_tests.cpp"   -o "$BUILD/vulkax_visibility_gpu_handoff_tests"
"$BUILD/vulkax_visibility_gpu_handoff_tests"

mkdir -p "$OUT"
"$BUILD/vulkax_viewer_benchmark"   --sizes 10000,50000,100000,200000   --repeats 5   --json "$OUT/native_viewer_benchmark.json"   --markdown "$OUT/native_viewer_benchmark.md"

python3 - "$OUT/native_viewer_benchmark.json" <<'PY'
import json, math, sys
from pathlib import Path

path = Path(sys.argv[1])
data = json.loads(path.read_text())
assert data["schema"] == "vulkax.native_viewer.research_checkpoint.v1"
assert [r["source_count"] for r in data["rows"]] == [10000, 50000, 100000, 200000]
for row in data["rows"]:
    assert row["parity"] is True, row
    assert row["retained_count"] == row["source_count"], row
    for key, value in row.items():
        if key.endswith("_ms") or key == "speedup_vs_cpu":
            assert isinstance(value, (int, float)) and math.isfinite(value), (key, value)
            assert value >= 0.0, (key, value)
print("checkpoint_json_contract: passed")
PY

echo
cat "$OUT/native_viewer_benchmark.md"
echo
echo "CHECKPOINT 1: PASSED"
echo "Artifacts: $OUT"
