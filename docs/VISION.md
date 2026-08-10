# Vulkax vision

Vulkax should help a researcher or engineer answer a physical question—not select a canned visual demo.

## Product statement

**Vulkax is a self-verifying computational-physics system that turns a physical problem into a validated
numerical experiment, then uses that experiment for simulation, analysis, inverse inference,
optimization, and scientific visualization.**

The long-term interaction is:

```text
geometry + materials + observations + governing knowledge + goals + accuracy + compute budget
                                      |
                                      v
                                  ProblemIR
                                      |
                                      v
                               numerical experiment
                                      |
                     +----------------+----------------+
                     |                |                |
                  solution       verification      sensitivities
                     |                |                |
                     +---------- analysis ------------+
                                      |
                           visualization / film / report
```

## Four questions

A mature Vulkax should support four directions over the same physical model:

- **Forward:** what happens?
- **Inverse:** what parameters/model produced what I observed?
- **Optimization:** what design produces the result I want?
- **Experiment design:** what should I measure next to reduce uncertainty?

## What makes the system universal

Universal does not mean one numerical method solving every PDE. It means physical intent is represented
independently of solver and renderer choice. Different problem families may use grids, sparse grids,
particles, unstructured meshes, MPM grids, ray bundles, or hybrid representations.

## First forcing verticals

The architecture will be judged against three demanding workflows:

1. GPU DEM/granular simulation of a rotating mill,
2. hyperelastic inverse material identification and experiment design,
3. vehicle aerodynamics with verification and optimization.

These are integration targets, never top-level modes.

## Non-goals

Vulkax is not an ANSYS clone, a game engine, a shader toy, a collection of presets, or an LLM that emits
unverified solver code. Learned models may accelerate a calculation, but physics and numerical evidence
remain explicit.
