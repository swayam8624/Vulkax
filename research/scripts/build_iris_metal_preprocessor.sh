#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"

OUT="${1:-build/tools/vulkax_iris_metal_preprocess}"
mkdir -p "$(dirname "$OUT")"

if [[ "$(uname -s)" != "Darwin" ]]; then
  echo "Metal preprocessing is macOS-only." >&2
  exit 2
fi

if ! command -v xcrun >/dev/null 2>&1; then
  echo "xcrun/Xcode command line tools are required." >&2
  exit 2
fi

SRC="src/tools/iris_metal_preprocess.mm"

echo "[metal-preprocess] compiling $SRC"
xcrun clang++ \
  -std=c++20 \
  -O3 \
  -DNDEBUG \
  -fobjc-arc \
  -Wall -Wextra -Wpedantic \
  "$SRC" \
  -framework Foundation \
  -framework Metal \
  -framework AVFoundation \
  -framework CoreVideo \
  -framework CoreMedia \
  -o "$OUT"

echo "VALID Metal IRIS preprocessor build -> $OUT"
