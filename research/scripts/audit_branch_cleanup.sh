#!/usr/bin/env bash
set -euo pipefail

canonical="origin/research/integration-20260920"
delete=0

if [[ "${1:-}" == "--delete" ]]; then
  delete=1
elif [[ $# -gt 0 ]]; then
  echo "usage: $0 [--delete]" >&2
  exit 2
fi

git fetch origin --prune

if ! git rev-parse --verify "$canonical" >/dev/null 2>&1; then
  echo "missing canonical ref: $canonical" >&2
  exit 1
fi

protected=(
  "main"
  "release/1.0.0"
  "legacy/studio-v1-2026-08-10"
  "research/integration-20260920"
)

is_protected() {
  local needle="$1"
  for p in "${protected[@]}"; do
    [[ "$needle" == "$p" ]] && return 0
  done
  return 1
}

safe=()
divergent=()

while IFS= read -r ref; do
  branch="${ref#origin/}"
  [[ "$branch" == "HEAD" ]] && continue
  is_protected "$branch" && continue

  ahead="$(git rev-list --count "$canonical..$ref")"
  behind="$(git rev-list --count "$ref..$canonical")"

  if [[ "$ahead" -eq 0 ]]; then
    safe+=("$branch")
    printf 'SAFE      %-55s ahead=%-4s behind=%s\n' "$branch" "$ahead" "$behind"
  else
    divergent+=("$branch")
    printf 'DIVERGENT %-55s ahead=%-4s behind=%s\n' "$branch" "$ahead" "$behind"
  fi
done < <(git for-each-ref --format='%(refname:short)' refs/remotes/origin | sort)

echo
echo "safe-delete: ${#safe[@]}"
echo "divergent:   ${#divergent[@]}"
echo "protected:   ${#protected[@]}"

if [[ "$delete" -eq 0 ]]; then
  echo
  echo "Dry-run only. Re-run with --delete to delete branches still classified SAFE."
  exit 0
fi

echo
echo "Deleting only currently verified SAFE branches..."
for branch in "${safe[@]}"; do
  echo "DELETE $branch"
  git push origin --delete "$branch"
done

echo "Done."
