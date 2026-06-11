# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

ParticleForge is a 3D particle generator plug-in for Adobe After Effects 2021 (17.x) and later, modelled on Trapcode Particular ~v1.5. It is an independent educational re-implementation (not affiliated with Red Giant/Maxon). The plug-in re-simulates particles from t=0 to the current comp time on every rendered frame and composites them over its host layer, with 8-bpc and 16-bpc support.

## Building

Requires the **Adobe After Effects SDK** (free from Adobe, NOT in this repo). Point CMake at it via `AESDK_ROOT` — the folder containing `Examples/Headers`.

```bat
:: Windows (Visual Studio 2019/2022, v142/v143)
cmake -B build -A x64 -DAESDK_ROOT="C:/AE_SDK/AfterEffectsSDK"
cmake --build build --config Release
```

```bash
# macOS (Xcode 12+, universal arm64 + x86_64)
cmake -G Xcode -B build -DAESDK_ROOT="$HOME/AE_SDK/AfterEffectsSDK"
cmake --build build --config Release
```

Output: `ParticleForge.aex` (Windows) / `ParticleForge.plugin` (macOS). Install into `...\Adobe\Common\Plug-ins\7.0\MediaCore\` (Win) or `/Library/Application Support/Adobe/Common/Plug-ins/7.0/MediaCore/` (mac).

On Windows the build auto-compiles+embeds the PiPL resource if `PiPLtool` (from `Examples/Resources`) is on the path; otherwise see README.md for the manual `cl /EP` + `PiPLtool` steps. On macOS the PiPL is embedded with `Rez` (see README.md). The `build/` directory IS committed to git — regenerated CMake artifacts will show up as diffs.

There is **no test target and no linter configured.** The simulation core (`Simulation.*`, `Noise.h`) has no AE SDK dependency, so it compiles standalone — to test simulation logic, compile those files into a small standalone harness rather than loading the plug-in into AE.

## Architecture

Two cleanly separated layers — keep them that way:

- **AE glue (`ParticleForge.cpp`, `ParticleForge.h`)** — depends on the AE SDK. Handles `PF_Cmd` dispatch (`EffectMain`), parameter setup, gathering param values into a `pf::SimParams`, and compositing the returned `pf::RParticle`s into the output world.
- **Simulation core (`Simulation.{h,cpp}`, `Noise.h`)** — pure C++ in `namespace pf`, **zero AE SDK dependency**. `Simulate(const SimParams&, vector<RParticle>&)` runs the physics. Do not introduce SDK includes here.

### The re-simulation model (most important concept)

AE effects are stateless per frame, but particles need temporal continuity. So like classic Particular, `Simulate()` runs the *entire* simulation from t=0 up to `currentTime` on every frame, stepping at `frameRate`. Determinism comes from per-particle seeded hashing (`Rng` seeded from particle id + `randomSeed` via `HashU32`), so scrubbing, single-frame render, and multi-frame render all agree. Consequence: **render cost grows with comp time.** Hard caps (`kMaxSteps`, `kMaxAlive` in `Simulation.cpp`) bound runaway cost. The simulation works in comp/pixel space, then projects to screen. By default it uses a built-in perspective camera (`focalLength`, `centerX/Y`); the "Camera" param can switch to the comp's active 3D camera, in which case the AE glue (`GetCompCameraView`) queries it via AEGP and hands the simulation a world→view matrix + focal length (`camView`, `camFocalX/Y`) — the simulation core itself stays SDK-free and just applies whatever matrix it is given, falling back to the internal camera if acquisition fails.

### Parameters — two parallel enums that must stay in sync

`ParticleForge.h` has two enums:
- `PARAM_*` (UI order) — must match the order params are added in `ParamsSetup()` and the `params[]` array order at render time.
- `ID_*` (persistent disk IDs) — **never reorder or reuse** once shipped; saved AE projects read values by these IDs.

Adding a param means: add a `PARAM_*` entry (in UI position), add a new `ID_*` (appended, new number), add the matching `Add*()` call in `ParamsSetup()` at the right position, and read it in `Render()`. Popup choice strings are `#define`d in the header (e.g. `EMITTER_TYPE_CHOICES`), and the popup enum values in `Simulation.h` (`kEmitter_*`, `kParticle_*`, `kCurve_*`, `kBlend_*`) are **1-based** to match AE popup indices.

### Render flow (`Render()` in ParticleForge.cpp)

Gathers params → applies `downsample_x/y` scaling to all length/pixel values (so the look is resolution-independent; note `turbScale` divides by `ds`) → derives `currentTime`/`frameRate` from `in_data` time fields → fills `SimParams` projection fields → `Simulate()` → clears+copies input world → composites each particle via `DrawParticle<PixT>` templated over `PF_Pixel8`/`PF_Pixel16`. Particles are sorted back-to-front by depth before compositing (matters for Normal blend). `DrawParticle` renders three primitive types (Sphere = AA disc, GlowSphere = gaussian, Star = gaussian core + axial spikes) under Add/Normal/Screen blend.

### Version & flags coupling

`MAJOR/MINOR/BUG/STAGE/BUILD_VERSION` in `ParticleForge.h` encode to the `AE_Effect_Version` integer in `ParticleForgePiPL.r` (currently 689665 for 1.5.0). The `out_flags` set in `GlobalSetup()` (`DEEP_COLOR_AWARE | NON_PARAM_VARY | PIX_INDEPENDENT` = `0x02000404`) **must match** `AE_Effect_Global_OutFlags` in the `.r` file. Changing one requires updating the other. The match name (`PF_MATCH_NAME` "YS ParticleForge") must also match `AE_Effect_Match_Name` in the PiPL and must never change once shipped.
