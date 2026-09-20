#!/usr/bin/env python3
"""Compare released GAUGE foam mesh aspect against the marker-span prism proxy.

The released OBJ is used only for unit-free bounding-box aspect ratios. Absolute
scale comes from GAUGE mass/density volume, so unknown OBJ authoring units cannot
silently enter the physics. No trajectory error or Vulkax prediction is consulted.
"""
import argparse
import hashlib
import json
import math
import pathlib
import statistics


def sha256(path):
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def read_obj_vertices(path):
    vertices = []
    with path.open("r", errors="strict") as f:
        for line in f:
            if not line.startswith("v "):
                continue
            fields = line.split()
            if len(fields) < 4:
                raise RuntimeError("malformed OBJ vertex")
            p = tuple(float(fields[i]) for i in range(1, 4))
            if not all(math.isfinite(x) for x in p):
                raise RuntimeError("non-finite OBJ vertex")
            vertices.append(p)
    if len(vertices) < 8:
        raise RuntimeError("GAUGE foam OBJ has too few vertices")
    return vertices


def bbox(points):
    lo = [min(p[a] for p in points) for a in range(3)]
    hi = [max(p[a] for p in points) for a in range(3)]
    span = [hi[a] - lo[a] for a in range(3)]
    if not all(x > 0.0 and math.isfinite(x) for x in span):
        raise RuntimeError("degenerate geometry bounding box")
    return lo, hi, span


def trial_marker_span(path):
    d = json.loads(path.read_text())
    foam = d["foam"]
    ids = sorted(foam)
    pts = [
        tuple(float(foam[mid][axis][0]) * 1.0e-3 for axis in ("x", "y", "z"))
        for mid in ids
    ]
    return bbox(pts)[2]


def axis_mapping(mesh_span, marker_span):
    """Map mesh bbox axes onto marker axes without using trajectory error."""
    mesh_order = sorted(range(3), key=lambda a: mesh_span[a])
    marker_order = sorted(range(3), key=lambda a: marker_span[a])
    mapping = {}
    for ma, oa in zip(mesh_order, marker_order):
        mapping[ma] = oa
    return mapping


def volume_scaled_prism(mesh_span, volume_m3, mapping):
    raw_box_volume = math.prod(mesh_span)
    scale = (volume_m3 / raw_box_volume) ** (1.0 / 3.0)
    mesh_scaled = [x * scale for x in mesh_span]
    marker_axes = [0.0, 0.0, 0.0]
    for mesh_axis, marker_axis in mapping.items():
        marker_axes[marker_axis] = mesh_scaled[mesh_axis]
    return scale, marker_axes


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", required=True)
    ap.add_argument("--out", required=True)
    args = ap.parse_args()

    root = pathlib.Path(args.root)
    out = pathlib.Path(args.out)
    out.mkdir(parents=True, exist_ok=True)

    obj_path = root / "assets" / "foam.obj"
    metadata_path = root / "metadata" / "foam shearing.json"
    vertices = read_obj_vertices(obj_path)
    raw_lo, raw_hi, raw_span = bbox(vertices)
    metadata = json.loads(metadata_path.read_text())

    result = {
        "schema": "vulkax.gauge_foam_asset_geometry",
        "version": 1,
        "provenance": "released-asset+measured-metadata+measured-marker-geometry",
        "fit_performed": False,
        "simulation_performed": False,
        "asset": {
            "path": "assets/obj/foam.obj",
            "sha256": sha256(obj_path),
            "vertex_count": len(vertices),
            "raw_bbox_min": raw_lo,
            "raw_bbox_max": raw_hi,
            "raw_bbox_span": raw_span,
            "raw_span_normalized_to_longest": [
                x / max(raw_span) for x in raw_span
            ],
            "unit_policy": (
                "OBJ absolute units are ignored. Only bbox aspect ratios are used; "
                "absolute volume is fixed by GAUGE mass/density metadata."
            ),
        },
        "materials": {},
    }

    for material in ("soft", "hard"):
        spans = [
            trial_marker_span(
                root / "data" / "foam shearing" / material / f"{trial}.json"
            )
            for trial in range(1, 11)
        ]
        median_marker_span = [
            statistics.median(s[a] for s in spans) for a in range(3)
        ]
        mapping = axis_mapping(raw_span, median_marker_span)

        mat = metadata["assets"]["foam"]["material"][material]
        mass = float(mat["mass"])
        density = float(mat["density"])
        volume = mass / density
        raw_to_m, physical_span = volume_scaled_prism(raw_span, volume, mapping)

        coverage = [
            median_marker_span[a] / physical_span[a] for a in range(3)
        ]
        margin = [
            0.5 * (physical_span[a] - median_marker_span[a]) for a in range(3)
        ]
        long_axis = max(range(3), key=lambda a: median_marker_span[a])

        result["materials"][material] = {
            "mass_kg": mass,
            "density_kg_m3": density,
            "volume_m3": volume,
            "median_marker_bbox_span_m": median_marker_span,
            "marker_long_axis": "xyz"[long_axis],
            "mesh_axis_to_marker_axis": {
                "xyz"[mesh_axis]: "xyz"[marker_axis]
                for mesh_axis, marker_axis in mapping.items()
            },
            "bbox_aspect_volume_scale_raw_to_m": raw_to_m,
            "asset_aspect_prism_span_m": physical_span,
            "marker_coverage_fraction_of_asset_aspect_prism": coverage,
            "centered_margin_each_side_m": margin,
            "long_axis_marker_coverage": coverage[long_axis],
            "long_axis_margin_each_end_m": margin[long_axis],
        }

    result["interpretation_guard"] = (
        "This is a geometry-adequacy diagnostic. Treating the mesh bbox aspect as a "
        "rectangular MPM prism is still a proxy; it may justify moving fixture planes "
        "outside the marker envelope, but it does not constitute a mesh-conforming simulation."
    )
    (out / "summary.json").write_text(json.dumps(result, indent=2) + "\n")

    print("VALID GAUGE released-asset geometry diagnostic")
    print("ASSET_RAW_SPAN", *raw_span)
    for material, row in result["materials"].items():
        print(
            "ASSET_GEOMETRY",
            material,
            "marker_span", row["median_marker_bbox_span_m"],
            "prism_span", row["asset_aspect_prism_span_m"],
            "long_coverage", row["long_axis_marker_coverage"],
            "end_margin_m", row["long_axis_margin_each_end_m"],
        )


if __name__ == "__main__":
    main()
