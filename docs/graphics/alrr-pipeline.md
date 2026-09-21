# ALRR pipeline — reconstruction tiers

ALRR = Adaptive Low-Resolution Reconstruction. One engine, four tiers; the
same tier feeds every backend (identical algorithm, different capture layer).

## Tier map

| Tier | Version | Type | Cost | Purpose |
|---|---|---|---|---|
| ALRR Spatial | 0.1 | spatial | very low | correctness floor: clean resample + controlled sharpen |
| ALRR Edge | 0.5 | spatial, adaptive | low | quality where it matters: edge/contrast-aware treatment |
| NIS fallback | — | spatial | low | interchangeable third-party backend, *not* our identity |
| ALRR Temporal | 1.0 | temporal | medium | history + jitter (+ motion info) to approach stable detail |

## ALRR Spatial (shipping 0.1)

Center-aligned bilinear resample + 3×3 unsharp sharpening. Cheap enough to
run anywhere; exact corners; flat areas untouched by the high-pass.
CPU reference: `src/reconstruction/src/SpatialUpscaler.cpp`.
GPU ports: `shaders/spatial/alrr_spatial.hlsl` / `.glsl`.

## ALRR Edge (0.5)

Problem with naive sharpening from ≤640×360 sources: it amplifies noise in
flat areas (sky, fog) as much as it rescues edges. ALRR Edge modulates the
sharpen amount per pixel by the source luma gradient:

```text
amount(pixel) = sharpen × smoothstep(edgeLow, edgeHigh, |∇luma|)
flat  → 0.0 (no halos)   ·   strong edge → full sharpen
```

CPU reference: `src/reconstruction/src/EdgeUpscaler.cpp`.
GPU reference port: `shaders/edge/alrr_edge.hlsl` (WIP — see file notes).

## NIS fallback

[NVIDIA Image Scaling](https://github.com/NVIDIADeveloper/NVIDIAImageScaling):
single-pass spatial scaler, 6-tap directional scaling + adaptive sharpening,
implemented as compute shaders — vendor-neutral at runtime despite the name.
RenderLift may integrate it as an interchangeable `IReconstructionStage`.
It never names the product and is never required: ALRR tiers must remain
competitive without it (see `THIRD_PARTY_NOTICES.md`).

## ALRR Temporal (1.0)

Design notes in `shaders/temporal/README.md`. Hard requirement: **no motion
vectors demanded from the game** — camera reprojection fallback or bust, so
it runs on unmodified D3D11-era titles on UHD-class hardware.

## Sharpening policy

Sharpen is a separate pass (`sharpen/alrr_sharpen.hlsl`, CAS-lite with
min/max clamped negative lobes) so tiers can compose:
`reconstruct → optional extra sharpen → UI composite`.
Profile defaults stay low (0.30–0.40): over-sharpened 360p beats nothing in
artifacts only.
