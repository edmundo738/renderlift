# RenderLift — architecture overview

## Product vs engine

- **RenderLift** is the product: loader, game profiles, UI/CLI, benchmark.
- **ALRR** (*Adaptive Low-Resolution Reconstruction*) is the reconstruction
  engine inside it (namespace `rl::alrr`, versions independently).
  See [ADR 0001](adr/0001-renderlift-product-alrr-engine.md).

```text
RenderLift 0.1 → ALRR Spatial 0.1
RenderLift 0.5 → ALRR Spatial + ALRR Edge
RenderLift 1.0 → ALRR Spatial + ALRR Edge + ALRR Temporal
```

## The central thesis: steer the render, don't resize the frame

A `Present`-time hook can only post-process an image whose cost was already
paid. RenderLift's backends must instead steer the **creation and use** of
render targets and viewports so the 3D pipeline genuinely runs at the internal
resolution:

```text
NOT this:  render 1366×768 → Present → rescale → 1366×768   (same GPU cost)

But this:
  ┌─ backend steers targets/viewports ─┐
  render 426×240…854×480 → lighting/shadows/FX at internal res
        → ALRR reconstruction → sharpen
        → HUD/UI composed at 1366×768 native → Present
```

That single difference is the product's identity and is enforced by the
frame-pipeline contract in `src/renderer/include/renderlift/render/FramePipeline.hpp`.

## Module map

```text
                        RENDERLIFT
                            │
                  ┌─────────┴──────────┐
                  │      ALRR CORE      │   src/ — API-agnostic
                  │  resolution         │   ladders + dynamic controller
                  │  reconstruction     │   Spatial / Edge / (NIS) / Temporal
                  │  renderer           │   frame pipeline contract
                  │  detection          │   CPU/GPU/OS probing
                  │  profiling          │   frame timers & stats
                  │  core               │   types, JSON, profiles
                  └─────────┬──────────┘
   ┌───────────┬───────────┼────────────┬─────────────┐
   ▼           ▼           ▼            ▼             ▼
 RenderLift. RenderLift. RenderLift. RenderLift.  RenderLift.
 D3D9.dll    D3D10.dll  D3D11.dll   D3D12.dll   Vulkan.dll   ← backends/
```

Rule of dependency: **backends → engine, never engine → backend.**
Engine code must never include a graphics-API header.

## Process layout (target, 0.3+)

```text
RenderLift.exe            launcher/loader: picks profile, injects backend
RenderLift.Core.dll       ALRR engine (dynamic controller, reconstruction host)
RenderLift.<API>.dll      backend injected into the game process
RenderLift.UI.exe         (later) desktop UI over the same core
RenderLift.CLI.exe        offline toolbox (ships already in 0.1)
```

One user, one screen:

```text
Game detected: GTA V ─ API: D3D11 ─ GPU: Intel UHD 620 ─ Display: 1366×768
Internal  [640×360▾]  Reconstruction [ALRR Edge▾]  Sharpen [0.35]
Dynamic [ON]  UI native [ON]                              [ APPLY ]
```

## Data flow per frame (D3D11 lab)

```text
game calls CreateTexture2D/SetRenderTarget/RSSetViewports
   → backend redirects scene targets to internal-res resources
game renders scene + post-processing at internal res
   → backend detects the 3D pass boundary (heuristics per profile)
   → ALRR compute pass: internal → display res
   → game draws HUD/UI onto the display-res chain
   → Present (unmodified swapchain)
telemetry (frame ms, GPU busy heuristic) → dynamic controller → ladder step
```

## Why not D3D12-first / not "just NIS"

Both are addressed in dedicated docs: [apis/d3d12.md](../apis/d3d12.md) —
explicit resource states and command lists make D3D12 a *later* target —
and [graphics/alrr-pipeline.md](../graphics/alrr-pipeline.md) — NIS is a
welcome fallback tier, never the project's identity.
