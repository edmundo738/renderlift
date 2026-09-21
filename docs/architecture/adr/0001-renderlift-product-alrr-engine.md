# ADR 0001 — RenderLift is the product; ALRR is the engine

- Status: **accepted**
- Date: 2026-09-21
- Deciders: edmundo738 + Arena agent

## Context

Early discussions called the whole program "ALRR". That couples the user's
mental model ("what do I install?") to a specific technique
("how does reconstruction work?"). If the reconstruction strategy changes —
or grows tiers — a technique-named product ages badly. (A prior candidate
project name, "FrameForge", was also discarded: the name is already used by
several public projects.)

## Decision

- **RenderLift** names the product, repository, executables and modules:
  `RenderLift.exe`, `RenderLift.Core.dll`, `RenderLift.D3D11.dll`, …
- **ALRR — Adaptive Low-Resolution Reconstruction** names the reconstruction
  engine shipping inside the product, with its own version track:
  ALRR Spatial (0.1) → ALRR Edge (0.5) → ALRR Temporal (1.0).
- Names that tie the project to one game, GPU, or third-party tech
  (`GTA-Upscaler`, `UHD620-FSR`, …) are rejected permanently.
- NVIDIA Image Scaling (NIS), if integrated, is a reconstruction *fallback
  tier* — never part of the naming.

## Consequences

- C++: product umbrella `rl::`, engine tiers under `rl::alrr`.
- Versioning: `RenderLift 0.5` may ship "ALRR Edge 0.5" without a name change.
- Room for non-upscaler features under the same product later: dynamic
  resolution, frame pacing, compatibility layers, game profiles, benchmarks.
- GTA V remains a *test lab*, not the brand.
