#!/usr/bin/env zsh
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
package="$root/apps/VulkaxPhysicsStudioMac"
cd "$package"
swift build -c release

binary="$package/.build/release/VulkaxPhysicsStudioMac"
for argument in "$@"; do
  case "$argument" in
    --native-gpu-smoke|--native-*-gpu-smoke)
      exec "$binary" "$@"
      ;;
  esac
done

bundle="$package/.build/release/Vulkax Physics Studio.app"
mkdir -p "$bundle/Contents/MacOS" "$bundle/Contents/Resources"
cp "$package/Resources/Info.plist" "$bundle/Contents/Info.plist"
cp "$binary" "$bundle/Contents/MacOS/VulkaxPhysicsStudioMac"
exec open -W -n -a "$bundle" --args "$@"
