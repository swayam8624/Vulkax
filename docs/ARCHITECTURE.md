# Architecture

## Dependency direction

```text
core/units
    |
problem/ProblemIR <------ operators/OperatorGraph
    |                              |
    +---------- compiler ----------+
                    |
                numerics
                    |
            solver backends
                    |
               execution
                    |
          verification/certificates
                    |
        analysis / optimization
                    |
             visualization
                    |
                 studio
```

Rendering and UI are downstream consumers. They cannot add hidden governing equations or silently change
solver semantics.

## ProblemIR

The current bootstrap representation already distinguishes domains, fields, residual operators,
materials, boundary conditions, objectives, accuracy targets, and compute budgets. Geometry,
constitutive models, observations, contacts, couplings, discretization plans, and uncertainty models are
next additions.

The key choice is **residual-operator decomposition**. Instead of storing “Navier-Stokes mode,” the model
stores mechanisms whose residual contributions can later be discretized, solved, differentiated, and
attributed independently.

## Operator graph

The operator graph is deliberately not a DAG. Coupled physics can be cyclic. A DAG requirement would
encode an execution/solver order into the physical model. Solver planning is responsible for deciding
splitting, monolithic coupling, iteration, or time integration.

## Verification

A result has a trust state:

- `Preview`: useful visualization, no verification claim.
- `Converging`: evidence is being accumulated or a refinement study is incomplete.
- `Verified`: all required criteria pass and the declared convergence evidence is complete.
- `Rejected`: a required criterion fails.

The certificate design will expand to carry conservation metrics, residual histories, discretization
uncertainty, solver/build hashes, device/backend identity, and reproducibility metadata.

## Backend boundary

Problem semantics are independent of GPU API. Backends advertise capabilities. A workload states its
requirements. Policy selects the highest-scoring valid backend.

The intended order is not a blind hard-coded fallback chain. Vulkan is the primary cross-platform target,
Metal is a first-class Apple backend, and OpenGL is a compatibility renderer. Actual runtime probes must
report feature/device/driver capability; backend policy then chooses among valid candidates.

## Architectural rejection tests

A change is rejected if it requires statements such as:

```text
if (mode == Smoke) ...
if (mode == Car) ...
if (mode == BlackHole) ...
```

at an architectural layer.

The acceptable forms are things such as:

```text
for each residual operator ...
for each field on this domain ...
for each boundary condition ...
for each required backend feature ...
```
