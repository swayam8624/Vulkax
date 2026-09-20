# GAUGE fixture-envelope diagnostic — 2026-09-20

## Purpose

Explain whether the historical GAUGE forward model places its prescribed fixture at a physically justified location. This is a measured/metadata diagnostic only. No trajectory error is used to choose geometry.

## Frozen selected-trial evidence

Using the same frozen representative shearing trials as the no-fit forward gate:

| quantity | soft | hard |
|---|---:|---:|
| measured driver peak | 59.675 mm | 59.019 mm |
| topology-terminal moving-edge peak / driver | 0.9311 | 0.9220 |
| moving-edge driver residual RMS / driver peak | 0.0387 | 0.0455 |
| moving-edge driver-direction correlation | 0.999987 | 0.999981 |
| topology-terminal nominal-fixed-edge RMS / driver peak | 0.3037 | 0.2901 |
| terminal marker-strip long span | 149.791 mm | 152.028 mm |

The moving terminal edge closely follows the driver direction, but the opposite topology-terminal edge is not stationary: it moves with an RMS magnitude around 29–30% of driver peak.

That does **not** by itself falsify the physical fixture, because the released marker strip does not extend to the foam's physical ends.

## Released-asset geometry resolves the missing support

GAUGE releases a foam asset with unit-free bbox aspect 1:1:4. Combining only that aspect with measured mass/density gives:

- soft volume = 0.0066 kg / 13.2 kg m^-3 = **0.0005 m^3**
- hard volume = 0.0131 kg / 26.2 kg m^-3 = **0.0005 m^3**
- volume-consistent dimensions = **50 mm x 50 mm x 200 mm**

Compared with the marker envelope:

- soft: 200.000 - 149.791 = 50.209 mm unobserved longitudinal support, about **25.105 mm per end** if centered;
- hard: 200.000 - 152.028 = 47.972 mm unobserved support, about **23.986 mm per end** if centered.

The historical `measured_aspect` proxy set `prismLo/prismHi` on the long axis directly from marker min/max and then prescribed the terminal particle layer there. It therefore treated the marker envelope as the body envelope.

The new `released_asset_aspect` mode instead centers a 200 mm volume-consistent body around the marker envelope and places prescribed particle layers at the actual proxy body ends. This change is physically justified from released asset aspect + measured mass/density and does not inspect trajectory error.

### Historical prism distortion

Because the historical proxy forced measured mass/density volume into the much shorter marker-envelope length, it also inflated the inferred cross-section:

- soft historical proxy: approximately **58.83 x 56.74 x 149.79 mm**;
- hard historical proxy: approximately **59.74 x 55.05 x 152.03 mm**;
- released-asset-aspect proxy: **50 x 50 x 200 mm** for both, from the common 0.0005 m^3 metadata volume and 1:1:4 asset aspect.

Therefore the repair changes both fixture distance and global body aspect in a provenance-backed way. The mechanism comparison must determine which observed mode errors actually move in the predicted direction; no improvement is assumed in advance.

## Falsifiable hypothesis

**H-GAUGE-FIXTURE-ENVELOPE**

A substantial fraction of the historical GAUGE shearing miss is caused by imposing the fixture at the marker envelope rather than at the physical body ends, which overconstrains interior observed points and distorts longitudinal/area coupling.

### Prediction before seeing the new run

If this hypothesis is materially correct, released-asset geometry should:

1. reduce marker-position error and face-area error together;
2. reduce the 2–3x over-amplification in face-area/shear RMS;
3. move final mean longitudinal strain toward the measured negative sign in both materials;
4. preserve the strong average shear/transverse temporal agreement.

If it only improves face-area NRMSE while marker RMSE worsens, or leaves the wrong-sign longitudinal response intact, the hypothesis is weakened/rejected.

Inverse fitting remains locked regardless of outcome until the no-fit adequacy gate is independently passed.
