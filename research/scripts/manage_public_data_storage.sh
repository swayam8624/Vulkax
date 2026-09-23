#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"

BUILD="build"
MODE="usage"
INCLUDE_GLOBAL_HF=0

while [[ $# -gt 0 ]]; do
  case "$1" in
    usage|pendulum|freefall|all-iris-videos|local-hf-cache) MODE="$1"; shift;;
    --build-dir) BUILD="$2"; shift 2;;
    --include-global-hf-cache) INCLUDE_GLOBAL_HF=1; shift;;
    *) echo "Unknown option: $1" >&2; exit 2;;
  esac
done

DATA="$BUILD/public-datasets"
IRIS="$DATA/iris"
RESULTS="$BUILD/publication-validation"
LOCAL_HF="$IRIS/.cache/huggingface"
GLOBAL_HF="${HOME}/.cache/huggingface/hub/datasets--rasulkhanbayov--IRIS"

size_of() {
  local p="$1"
  if [[ -e "$p" ]]; then du -sh "$p" 2>/dev/null | awk '{print $1}'; else echo "0B"; fi
}

show_usage() {
  echo "=== Vulkax disk usage ==="
  printf "%-46s %s\n" "$DATA" "$(size_of "$DATA")"
  printf "%-46s %s\n" "$IRIS" "$(size_of "$IRIS")"
  printf "%-46s %s\n" "$IRIS/Pendulum" "$(size_of "$IRIS/Pendulum")"
  printf "%-46s %s\n" "$IRIS/Dropping_ball" "$(size_of "$IRIS/Dropping_ball")"
  printf "%-46s %s\n" "$LOCAL_HF" "$(size_of "$LOCAL_HF")"
  printf "%-46s %s\n" "$RESULTS" "$(size_of "$RESULTS")"
  printf "%-46s %s\n" "$GLOBAL_HF" "$(size_of "$GLOBAL_HF")"
  echo
  df -h "$ROOT" | tail -1
}

require_evidence() {
  local p="$1"
  if [[ ! -s "$p" ]]; then
    echo "Refusing cleanup: expected result evidence missing: $p" >&2
    exit 3
  fi
}

delete_mp4_tree() {
  local dir="$1"
  if [[ -d "$dir" ]]; then
    find "$dir" -type f -name '*.mp4' -print -delete
    find "$dir" -type d -empty -delete 2>/dev/null || true
  fi
}

echo "[storage] before"
show_usage

case "$MODE" in
  usage)
    exit 0
    ;;
  pendulum)
    require_evidence "$RESULTS/iris-pendulum-final-test/final_summary.json"
    require_evidence "$RESULTS/iris-pendulum-postfinal-robustness/summary.json"
    echo "[storage] deleting redownloadable IRIS pendulum MP4s only"
    delete_mp4_tree "$IRIS/Pendulum"
    ;;
  freefall)
    require_evidence "$RESULTS/iris-freefall-development/summary.json"
    echo "[storage] deleting redownloadable IRIS free-fall development/validation MP4s"
    delete_mp4_tree "$IRIS/Dropping_ball/drop_50"
    delete_mp4_tree "$IRIS/Dropping_ball/drop_100"
    ;;
  all-iris-videos)
    require_evidence "$RESULTS/iris-pendulum-final-test/final_summary.json"
    require_evidence "$RESULTS/iris-pendulum-postfinal-robustness/summary.json"
    echo "[storage] deleting ALL redownloadable IRIS MP4 files; preserving parameters/manifests/results"
    if [[ -d "$IRIS" ]]; then
      find "$IRIS" -type f -name '*.mp4' -print -delete
      find "$IRIS" -type d -empty -delete 2>/dev/null || true
    fi
    ;;
  local-hf-cache)
    echo "[storage] deleting repo-local Hugging Face metadata/cache"
    rm -rf "$LOCAL_HF"
    ;;
esac

# Repo-local HF cache is always safe to recreate after media cleanup.
if [[ "$MODE" != "usage" && "$MODE" != "local-hf-cache" ]]; then
  rm -rf "$LOCAL_HF"
fi

if [[ "$INCLUDE_GLOBAL_HF" -eq 1 ]]; then
  echo "[storage] explicitly deleting global cached IRIS dataset blobs"
  rm -rf "$GLOBAL_HF"
fi

echo
echo "[storage] after"
show_usage
echo "VALID Vulkax storage cleanup mode=$MODE"
echo "Result evidence under $RESULTS was not deleted."
