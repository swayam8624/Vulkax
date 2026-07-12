#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 1 ]]; then
  echo "usage: $0 /path/to/LveEngine [output-dir]" >&2
  exit 2
fi

EXE="$1"
OUT="${2:-beacon_results_matrix}"

mkdir -p "$OUT"

run_case() {
  local technique="$1"
  local objects="$2"
  local lights="$3"
  local distribution="$4"
  local resolution="$5"
  local width="${resolution%x*}"
  local height="${resolution#*x}"
  local name="${technique}_${objects}o_${lights}l_${distribution}_${resolution}"
  local group="model_predictions"
  if [[ "$technique" == "ssbo" || "$technique" == "instanced" || "$technique" == "gpu-clustered" ||
        "$technique" == "adaptive-exact" || "$technique" == "adaptive-bounded" || "$technique" == "beacon" ]]; then
    group="vulkan_cpu_measurements"
  fi

  local capture=()
  if [[ "$technique" == "gpu-clustered" || "$technique" == "adaptive-exact" ||
        "$technique" == "adaptive-bounded" || "$technique" == "beacon" ]]; then
    capture=(--capture-reference true)
  fi

  "$EXE" \
    --benchmark \
    --technique "$technique" \
    --scene repeated \
    --objects "$objects" \
    --lights "$lights" \
    --light-distribution "$distribution" \
    --width "$width" \
    --height "$height" \
    --frames 300 \
    --warmup-frames 120 \
    --seed 1337 \
    --show-light-billboards false \
    "${capture[@]}" \
    --output "$OUT/$group/$name"
}

for technique in ssbo instanced gpu-clustered cpu-clustered fixed-cluster-cost-model adaptive-exact adaptive-bounded beacon; do
  run_case "$technique" 100 10 uniform 1280x720
  run_case "$technique" 1000 100 uniform 1920x1080
  run_case "$technique" 1000 500 single-hotspot 1920x1080
  run_case "$technique" 5000 1000 multi-hotspot 1920x1080
  run_case "$technique" 5000 1000 depth-stacked 2560x1440
  run_case "$technique" 10000 2000 adversarial 1920x1080
done

echo "BEACON matrix complete: $OUT"
