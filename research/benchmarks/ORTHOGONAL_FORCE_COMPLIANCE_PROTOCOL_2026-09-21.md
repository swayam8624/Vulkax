# Orthogonal Force-Compliance Information Test — Frozen Protocol

Status: **FROZEN BEFORE EXECUTION**

Date: 2026-09-21

This experiment is a follow-on test of the information-limit conclusion. It is **not**
a retuning of D2/D3/D4V and must not consume or modify their frozen truth worlds.

## Question

D4V showed that ordinary held-out improvements can be deceptive while every tested
kinematic verification channel remained unresolved.

The follow-on question is:

> Does adding a genuinely different physical control channel — a known calibrated
> external force with measured displacement compliance — make deceptive and
> beneficial repairs more observable than the existing kinematic DCS channel?

## Independence contract

The experiment uses:

- fresh off-grid truth material values not used by D4V;
- a new force-controlled excitation family not used for fitting or repair proposal;
- the same frozen absolute decision reference, |z| >= 2;
- no threshold tuning after outputs are observed;
- no use of hidden beneficial/deceptive labels for probe selection.

D2/D3/D4V remain frozen regardless of this result.

## Fresh truth worlds

Young's modulus:

```text
14,125 Pa
16,625 Pa
18,125 Pa
```

Poisson ratio:

```text
0.22
0.32
```

Truth transfer:

```text
APIC
```

Truth timestep:

```text
2.5e-5 s
```

Total: 6 fresh truth worlds.

## Candidate families

Candidates are fitted only on the existing small-amplitude kinematic calibration
family:

- APIC;
- PIC;
- constrained-nu APIC;
- coarse-timestep APIC.

Fitting grid remains intentionally coarse and fixed:

```text
E  in {12,000, 15,000, 18,000} Pa
nu in {0.20, 0.30, 0.40}
constrained-nu candidate uses nu = 0.10
```

## Repair proposal

For each truth world, all candidate pairs are ordered by ordinary held-out kinematic
RMS error.

The lower-error candidate becomes the proposed repair.

The proposal mechanism is therefore blind to the force-compliance channel.

## Hidden label

A stronger untouched kinematic target labels the proposal:

- repair target error < baseline target error -> beneficial;
- repair target error > baseline target error -> deceptive.

These labels are used only after the repair has been proposed and all verification
signals have been computed.

## Existing comparison channel

The existing witness-space order-2 DCS probe is executed on a fresh 3x3
shear/axial intervention lattice.

This is a comparator only. It is not retuned.

## New orthogonal channel

### Boundary

- bottom particle layer: fixed kinematically;
- all other particles: unconstrained;
- gravity: zero.

### Known excitation

The top particle layer receives a known external force for a fixed short horizon.

Per-top-particle force amplitude:

```text
40 N
```

Excitation directions:

```text
+x
-x
+y
+z
```

The force is re-applied before every MPM step because the solver consumes and clears
externalForce during a step.

### Observable

For every force direction, record:

- mean top-layer displacement (x,y,z);
- mean interior-layer displacement (x,y,z).

Thus each force experiment returns a six-component compliance response.

The complete force channel is the four-experiment response bundle.

## Numerical uncertainty

Each candidate is run at:

- nominal candidate timestep;
- half candidate timestep.

Force-channel numerical uncertainty is the RMS difference between nominal and
half-dt compliance bundles.

## Observation uncertainty

Synthetic truth receives deterministic position noise with fixed scale:

```text
2e-5 m per response component
```

Measurement and repeat variance each use that same frozen scale.

## Progress statistic

For baseline B, repair R, measured force bundle Y:

```math
z_{force}
=
\frac{
e(B,Y)-e(R,Y)
}{
\sqrt{
\sigma_{meas}^2+
\sigma_{repeat}^2+
\sigma_{num,B}^2+
\sigma_{num,R}^2
}
}
```

where e is bundle RMS error.

Decision:

```text
z >= +2  -> support repair
z <= -2  -> veto repair
otherwise -> unresolved
```

The DCS comparator uses the same absolute decision reference.

## Frozen primary outputs

Report for both DCS and force-compliance:

- resolved coverage;
- veto count;
- support count;
- deceptive-repair veto recall over all deceptive repairs;
- beneficial support rate over all beneficial repairs;
- false-veto rate on beneficial repairs;
- sign accuracy irrespective of threshold;
- median |z|;
- maximum |z|.

Also report:

- force/DCS median |z| ratio;
- number of proposals resolved only by force;
- number resolved only by DCS;
- number resolved by both;
- agreement/disagreement with hidden labels.

## Advancement gate

A positive “orthogonal information helps” claim requires **all**:

1. at least 4 deceptive repairs in the fresh proposal population;
2. force resolved coverage >= 30%;
3. force deceptive veto recall over all deceptive repairs >= 50%;
4. force beneficial false-veto rate <= 25%;
5. force resolved coverage > DCS resolved coverage;
6. force median |z| > DCS median |z|;
7. numerical half-dt comparison finite for every proposal.

If any gate fails, the result is retained as a negative follow-on experiment.

## Forbidden responses to failure

After execution, do not:

- change the 40 N force amplitude;
- change the force directions;
- change the fresh truth values;
- lower |z| = 2;
- change the fitting grid;
- remove inconvenient candidate families;
- reuse this partition for tuning a replacement channel.

A redesigned experiment would require another fresh protocol and new truth worlds.

## Interpretation boundary

Even a positive synthetic result would establish only that an orthogonal
force-controlled channel can improve observability in this controlled MPM regime.

It would **not** by itself establish measured-world prospective verification. A real
force-sensor experiment would still be required for that stronger claim.
