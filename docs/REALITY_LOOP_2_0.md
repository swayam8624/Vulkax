# Executable WorldIR and the Vulkax reality loop

This document defines the first post-1.0 architectural step toward Vulkax as a closed-loop computational world engine. It does **not** change the evidence claims frozen for Vulkax 1.0. The existing captured-world benchmark, renderer evidence, measured DOT C2 artifacts, transaction semantics and verification certificates remain the source of truth for those claims.

## Why this exists

Before this change, `WorldIR` was principally a persistent semantic/appearance container used by stable-ID correspondence and verified local rewrite. That is necessary for rewritable captured worlds, but it is not yet an executable hypothesis of reality.

A closed loop needs one object to carry, without silently mixing evidence classes:

1. the persistent Gaussian appearance and entities already present in Vulkax;
2. an optional physics `ProblemIR` and its cyclic operator structure;
3. measured, derived, proxy or synthetic observations, including uncertainty;
4. the parameters the model is allowed to infer, including hard bounds and provenance;
5. global parameters that are not naturally owned by one entity.

`WorldIR` schema version 2 adds exactly that state while leaving the 1.0 transaction-facing fields intact.

## Evidence classes

Every observation and parameter belief can be labelled as `Measured`, `Derived`, `ModelProxy`, `LiteratureProxy`, `Synthetic` or `Unknown`. This is intentionally explicit. A synthetic test target cannot accidentally become a measured result simply because it passed through the same optimizer as camera-derived evidence.

`validateHypothesis()` rejects duplicate entity/observation identities, invalid parent references, non-finite parameters, malformed observation uncertainties, inverted parameter bounds, missing addressed parameters, and values outside declared beliefs.

## Addressed parameters

The inverse layer does not keep a second hidden copy of material state. A `ParameterAddress` points at an existing material, constraint or global scalar in `WorldIR`; `parameterValue()` and `setParameterValue()` resolve the live state. `ParameterBelief` adds bounds, uncertainty and provenance around that addressed value.

This matters for verified rewrite: inference should eventually propose a normal `WorldTransaction`, not mutate a disconnected optimization vector and hope it still refers to the same world.

## Generic forward model

`world/reality_loop.hpp` defines a `ForwardModel` callback:

```cpp
std::vector<ObservationPrediction>(const WorldIR& candidate);
```

The callback is intentionally solver-agnostic. Today it can wrap an analytic test, the existing captured MPM replay, FEM, a Gaussian renderer or another Vulkax execution path. Later it can wrap a reconstruction/photometric pipeline. The inverse layer only requires that predictions retain the stable observation IDs and component dimensions declared by the world.

## Closed-loop inverse fitting

`fitWorldHypothesis()` minimizes uncertainty-weighted observation residuals with a damped finite-difference Gauss-Newton loop:

\[
r_i(\theta)=\frac{\hat y_i(\theta)-y_i}{\sigma_i},
\qquad
L(\theta)=\frac12\sum_i r_i^2.
\]

A central finite-difference numerical oracle estimates the residual Jacobian while respecting declared parameter bounds. Each iteration solves a damped normal system and performs backtracking before accepting a world update. Trial states are clamped to the parameter beliefs, so an inverse fit cannot silently leave a declared physical/experimental range.

Finite differences are deliberate at this layer even though Vulkax already contains adjoint work. They provide an independent reference path against which future analytic/reverse derivatives can be checked.

## Local sensitivity versus causality

`rankParameterSensitivity()` reports local numerical sensitivity of weighted predictions and the current objective. It is useful for deciding which parameters are capable of explaining a mismatch near the current state, but it is **not** labelled causal identification.

Likewise `OperatorGraph::traceUpstream()` now walks the cyclic bipartite field/operator graph backward from an observable field. It returns every structurally reachable upstream operator once and terminates cycles with visited sets. The result means "this mechanism can structurally influence this field according to the declared model," not "this mechanism caused the real-world event."

Formal causal claims require interventions, identifiability assumptions and evidence that this structural trace alone does not provide.

## Tests added with this slice

The reality-loop test target exercises:

- executable `WorldIR` validation and uncertainty rejection;
- addressed parameter access and hard belief bounds;
- recovery of two coupled unknown parameters from two weighted observations;
- convergence to an active bound when the target is unreachable;
- ranking an actually influential parameter above an unused one;
- upstream traversal of a cyclic operator graph without infinite recursion or disconnected contamination.

## What this does not claim yet

This slice is infrastructure for the reality compiler; it is not the completed reality compiler. In particular it does not yet establish:

- video-to-3D Gaussian reconstruction inside Vulkax;
- camera calibration, tracking, segmentation or optical-flow ingestion;
- real material recovery from arbitrary Internet footage;
- a differentiable Gaussian photometric renderer connected to the inverse loop;
- general multi-physics coupling through the new loop;
- formal causal identification;
- automatic verified acceptance of an inferred rewrite.

Those capabilities must be connected with their own measured evidence rather than inferred from the existence of a generic optimizer.

## Next integration seam

The next high-value adapter is the existing captured-deformable path. A captured bundle can populate `ObservationRecord`s; its MPM replay can become the `ForwardModel`; the selected material parameters can become addressed beliefs; and the final candidate can be proposed through the existing verified transaction/rollback machinery.

After that adapter is measured end-to-end, a video observation producer can be introduced. Any public clip used for repository evidence should be license-safe, pinned by URL and content hash, and recorded in the evidence registry so the source cannot silently drift.

The intended architecture is therefore:

```text
Observation producer
        |
        v
Executable WorldIR -- ProblemIR / operator graph
        |                         |
        |                         +--> structural upstream trace
        v
Forward model (MPM/FEM/render/reconstruction)
        |
        v
weighted mismatch
        |
        +--> bounded inverse fit
        +--> local parameter sensitivity
        |
        v
candidate WorldIR
        |
        v
existing verified rewrite / rollback / certificate path
```

That closes the first architectural gap between Vulkax 1.0's verified rewritable captured world and the longer-term observe -> infer -> simulate -> compare -> correct loop.
