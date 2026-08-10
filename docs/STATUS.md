# Vulkax implementation status

This is the detailed implementation ledger moved from the repository front page. States such as `Implemented foundation` and `Implemented research path` are intentionally more conservative than production-readiness claims.


| Component | State |
| --- | --- |
| Equation parsing, conservative interval/singularity analysis, AST evaluation, built-in presets, raw analytical results | Implemented |
| Qt 6 macOS editor, parameterized dynamic graphs, project I/O, timeline, PNG/sequence export | Implemented |
| Executable scalar Physics IR with CPU interpreter, common-subexpression elimination, constant folding, GLSL/MSL emission, and Vulkan/Metal wave agreement | Implemented foundation |
| Scalar evolution/stencil IR with Laplacian and directional gradients, explicit time stepping, open/periodic/fixed boundaries, CPU oracle, generated SPIR-V/MSL, and Vulkan agreement | Implemented foundation |
| Coupled scalar evolution IR with cross-field stencils, simultaneous old/new state semantics, per-field boundaries, generated SPIR-V/MSL, and CPU/Vulkan Gray-Scott agreement | Implemented foundation |
| Persistent editor Wave Field and Gray-Scott Vulkan compute, plus headless wave/N-body CPU/GPU agreement | Implemented |
| Linear OpenEXR preview export | Implemented |
| Typed physics IR, explicit equation-defined pass graphs, reflected resource layouts, automatic Vulkan buffer/image allocation, shared-resource imports, descriptor writes, pipeline layouts, history rotation and pass barriers, persistent pipeline artifacts, and executable scalar-field programs | Implemented foundation; generated stencil and editor wave/HDR paths migrated |
| Direct macOS Metal viewport with GPU-resident HDR radiance and presentation | Implemented |
| Direct Vulkan/MoltenVK compute-to-swapchain field presenter with no CPU image bridge | Implemented and smoke-tested on Apple M2 Pro |
| Direct Vulkan/MoltenVK Schwarzschild GPU preview with timestamped compute-to-present path | Implemented foundation and smoke-tested on Apple M2 Pro |
| Direct Vulkan Schwarzschild linear HDR OpenEXR capture with content validation | Implemented and tested |
| CPU Kerr/Carter reference with null-constraint telemetry, curvature-driven Jacobi/geodesic-deviation transport, root-refined multi-intersection events, finite-thickness disk/corona transfer, adaptive central Vulkan ray, progressive HDR, and measured EXR quality | Implemented research foundation and smoke-tested on Apple M2 Pro |
| GPU active-ray integrate/scan/scatter compaction with indirect next-dispatch generation | Implemented and Vulkan-tested on Apple M2 Pro |
| Wave-field direct Vulkan HDR OpenEXR export | Implemented |
| Schwarzschild CPU reference, Vulkan fixed-step compute check, and interactive Metal 3D orbital-plane visual | Implemented foundation |
| Schwarzschild thin-disk lensing and 2D buoyant-smoke equation suites | Implemented and tested |
| Interactive GPU 3D MAC smoke with staggered face velocity, RK2/MacCormack transport, obstacle-aware two-level multigrid projection, curl, temperature, and volume ray marching | Implemented research foundation |
| Portable Vulkan MAC projection, GPU CFL, solid obstacles, curl/vorticity, two-level pressure correction, RK2/MacCormack transport, density hierarchy, self-shadowed HDR volume march, timestamps, and persisted pipeline cache | Implemented foundation |
| Imported-mesh airflow coupling with multi-object project records, persisted transforms, quaternion rotation, Metal/Vulkan voxelization, GPU pressure force/torque, angular integration, moving boundaries, restitution/friction contacts, editor controls, and reflected graph passes | Implemented research path |
| Linear-HDR EXR MSE, PSNR, global SSIM, maximum error, temporal difference, 1/4/16-sample convergence, and golden-image gates | Implemented |
| Adaptive preview quality controller with live analytical MSE and raw benchmark | Implemented foundation |

The Interstellar reference is an inspiration for visual rigor, not a claim of parity with DNEG's
DNGR production renderer. Vulkax will state whether a visualization is analytical, numerically
validated, real-time approximate, or offline reference.
