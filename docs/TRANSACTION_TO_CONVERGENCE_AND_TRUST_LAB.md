# Reality Probe laboratory: revise a world, calculate convergence, then decide trust

Start with [WorldIR transaction](../src/world/transaction.cpp), [verified rewrite](../src/world/verified_rewrite.cpp), [three-level convergence](../src/verify/convergence.cpp) and [result certificate](../src/verify/result_certificate.cpp). The existing [verified rewrite learning path](VERIFIED_REWRITE_LEARNING_PATH.md) connects them to the full captured-world and scientific-evidence pipeline. This lesson adds a numeric example and explicit failure cases.

## 1. A WorldIR hypothesis is more than rendered geometry

WorldIR connects captured Gaussian appearance to entities, correspondence mappings, material/constraint parameters and physical hypotheses. Two scenes can explain the observed image yet make different predictions under a future intervention. Reality Probe therefore asks how a proposed edit changes **independently measurable physical behavior**, not merely whether the new image looks better.

WorldTransaction includes a stable transaction ID, author, expected revision and typed edits. A correspondence graph links an authored Entity to the Gaussian IDs and physical bindings it can legitimately change. A stale expected revision, missing mapped Gaussian or nonfinite edit is a transaction-validation failure before any mutation.

## 2. In-memory transactions and receipts

validateTransaction checks the world, graph and edit targets. applyTransaction first snapshots affected Gaussian positions, entity material parameters and constraint parameters into a TransactionReceipt, then modifies a copy, increments its revision and appends provenance before publishing the candidate by move.

This is *in-memory* atomicity. It does not promise a distributed transaction across GPU buffers, files, solver subprocesses or visualization output.

rollbackTransaction uses the receipt's expected tentative revision. It restores the original touched state rather than assuming the world still matches some earlier default configuration. The revision guard prevents undoing a later unrelated edit with an old receipt.

**Exercise:** start at WorldIR revision 4, apply transaction T with expectedRevision=4, and observe revision 5 with receipt before/after. Attempt rollback after another transaction has already made revision 6. Explain why the old receipt must be rejected.

## 3. Untouched-region drift is keyed by stable Gaussian identity

A GaussianCloud may reorder splats during filtering or GPU upload. Comparing vector position i to vector position i in the next cloud could falsely report a big physical movement when the same stable Gaussian ID just moved to another storage slot.

unaffectedPositionDrift therefore compares Gaussian positions by stable ID across before/after clouds, excluding IDs touched by the transaction. This is one locality test. It is not the image appearance test and not the physical observable error test.

**Exercise:** create three splats A, B, C, translate A, and reorder the post-edit storage as C, A, B. Compare an index-by-index distance against stable-ID matching. Only the latter measures what changed outside the touched set.

## 4. The evidence gates of a verified rewrite

executeVerifiedRewrite validates the edit, records required gates, demands a PhysicalRewriteVerifier before attempting edits that require a physical rerun, then applies the tentative transaction.

Afterward it computes untouched-position drift, verifies required appearance propagation, and invokes the supplied physical verifier when required. The verifier must supply explicit rerun-completed/passed flags, scalar observable error and tolerance, an attributable artifact, and an independent oracle decision when that oracle is required.

The rewrite becomes Verified only when all required checks pass: no validation rejection; finite drift under max allowed; appearance propagation evidence; completed and passing physical rerun with finite bounded error and artifact; passing independent oracle when required.

If any post-edit check fails, the receipt is used to roll the world back and record rejection. A missing verifier does not magically become a pass, and a pretty render does not replace a physical oracle.

**Exercise:** write a mock verifier that reports rerunCompleted=true but artifact empty, or independentOraclePassed=false. Trace the exact failing condition and confirm the world is restored.

## 5. Three-level Richardson-style convergence by hand

Suppose the *same scalar physical observable* is computed at three geometrically refined timesteps/meshes with ratio r=2:

| Level | Observed value |
| --- | ---: |
| Coarse | 1.16 |
| Medium | 1.04 |
| Fine | 1.01 |

The consecutive differences are \(\Delta_c=1.16-1.04=0.12\) and \(\Delta_f=1.04-1.01=0.03\), both with the same sign. The empirical order is

\[
p=\frac{\ln|\Delta_c/\Delta_f|}{\ln r}
 =\frac{\ln 4}{\ln 2}=2.
\]

The extrapolation correction is

\[
\delta=\frac{x_f-x_m}{r^p-1}=\frac{1.01-1.04}{4-1}=-0.01
\]

so the estimated infinite-resolution value is \(x_\infty\approx1.00\).

The implementation estimates relative correction \(|\delta|/\max(|x_f|,10^{-30})\approx0.009901\), or roughly 0.99%. With safetyFactor=3, the inflated estimate is about 2.97%.

These are **conditional estimates**: values must belong to the same observable, use a consistent refinement ratio, avoid nearzero consecutive differences and exhibit monotonic convergence. Even that cannot prove true asymptotic convergence from three samples alone.

**Exercise:** replace the medium value by 1.20. The consecutive differences change sign and the estimator should reject the oscillatory sequence. Another failure is equal consecutive values causing a nearzero denominator.

## 6. Why convergence and observation error are separate

Numerical time-step convergence measures how solver predictions change with discretization. A camera/video track may be temporally downsampled, physically biased or incorrectly calibrated. Reducing solver dt cannot repair incorrectly timed or noisy observations by itself.

A physically plausible fit can overfit one sequence and fail on a held-out intervention. Reality Probe's previously reported pendulum results do **not** imply universal success; free-fall and temporal downsampling limitations must remain visible in the frozen scientific record.

Compare convergence error, observed-versus-predicted residual, independent rerun/oracle error, and held-out generalization as separate columns—not a single “accuracy” score.

## 7. What ResultCertificate can legitimately claim

VerificationCriterion declares threshold direction, observed value and whether it is Required. Nonfinite values fail checks. ResultCertificate::requiredEvidencePasses enforces the mandatory set; optional criteria may be reported but cannot substitute for missing required evidence.

updateTrustState requires both mandatory criteria passing and an indicated complete convergence study before marking the configured numeric certificate Verified. The separate world-rewrite protocol can require additional independent physical oracle evidence, so its verification status is not equivalent to every generic numeric ResultCertificate.

toJson serializes decisions and provenance for audit. JSON formatting or high numeric precision does not prove that measurements were experimentally valid.

**Exercise:** create one required criterion that passes and another required criterion that fails. Set convergenceStudyComplete=true and inspect trust state. Then make both pass but set convergenceStudyComplete=false; verification must still be gated.

## 8. Scientific falsification and tests

Review [verified_rewrite_tests.cpp](../tests/verified_rewrite_tests.cpp), [captured_reality_loop_tests.cpp](../tests/captured_reality_loop_tests.cpp), [timestep_convergence_tests.cpp](../tests/timestep_convergence_tests.cpp) and [metric_video_bounce_tests.cpp](../tests/metric_video_bounce_tests.cpp). Add tests for stale transaction receipts, missing physical verifier, thrown verifier, missing independent oracle, no artifact, invalid Gaussian ID, reordering invariant, oscillatory triplets, and required-evidence failures.

A reproducible paper claim needs exact code SHA, dataset/observation identity, intervention, solver parameters, coarse/medium/fine resolutions, tolerance, oracle independence, machine/backend and the frozen decision ledger. Do not retroactively relabel failed experiments as successes.

*Documentation-only guide. No new physics run, model fitting, convergence sweep or native build was executed during this pass.*
