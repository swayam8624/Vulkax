#!/usr/bin/env bash
set -euo pipefail

# Generated from research/state/BRANCH_RETIREMENT_2026-09-20.md.
# Every branch in this list had ahead_by=0 relative to research/dcs-darkfield
# when the manifest was created.

branches=(
  "research/constitutive-adequacy"
  "research/convergence-certificate"
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
  "develop/1.1"
  "feat/adaptive-material-influence-regions"
  "feat/captured-adjoint-influence"
  "feat/captured-world-run"
  "feat/reality-loop-worldir"
  "feat/release-hardening-0-90"
  "feat/surface-proxy-showcase-1-0-main"
  "feat/verified-rewrite-transactions"
  "feat/verified-rewrite-transactions-check"
)

for branch in "${branches[@]}"; do
  git push origin --delete "$branch"
done
