# ALRR Temporal (tier 1.0) — design notes

Target: `RenderLift 1.0`. Not started — this directory holds design intent so
Spatial/Edge decisions stay compatible with a future temporal stage.

## Inputs the temporal stage will need

| Input | Source | Notes |
|---|---|---|
| Current internal-res frame | backend render-target steering | already available from 0.2+ |
| History buffer (previous reconstructed frame) | engine-owned | `R16G16B16A16_FLOAT`, ping-pong |
| Per-frame jitter | engine → game's projection | sub-pixel translation applied to internal rendering; sampled sequence (Halton 2,3) |
| Depth buffer | backend read | for disocclusion + reactive masking |
| Motion vectors | **hardest**: game-renderer interception or generated optically | D3D11-era games rarely expose them; plan an engine-generated fallback |

## Pipeline sketch

```text
internal frame (jittered)
        │
        ▼
  neighborhood clamp vs current frame
        │        ┌── history reprojection (motion vectors or camera-only fallback)
        ▼        ▼
  blend factor per pixel (confidence)
        │
        ▼
  ALRR Edge spatial pass on the blended result
        │
        ▼
  sharpen (CAS-lite) → UI composite → present
```

## Non-goals for 1.0

- No inference/ML — must run on Intel UHD-class iGPUs.
- No requirement that games expose motion vectors (fallback: camera
  reprojection + conservative blend factors).
- Frame generation is out of scope; ALRR Temporal is *reconstruction*, not
  interpolation.
