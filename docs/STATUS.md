# Vulkax implementation status

This is the detailed implementation ledger moved from the repository front page. States such as `Implemented foundation` and `Implemented research path` are intentionally more conservative than production-readiness claims.

| Component | State |
| --- | --- |
| Canonical C++ equation parsing, conservative interval/singularity analysis, AST evaluation, built-in presets, raw analytical results | Implemented |
| Native Swift equation editor bridged to the canonical C++ parser over a small C ABI; shared parameter ordering, diagnostics, AST hash and Metal source | Implemented |
| Equation-aware simulation-medium inference with confidence/reasons and persisted Auto/manual override | Implemented |
| Qt 6 editor, parameterized dynamic graphs, project I/O, timeline, PNG/sequence export | Implemented |
| Native macOS three-pane scene workspace with scene roles, inspector, timeline and live Metal viewport | Implemented |
| `.vxp` project format v9 with backward loading for v1–v9, scene entities, proxy roles, camera director track and capture settings | Implemented |
| Executable scalar Physics IR with CPU interpreter, CSE, constant folding, GLSL/MSL emission and Vulkan/Metal agreement | Implemented foundation |
| Executable vector2/vector3 compute IR with CPU execution, canonical hashing and GLSL/MSL emission | Implemented foundation |
| Executable dense 3×3 tensor compute IR as nine canonical scalar component programs with CPU oracle, shared parameters/domain, canonical hashing and GLSL/MSL emission | Implemented foundation |
| Scalar evolution/stencil IR with Laplacian and directional gradients, explicit stepping, open/periodic/fixed boundaries, CPU oracle, generated SPIR-V/MSL and Vulkan agreement | Implemented foundation |
| Generated constant-velocity transport/diffusion planner with optional source, existing stencil-IR lowering, advection-CFL and explicit diffusion stability limits | Implemented bounded solver-generation path |
| Coupled scalar evolution IR with cross-field stencils, simultaneous old/new state semantics, per-field boundaries, generated SPIR-V/MSL and CPU/Vulkan Gray-Scott agreement | Implemented foundation |
| Persistent editor Wave Field and Gray-Scott Vulkan compute, plus headless wave/N-body CPU/GPU agreement | Implemented |
| Linear OpenEXR preview export and HDR quality tooling | Implemented |
| Typed physics IR, explicit equation-defined pass graphs, reflected resource layouts, automatic Vulkan buffer/image allocation, shared-resource imports, descriptor writes, pipeline layouts, history rotation/pass barriers, persistent pipeline artifacts and executable scalar-field programs | Implemented foundation |
| Reusable `vulkax_gpu` compute context and Vulkax-owned direct-presentation boundary | Implemented foundation |
| Direct macOS Metal viewport with GPU-resident HDR radiance and presentation | Implemented |
| Direct Vulkan/MoltenVK compute-to-swapchain field presenter with no CPU image bridge | Implemented and smoke-tested |
| Direct Vulkan Release validation-layer CI across Wave, Schwarzschild, Kerr and Volume modes on Lavapipe | Implemented |
| Direct Vulkan/MoltenVK Schwarzschild GPU preview with timestamped compute-to-present path | Implemented foundation |
| Direct Vulkan Schwarzschild linear HDR OpenEXR capture with content validation | Implemented and tested |
| CPU Kerr/Carter reference with null-constraint telemetry, curvature-driven Jacobi/geodesic-deviation transport, root-refined multi-intersection events, finite-thickness disk/corona transfer, adaptive central Vulkan ray, progressive HDR and measured EXR quality | Implemented research foundation |
| GPU active-ray integrate/scan/scatter compaction with indirect next-dispatch generation | Implemented and Vulkan-tested |
| Wave-field direct Vulkan HDR OpenEXR export | Implemented |
| Schwarzschild CPU reference, Vulkan fixed-step compute check and interactive Metal 3D orbital-plane visual | Implemented foundation |
| Schwarzschild thin-disk lensing and 2D buoyant-smoke equation suites | Implemented and tested |
| Interactive GPU 3D MAC smoke with staggered face velocity, RK2/MacCormack transport, obstacle-aware two-level multigrid projection, curl, temperature and volume ray marching | Implemented research foundation |
| Portable Vulkan MAC projection, GPU CFL, solid obstacles, curl/vorticity, two-level pressure correction, RK2/MacCormack transport, density hierarchy, self-shadowed HDR volume march, timestamps and persisted pipeline cache | Implemented foundation |
| Multi-object imported-mesh airflow coupling with quaternion rigid state, Metal/Vulkan voxelization, GPU pressure force/torque, angular integration, moving boundaries, restitution/friction contacts and reflected graph passes | Implemented research path |
| Visual-mesh versus physics-proxy scene boundary, including automatic conservative bounds proxy for unsuitable car/prop topology | Implemented |
| First-class OBJ/glTF 2.0/GLB static scene import with transforms, triangle primitives, normals, UVs and multi-material geometry | Implemented |
| glTF PBR scene rendering with base-color, metallic-roughness and normal textures/factors, emissive factor, derivative tangent frame and GGX/Smith/Schlick shading | Implemented foundation |
| Runtime-gated macOS Model I/O static compatibility adapter for formats reported importable by the installed framework; guaranteed path regression-tested with PLY | Implemented compatibility path |
| Camera orbit/pan/dolly/presets/FOV/exposure shared by simulation, scene rendering and capture | Implemented |
| Persisted director camera keyframes with deterministic interpolation in preview and offline capture | Implemented |
| Deterministic 1080p/4K UHD HEVC `.mov` capture using Metal-backed CoreVideo buffers and exact frame cadence | Implemented and tested |
| Linear-HDR EXR MSE, PSNR, global SSIM, maximum error, temporal difference, 1/4/16-sample convergence and golden-image gates | Implemented |
| Adaptive preview quality controller with Fixed, Screen-space-only, Numerical-only and Equation-aware policies; missing evidence represented explicitly rather than as zero | Implemented foundation |
| Raw four-policy quality benchmark with screen-space RMS and numerical MSE evidence | Implemented |
| Linux Vulkan/Lavapipe, native macOS Metal and Windows UCRT64 product CI; ASan/UBSan, clang-tidy and validation-layer analysis | Implemented |
| Root CMake identity `Vulkax` while preserving `LveEngine` as the legacy compatibility executable | Implemented |

## Claim boundaries

The Interstellar reference is an inspiration for visual rigor, not a claim of parity with DNEG's DNGR production renderer. Vulkax should continue to state whether a visualization is analytical, numerically validated, real-time approximate or offline reference.

The generated transport path is intentionally bounded to explicit constant-velocity scalar advection/diffusion plus optional source; it is not a universal symbolic PDE solver. First-class glTF support is for static triangle scenes; sparse accessors, skinning, morph targets and animation are not production features. Model I/O formats are static compatibility fallbacks and remain runtime-dependent.