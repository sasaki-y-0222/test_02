# ParticleForge

**ParticleForge** is a 3D particle generator plug-in for **Adobe After Effects
2021 (17.x) and later**, modelled on the feature set of *Trapcode Particular*
around its **version 1.5** era. It is an independent, educational
re-implementation — it is **not** affiliated with or derived from Red Giant /
Maxon, and "Trapcode" / "Particular" are trademarks of their respective owners.

The plug-in emits and simulates particles every frame (deterministically) and
composites them over the layer it is applied to, with full 8‑bpc and 16‑bpc
support.

---

## Feature overview (Particular 1.5 parity targets)

### Emitter
| Control | Notes |
|---|---|
| Particles / sec | emission rate |
| Emitter Type | Point / Box / Sphere |
| Position XY + Position Z | 3D emitter origin |
| Direction | base emission angle (0° = up) |
| Direction Spread | cone spread, 0–100 % |
| Velocity | initial speed (px/sec) |
| Velocity Random | speed randomisation |
| Velocity Distribution | bias of the random speed spread |
| Emitter Size X / Y / Z | box / sphere dimensions |

### Particle
| Control | Notes |
|---|---|
| Life [sec] + Life Random | lifespan |
| Particle Type | Sphere / Glow Sphere / Star |
| Size + Size Random | particle size |
| Size over Life | Constant / Grow / Shrink / Grow‑then‑Shrink / Bell |
| Opacity + Opacity over Life | Constant / Fade In / Fade Out / In‑and‑Out / Bell |
| Birth Color / Death Color | colour interpolated over the lifespan |
| Blend Mode | Add / Normal / Screen |

### Physics
| Control | Notes |
|---|---|
| Gravity | constant downward (or up) acceleration |
| Air Resistance | velocity damping |
| Wind X / Wind Y | directional force |
| Spin | rotation of particles around the emitter |
| Turbulence Amount / Scale / Evolution | animated noise field (the "Turbulence Field" analogue) |

### Global
| Control | Notes |
|---|---|
| Random Seed | reproducible randomness |

> The projection uses a simple built‑in perspective camera centred on the
> composition. Integration with the After Effects 3D camera (via the AEGP
> camera suite) is a natural next step but is intentionally out of scope for
> this first version to keep the renderer self‑contained.

---

## How it works

After Effects effects are normally stateless per frame. A particle system needs
continuous temporal state, so — exactly like the classic Trapcode approach —
`ParticleForge` **re-simulates from time 0 up to the current composition time on
every rendered frame**. The simulation is fully deterministic (seeded hashing
per particle), so scrubbing, rendering and multi‑frame rendering all agree.

This makes long compositions progressively heavier to render (the cost grows
with the current time), which mirrors the behaviour of early Particular. The
engine caps the number of simulated steps and live particles to avoid runaway
memory/CPU on extreme parameter values.

Source layout:

```
ParticleForge/
├── ParticleForge.h        parameter layout, IDs, version, entry point
├── ParticleForge.cpp      AE glue: params setup, value gathering, compositing
├── Simulation.h           SDK-independent simulation API
├── Simulation.cpp         deterministic particle physics
├── Noise.h                3D value noise for the turbulence field
├── ParticleForgePiPL.r    PiPL resource (plug-in manifest)
└── CMakeLists.txt         cross-platform build
```

`Simulation.*` and `Noise.h` have **no dependency on the After Effects SDK** and
can be compiled / tested on their own.

---

## Building

You need the **Adobe After Effects SDK** (free download from Adobe). It is not
included here. Point the build at it via `AESDK_ROOT` — the folder that contains
`Examples/Headers`.

### Windows (Visual Studio 2019 / 2022)

```bat
cmake -B build -A x64 -DAESDK_ROOT="C:/AE_SDK/AfterEffectsSDK"
cmake --build build --config Release
```

This produces `ParticleForge.aex`. Copy it to:

```
C:\Program Files\Adobe\Common\Plug-ins\7.0\MediaCore\
```

If `PiPLtool` is not found automatically, compile the PiPL by hand:

```bat
cl /EP /I "%AESDK_ROOT%\Examples\Headers" /DMSWindows=1 ParticleForgePiPL.r > tmp.rr
PiPLtool tmp.rr ParticleForgePiPL.rc
```

then add `ParticleForgePiPL.rc` to the target (the CMake script does this for
you when `PiPLtool` is on the path).

### macOS (Xcode 12+, universal arm64 + x86_64)

```bash
cmake -G Xcode -B build -DAESDK_ROOT="$HOME/AE_SDK/AfterEffectsSDK"
cmake --build build --config Release
```

This produces `ParticleForge.plugin`. Compile and embed the PiPL with Rez
(part of the standard AE SDK build step):

```bash
Rez -i "$AESDK_ROOT/Examples/Headers" -i "$AESDK_ROOT/Examples/Headers/SP" \
    -d macintosh=1 -useDF \
    -o build/ParticleForge.plugin/Contents/Resources/ParticleForge.rsrc \
    ParticleForgePiPL.r
```

Copy the bundle to:

```
/Library/Application Support/Adobe/Common/Plug-ins/7.0/MediaCore/
```

---

## Usage

1. Apply **Effect ▸ Simulation ▸ ParticleForge** to a (preferably comp‑sized,
   black) solid.
2. Move the time indicator forward — particles accumulate from time 0.
3. Tune the Emitter, Particle and Physics groups. Good starting points:
   * **Sparks / fireworks** — Emitter *Point*, high Velocity, positive Gravity,
     Blend *Add*, Opacity over Life *Fade Out*.
   * **Smoke / dust** — Emitter *Sphere*, low Velocity, Turbulence Amount up,
     Particle *Glow Sphere*, large Size, Size over Life *Grow*.
   * **Starfield** — Emitter *Box* (wide), Velocity toward camera via Position Z
     animation, Particle *Star*.

---

## Limitations / roadmap

* Uses an internal perspective camera rather than the comp's 3D camera.
* No 32‑bpc float, motion blur, "Aux System", sprites or depth‑of‑field yet.
* Render cost grows with composition time (re-simulation model).

These are deliberate scope choices for a compact, readable first version; the
SDK‑independent simulation core is structured so they can be added without
touching the AE glue.

---

## License

Educational / non‑commercial reference implementation. Trademarks belong to
their respective owners.
