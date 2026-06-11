# test_02

This repository contains **ParticleForge** — a 3D particle generator plug-in
for **Adobe After Effects 2021 (17.x) and later**, modelled on the feature set
of *Trapcode Particular* around its **version 1.5** era.

It is an independent, educational re-implementation and is **not** affiliated
with Red Giant / Maxon. "Trapcode" and "Particular" are trademarks of their
respective owners.

➡️ See [`ParticleForge/README.md`](ParticleForge/README.md) for the full feature
list, build instructions (Windows / macOS) and usage notes.

## Quick summary

* Emitter: Point / Box / Sphere, 3D position, direction + spread, velocity
  (random / distribution), emitter size.
* Particles: life (+random), Sphere / Glow Sphere / Star, size & opacity over
  life, birth→death colour, Add / Normal / Screen blend modes.
* Physics: gravity, air resistance, wind, spin, and an animated turbulence
  field (amount / scale / evolution).
* 8‑bpc and 16‑bpc rendering; deterministic, time‑based simulation.

The simulation core (`ParticleForge/Simulation.*`, `Noise.h`) has no dependency
on the After Effects SDK and can be built and tested standalone. The AE glue
(`ParticleForge.cpp`) and PiPL manifest plug into the SDK; you supply the free
Adobe After Effects SDK at build time.
