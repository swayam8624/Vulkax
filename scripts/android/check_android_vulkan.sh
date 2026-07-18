#!/usr/bin/env bash
set -euo pipefail

if ! command -v adb >/dev/null 2>&1; then
  echo "adb is required (install Android platform-tools)." >&2
  exit 2
fi

device="${ANDROID_SERIAL:-}"
adb_args=()
if [[ -n "$device" ]]; then
  adb_args=(-s "$device")
fi

state="$(adb "${adb_args[@]}" get-state 2>/dev/null || true)"
if [[ "$state" != "device" ]]; then
  echo "No authorized Android device is connected." >&2
  exit 3
fi

echo "serial=$(adb "${adb_args[@]}" get-serialno)"
echo "model=$(adb "${adb_args[@]}" shell getprop ro.product.model | tr -d '\r')"
echo "sdk=$(adb "${adb_args[@]}" shell getprop ro.build.version.sdk | tr -d '\r')"
echo "abi=$(adb "${adb_args[@]}" shell getprop ro.product.cpu.abi | tr -d '\r')"
echo "vulkan_level=$(adb "${adb_args[@]}" shell getprop ro.hardware.vulkan.level | tr -d '\r')"
echo "vulkan_version=$(adb "${adb_args[@]}" shell getprop ro.hardware.vulkan.version | tr -d '\r')"

features="$(adb "${adb_args[@]}" shell pm list features | tr -d '\r')"
if ! grep -q "android.hardware.vulkan" <<<"$features"; then
  echo "Android package manager reports no Vulkan feature." >&2
  exit 4
fi
echo "Vulkan capability check passed."
