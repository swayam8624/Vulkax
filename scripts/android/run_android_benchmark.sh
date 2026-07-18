#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 1 ]]; then
  echo "usage: $0 <device-executable> [remote-output-dir]" >&2
  exit 2
fi

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
"$root/scripts/android/check_android_vulkan.sh"

binary="$1"
remote_output="${2:-/data/local/tmp/geobeacon-results}"
remote_root="/data/local/tmp/geobeacon"
adb_args=()
if [[ -n "${ANDROID_SERIAL:-}" ]]; then
  adb_args=(-s "$ANDROID_SERIAL")
fi

adb "${adb_args[@]}" shell mkdir -p "$remote_root" "$remote_output"
adb "${adb_args[@]}" push "$binary" "$remote_root/LveEngine"
adb "${adb_args[@]}" push "$root/data/connaught_place" "$remote_root/data/"
adb "${adb_args[@]}" shell chmod 755 "$remote_root/LveEngine"
adb "${adb_args[@]}" shell "cd $remote_root && ./LveEngine --geo \
  --geo-policy geo-beacon-bounded --geo-camera-path street-drive \
  --lights 500 --width 1280 --height 720 --warmup-frames 600 --frames 1800 \
  --capture-reference false --output $remote_output --quiet"
mkdir -p "$root/docs/results/android"
adb "${adb_args[@]}" pull "$remote_output" "$root/docs/results/android/"
echo "Android benchmark results copied to docs/results/android/."
