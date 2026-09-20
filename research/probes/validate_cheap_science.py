#!/usr/bin/env python3
import csv, json, math, pathlib, sys

root = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "build/research-cheap-probes")
csv_path = root / "cheap_probes.csv"
summary_path = root / "summary.json"
if not csv_path.is_file() or not summary_path.is_file():
    raise SystemExit("missing cheap-probe artifacts")
summary = json.loads(summary_path.read_text())
if summary.get("provenance") != "synthetic":
    raise SystemExit("cheap probes must remain explicitly synthetic")
rows = list(csv.DictReader(csv_path.open()))
if not rows:
    raise SystemExit("no probe rows")
for row in rows:
    value = float(row["value"])
    if row["metric"] != "condition_number" and not math.isfinite(value):
        # H6 trust radius must become finite in this controlled nonlinear system.
        raise SystemExit(f"non-finite result: {row}")
    if row["provenance"] != "synthetic":
        raise SystemExit(f"unexpected provenance: {row}")

def one(probe, scenario, metric):
    xs=[r for r in rows if r["probe"]==probe and r["scenario"]==scenario and r["metric"]==metric]
    if len(xs)!=1:
        raise SystemExit(f"expected one row for {(probe,scenario,metric)}, got {len(xs)}")
    return float(xs[0]["value"])

if one("identifiability","unanchored","rank") != 1:
    raise SystemExit("unanchored scale/gravity positive control lost rank deficiency")
if one("identifiability","anchored","rank") != 2:
    raise SystemExit("metric anchor did not restore expected local rank")

held = one("heldout_vs_counterfactual","same-regime-heldout","relative_error")
cf = one("heldout_vs_counterfactual","unseen-large-force","relative_error")
if not cf > held:
    raise SystemExit("counterfactual probe did not expose stronger model error than held-out replay")

radius = one("trust_region","threshold_5pct","first_failed_delta_force")
if not (radius > 0.0):
    raise SystemExit("trust-radius probe failed to find a finite nonlinear boundary")

print("VALID Vulkax cheap science probes")
print(f"heldout_relative_error={held:.9g}")
print(f"counterfactual_relative_error={cf:.9g}")
print(f"trust_radius_delta_force={radius:.9g}")
