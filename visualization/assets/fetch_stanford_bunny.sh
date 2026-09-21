#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
ASSET_ROOT="${VULKAX_ASSET_ROOT:-$ROOT/build/assets/stanford-bunny}"
ARCHIVE="$ASSET_ROOT/bunny.tar.gz"
EXTRACT="$ASSET_ROOT/extracted"
PLY="$EXTRACT/bunny/reconstruction/bun_zipper_res2.ply"
URL_HTTPS="https://graphics.stanford.edu/pub/3Dscanrep/bunny.tar.gz"
URL_HTTP="http://graphics.stanford.edu/pub/3Dscanrep/bunny.tar.gz"

mkdir -p "$ASSET_ROOT"

if [ ! -s "$PLY" ]; then
  echo "Fetching Stanford Bunny from the Stanford 3D Scanning Repository..." >&2
  if [ ! -s "$ARCHIVE" ]; then
    if ! curl -L --fail --retry 3 --retry-delay 2 "$URL_HTTPS" -o "$ARCHIVE"; then
      echo "HTTPS fetch failed; retrying Stanford's HTTP endpoint..." >&2
      curl -L --fail --retry 3 --retry-delay 2 "$URL_HTTP" -o "$ARCHIVE"
    fi
  fi
  rm -rf "$EXTRACT"
  mkdir -p "$EXTRACT"
  tar -xzf "$ARCHIVE" -C "$EXTRACT"
fi

if [ ! -s "$PLY" ]; then
  echo "error: expected Stanford Bunny reconstruction was not extracted: $PLY" >&2
  exit 1
fi

SHA="$(shasum -a 256 "$PLY" | awk '{print $1}')"
cat > "$ASSET_ROOT/ATTRIBUTION.txt" <<EOF
Stanford Bunny
Source: Stanford University Computer Graphics Laboratory
Repository: https://graphics.stanford.edu/data/3Dscanrep/
Archive: $URL_HTTPS
Local mesh: bunny/reconstruction/bun_zipper_res2.ply
SHA-256: $SHA

Use in VULKAX:
Research/paper visualization carrier only. The bunny mesh is deformed by the
exported VULKAX solver displacement field; it is not claimed to be the frozen
benchmark geometry. Publication images must credit the Stanford Computer
Graphics Laboratory.

Stanford repository terms state that the models may be used for research,
may be redistributed for free, and images made from them may be published in
scholarly articles/books with credit to the Stanford Computer Graphics Laboratory.
EOF

printf '%s\n' "$PLY"
