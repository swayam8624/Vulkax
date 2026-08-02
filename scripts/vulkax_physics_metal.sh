#!/usr/bin/env zsh
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
package="$root/apps/VulkaxPhysicsStudioMac"
arguments=("$@")
for ((index = 1; index <= ${#arguments}; ++index)); do
  if [[ "${arguments[$index]}" == "--native-imported-mesh-gpu-smoke" &&
        $((index + 1)) -le ${#arguments} ]]; then
    mesh_path="${arguments[$((index + 1))]}"
    if [[ "$mesh_path" != /* ]]; then
      arguments[$((index + 1))]="${mesh_path:A}"
    fi
  fi
done
cd "$package"
swift build -c release

binary="$package/.build/release/VulkaxPhysicsStudioMac"
for argument in "${arguments[@]}"; do
  case "$argument" in
    --native-gpu-smoke|--native-*-gpu-smoke)
      exec "$binary" "${arguments[@]}"
      ;;
  esac
done

bundle="$package/.build/release/Vulkax Physics Studio.app"
mkdir -p "$bundle/Contents/MacOS" "$bundle/Contents/Resources"
cp "$package/Resources/Info.plist" "$bundle/Contents/Info.plist"
cp "$binary" "$bundle/Contents/MacOS/VulkaxPhysicsStudioMac"
exec open -W -n -a "$bundle" --args "${arguments[@]}"
