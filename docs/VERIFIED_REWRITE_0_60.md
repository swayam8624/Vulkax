# Vulkax 0.60 — verified rewrite transactions

Vulkax 0.60 makes a local rewrite a transaction whose `verified` state is **derived from evidence**. Callers cannot set verification status directly.

## Transaction model

The world transaction layer supports three typed rewrite classes required by the 1.0 roadmap:

- `TranslateEntity` for local appearance geometry translation;
- `SetMaterialParameter` for entity-local material metadata;
- `SetConstraintParameter` for supported constraint/boundary-condition metadata.

Before any mutation, the transaction validates the transaction ID, author, summary, expected world revision, target entities, finite edit values, correspondence graph, required appearance correspondence, and required physical correspondence. Transaction IDs already present in provenance are rejected.

The low-level commit is copy-then-commit: snapshots are collected first, edits are applied to a candidate `WorldIR`, revision/provenance are appended to the candidate, and the candidate is moved into the live world only after the whole edit set succeeds. A later invalid edit therefore cannot leave earlier edits partially applied.

Rollback receipts now preserve Gaussian positions, material maps, constraint maps, revision, and provenance.

## Evidence-derived verification

`executeVerifiedRewrite` sits above the low-level transaction layer. It derives requirements from the edit set:

- geometry edits require appearance-propagation/locality evidence and require a physical rerun when the entity has physical bindings;
- material edits require a physical rerun **and an independent oracle**;
- constraint edits require a physical rerun when physical correspondence exists.

For physical rewrites, a verifier must provide completed/passed rerun evidence, a finite observable error and tolerance with `error <= tolerance`, and a non-empty artifact identity. Material rewrites additionally require completed/passed independent-oracle evidence.

The executor also measures unaffected-region Gaussian positional drift against policy. If any required post-commit evidence is missing or fails, the executor automatically rolls the transaction back. The returned status remains `rejected`; no API exists to force it to `verified`.

`transaction_evidence.csv` records the derived verification flags, locality evidence, physical-rerun/oracle state, scalar error/tolerance, artifact identity, verifier summary and rejection reason.

## Controlled captured-material adapter

0.60 contains one concrete solver-backed adapter: `makeCapturedMaterialRewriteVerifier` for a local captured APIC/MPM Young's-modulus rewrite.

The adapter:

1. accepts exactly one `young_modulus` edit;
2. derives the rewritten region from the semantic entity's stable MPM-particle bindings;
3. checks that the transaction's pre-rewrite Young's modulus matches the physical baseline;
4. checks that the requested `new_E / old_E - 1` exactly matches the nonlinear verification perturbation, preventing evidence from one rewrite magnitude from being attached to another;
5. runs the retained central finite-difference material derivative reference;
6. runs the separate nonlinear counterfactual perturbation for the exact requested rewrite magnitude;
7. runs the controlled APIC reverse-mode material adjoint;
8. compares adjoint and independent finite-difference derivatives;
9. writes `reference.csv`, `counterfactual.csv`, `adjoint.csv` and `derivative_comparison.csv`;
10. returns those results to the central transaction executor, which alone derives commit/reject status.

The controlled regression deliberately tries to reuse +2% evidence for a +1% transaction. The verifier rejects the mismatch and the transaction envelope restores material metadata, revision and provenance.

## Public controlled command

After generating the deterministic captured bundle, the public tool can exercise the complete transaction path:

```bash
./build/vulkax_captured_rewrite \
  build/captured-example/capture.vkcap \
  build/captured-verified-rewrite \
  m4 0.003 1 1 1 \
  43,44,47,48,59,60,63,64 \
  15000 0.30 0.08 0.01 0.02
```

This requests a +2% Young's-modulus rewrite (`15000 Pa -> 15300 Pa`) for the eight stable particle IDs in the controlled positive rest-space octant. Linux CI requires the command to return `status: verified`, commit the transaction without rollback, and emit:

```text
transaction_evidence.csv
transaction_summary.csv
physical_evidence/reference.csv
physical_evidence/counterfactual.csv
physical_evidence/adjoint.csv
physical_evidence/derivative_comparison.csv
```

The public gate requires all rerun/oracle flags, `physical_observable_error <= physical_observable_tolerance`, exactly eight selected particles, the expected 15000-to-15300 material change, and no rollback.

## Scope and limitations

This milestone establishes the central **transaction/evidence/rollback semantics** and one concrete solver-backed material adapter on the controlled captured APIC path.

It does **not** establish the following:

- a real measured-data rewrite result; 0.45 remains external-data-blocked;
- a solver-backed captured geometry verifier beyond the generic verifier interface and controlled transaction tests;
- a solver-backed captured constraint/boundary verifier beyond the generic verifier interface and controlled transaction tests;
- topology cutting, remeshing or fracture transactions;
- general differentiable MPM through FLIP blending, boundary clamps or arbitrary forcing;
- automatic region selection as verification: proposal generation and verification remain separate.

The material adapter inherits the controlled APIC derivative scope already documented by Vulkax. Finite-difference and nonlinear rerun paths remain verification oracles; they are not removed after the adjoint agrees.
