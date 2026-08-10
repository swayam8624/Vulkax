# GPU backend policy

## Intent

Vulkan is the primary cross-platform GPU target. Metal is a first-class native Apple path. OpenGL is a
compatibility/fallback rendering path. DirectX is intentionally out of scope.

The runtime probes actual devices. Operating-system identity influences selection policy, but it does not
invent a backend or capability that a probe did not report.

## Bootstrap probes

The current bootstrap has two real discovery paths:

- **Vulkan:** creates an instance, enumerates physical devices, checks compute queues, device-local memory,
  selected device extensions, storage-image limits, FP64 support, and compute timestamp availability.
- **Metal:** queries the native default `MTLDevice`, working-set budget, device class, and the core compute /
  storage / atomics / FP16 capability class used by the bootstrap.

OpenGL remains represented in selection policy but is **not** reported as available until a real context
and extension probe is implemented.

## Selection policy

A workload declares required features such as compute, storage buffers/images, atomics, subgroup
operations, FP16/FP64, descriptor indexing, timestamp queries, headless execution, or ray queries.

A backend candidate is eligible only if every required feature is present. Eligible candidates are then
scored using API priority, platform-native quality, dedicated-GPU status, memory, and a probe-supplied
quality estimate. The quality estimate is currently a conservative bootstrap heuristic; it must later be
replaced/augmented by measured conformance and performance evidence.

Expected common outcomes:

- Apple device with suitable Metal support -> Metal normally wins.
- Linux/Windows with suitable Vulkan support -> Vulkan normally wins.
- Vulkan unavailable/insufficient but a future OpenGL probe satisfies the workload -> OpenGL fallback.
- No backend satisfies requirements -> explicit failure, never a silent capability downgrade that changes
  physical semantics.

## Next backend work

```text
backends/vulkan  -> instance/device discovery [started]
                 -> logical device/queues
                 -> allocator/budget tracking
                 -> compute pipelines
                 -> timestamps
                 -> headless conformance kernels

backends/metal   -> device discovery [started]
                 -> command queues/resources/pipelines

backends/opengl  -> context/version/extensions probe
                 -> compatibility rendering only where appropriate
```

A backend conformance suite will execute identical tiny operator kernels and compare numerical signatures
before any backend is trusted for a solver.
