#!/usr/bin/env bash
set -euo pipefail

# Safe-delete set audited against research/integration-20260920 on 2026-09-20.
# Every branch below had ahead_by=0 at audit time.
#
# Re-run the compare audit before executing this script if the repository has moved
# materially since that date.

branches=(
  "develop/1.1"
  "feat/adaptive-material-influence-regions"
  "feat/captured-adjoint-influence"
  "feat/captured-world-run"
  "feat/reality-loop-worldir"
  "feat/release-hardening-0-90"
  "feat/surface-proxy-showcase-1-0-main"
  "feat/verified-rewrite-transactions"
  "feat/verified-rewrite-transactions-check"
  "fix/video-observation-refusal-outcome"
  "research/constitutive-adequacy"
  "research/convergence-certificate"
  "research/dcs-darkfield"
  "research/discovery"
  "research/fixed-forward-co-refinement"
  "research/fixed-forward-deep-refinement"
  "research/fixed-forward-space-transfer"
  "research/fixture-overlap-heldout"
  "research/gauge-structural-forensics"
  "research/grid-phase-falsification"
  "research/refinement-forensics"
  "research/refusal-validation3"
  "research/refusal-validation4"
  "research/refusal-validation5"
  "research/validation-truth-convergence"
)

printf 'About to delete %d fully-contained remote branches:\n' "${#branches[@]}"
printf '  %s\n' "${branches[@]}"
printf '\nCanonical branches main, release/1.0.0, legacy/studio-v1-2026-08-10, and research/integration-20260920 are NOT touched.\n'

for branch in "${branches[@]}"; do
  git push origin --delete "$branch"
done
