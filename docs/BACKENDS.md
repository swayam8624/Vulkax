# GPU backend policy

## Intent

Vulkan is the primary cross-platform GPU target. Metal is a first-class native Apple path. OpenGL is a
compatibility/fallback rendering path. DirectX is intentionally out of scope.

The runtime must eventually probe actual adapters/devices and report capabilities rather than infer them
only from the operating system.

## Selection policy

A workload declares required features such as compute, storage buffers/images, atomics, subgroup
operations, FP16/FP64, descriptor indexing, timestamp queries, headless execution, or ray queries.

A backend candidate is eligible only if every required feature is present. Eligible candidates are then
scored using API priority, platform-native quality, dedicated-GPU status, driver quality, memory, and
future workload-specific evidence.

Expected common outcomes:

- Apple device with suitable Metal support -> Metal normally wins.
- Linux/Windows with suitable Vulkan support -> Vulkan normally wins.
- Vulkan unavailable/insufficient but OpenGL satisfies the workload -> OpenGL fallback.
- No backend satisfies requirements -> explicit failure, never a silent capability downgrade that changes
  physical semantics.

## Next implementation step

The bootstrap contains policy only. Real probes will be implemented behind backend-specific modules:

```text
backends/vulkan  -> loader/device/features/queues/memory/timestamps
backends/metal   -> MTLDevice/features/families/memory
backends/opengl  -> context/version/extensions/compute capability
```

Vulkan is implemented first. A backend conformance suite will execute the same tiny operator kernels and
compare numerical signatures before any backend is trusted for a solver.
