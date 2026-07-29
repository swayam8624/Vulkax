#!/usr/bin/env zsh
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
package="$root/apps/VulkaxPhysicsStudioMac"
cd "$package"
swift build -c release
exec .build/release/VulkaxPhysicsStudioMac "$@"
