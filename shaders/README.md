# RenderLift shaders

GPU implementations of the ALRR reconstruction tiers. The CPU reference
implementations in `src/reconstruction/` define the expected output; these
shaders are production ports optimized for integrated GPUs (Intel UHD first).

| File | Tier | Language(s) | Notes |
|---|---|---|---|
| `spatial/alrr_spatial.hlsl` | ALRR Spatial 0.1 | HLSL (SM 5.0 compute) | bilinear resample + unsharp sharpen, one pass |
| `spatial/alrr_spatial.glsl` | ALRR Spatial 0.1 | GLSL 450 (Vulkan) | same algorithm for the Vulkan backend |
| `edge/alrr_edge.hlsl` | ALRR Edge 0.5 (WIP) | HLSL | gradient-adaptive sharpening (CPU-port of `EdgeUpscaler`) |
| `sharpen/alrr_sharpen.hlsl` | standalone | HLSL | CAS-lite post pass for already-scaled content |
| `temporal/` | ALRR Temporal 1.0 | — | design notes only; history + jitter + motion vectors |

## Conventions

- Compute shaders, `[numthreads(8, 8, 1)]`, one thread per output pixel.
- Sampler-less manual bilinear (keeps corners exact, matching the CPU path).
- Sharpen amount arrives via constant buffer / push constants (`0..1`, clamp
  the high-pass against a negative-lobe limit to avoid halos).
- Generic RGBA8 *and* float RTs: reads are format-agnostic (`Texture2D<float4>`),
  writes go to `RWTexture2D<unorm float4>`/`rgba8` images.

## Compilation (planned, not wired to the build yet)

```bash
# DXIL for D3D11/D3D12 backends
dxc -T cs_5_0 -E CSMain -Fo alrr_spatial.cso shaders/spatial/alrr_spatial.hlsl

# SPIR-V for the Vulkan backend
dxc -spirv -T cs_6_0 -E CSMain -Fo alrr_spatial.spv shaders/spatial/alrr_spatial.hlsl
# or: glslangValidator -V shaders/spatial/alrr_spatial.glsl -o alrr_spatial.spv
```

Compiled artifacts (`*.cso`, `*.spv`) are gitignored; they will be produced by
the build once the backends consume them.
