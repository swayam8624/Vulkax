# GAUGE released-asset geometry gate

This gate retires the marker-span-derived prism **only if** GAUGE's own released geometry supplies a better-defined physical domain.

Inputs:
- `assets/obj/foam.obj`
- `assets/mjcf/foam.xml`
- `assets/usd/foam shearing.usd`
- measured GAUGE foam-shearing material metadata

Rules:
1. No trajectory error may influence mesh selection, units, scale, material parameters, gravity, or boundary conditions.
2. Raw assets remain ephemeral in CI; Vulkax stores hashes and derived geometry/provenance only.
3. Closed OBJ signed volume is preferred; bounding-box volume is an explicit fallback.
4. Mass/density provides an independent physical-volume consistency check.
5. MJCF scale/orientation/reference attributes are recorded before interpretation.
6. The task-specific USD is hashed as the authoritative scene reference; scene parsing is a later gate.
7. Released-geometry forward simulation must still beat the frozen affine null on both face-area and marker-position evidence before inverse fitting is considered.

This is model-definition repair, not parameter fitting.
