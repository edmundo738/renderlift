# RenderLift

**Render less. Reconstruct more.**

[![ci](https://github.com/edmundo738/renderlift/actions/workflows/ci.yml/badge.svg)](https://github.com/edmundo738/renderlift/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.cppreference.com/w/cpp/20)
[![status](https://img.shields.io/badge/status-early%20scaffold-orange.svg)](CHANGELOG.md)

RenderLift is a cross-API **low-resolution rendering and reconstruction framework** for low-end gaming PCs. It aims to reduce the *real* internal 3D rendering cost of games and reconstruct the final image at the target display resolution — with support planned for **Direct3D 9 / 10 / 11 / 12 and Vulkan**.

> GTA V (on an Intel UHD 620 @ 1366×768) is the **first test lab** — not the product. The repository is born multi-game and multi-API.

---

## The core idea

Hooking `Present` alone is **not** enough. That only post-processes an image the game already rendered at full cost:

```text
WRONG for our goal (pure post-processing):

Game renders 1366×768 ──▶ Present ──▶ rescale ──▶ 1366×768
(FPS cost unchanged)
```

RenderLift intercepts the pipeline **before and during** the creation and use of render targets, viewports and the internal render resolution, so the expensive 3D work happens at a much lower resolution and is then reconstructed:

```text
RIGHT — what RenderLift does:

Game renderer
   ▼
Internal 3D render: 426×240 … 854×480   (dynamic, per-frame)
   ▼
Lighting / shadows / effects at internal res
   ▼
ALRR reconstruction (Spatial → Edge → Temporal)
   ▼
HUD / UI composited at native resolution
   ▼
1366×768 (or your display) to the monitor
```

That difference — reducing the **actual render work**, not resizing the output — is what separates RenderLift from an image filter.

## Architecture

**RenderLift** is the product. **ALRR** (*Adaptive Low-Resolution Reconstruction*) is the reconstruction engine inside it. They version independently:

```text
RenderLift 0.1   └── ALRR Spatial 0.1     (shipping engine tier)
RenderLift 0.5   ├── ALRR Spatial  └── ALRR Edge
RenderLift 1.0   ├── ALRR Spatial  ├── ALRR Edge   └── ALRR Temporal
```

```text
                    RENDERLIFT
                        │
              ┌─────────┴─────────┐
              │     ALRR CORE     │   (API-agnostic engine)
              │  Resolution Mgr   │
              │  Reconstruction   │
              │  Sharpening       │
              │  Dynamic Res Ctl  │
              │  Frame Pacing     │
              │  GPU/CPU Monitor  │
              └─────────┬─────────┘
        ┌───────┬───────┼────────┬─────────┐
        ▼       ▼       ▼        ▼         ▼
   D3D9.dll  D3D10    D3D11    D3D12    Vulkan   ← per-API backend modules
   backend   backend  backend  backend  backend     (only the capture/
                                                    injection layer differs)
```

The reconstruction algorithm is the **same** for every API. Only the backend — the layer that intercepts device/swapchain/render-target/viewport calls — is API-specific.

## Backends

| Backend   | Module                | Status      | Integration points |
|-----------|-----------------------|-------------|--------------------|
| D3D11     | `RenderLift.D3D11.dll` | 🥇 first target | `IDXGISwapChain::Present/ResizeBuffers`, render-target & viewport tracking |
| D3D9      | `RenderLift.D3D9.dll`  | planned     | `IDirect3DDevice9::Present/Reset` |
| D3D10     | `RenderLift.D3D10.dll` | planned     | DXGI swap chain family |
| D3D12     | `RenderLift.D3D12.dll` | later — not "D3D11 with different names": command lists, explicit resource states & barriers | `ExecuteCommandLists`, RTV tracking, `ResourceBarrier` |
| Vulkan    | `RenderLift.Vulkan.dll`| planned     | `vkCreateSwapchainKHR`, `vkAcquireNextImageKHR`, `vkQueuePresentKHR` |

See [`docs/apis/`](docs/apis/) for per-API integration notes, and [`docs/architecture/overview.md`](docs/architecture/overview.md) for the full picture.

## ALRR engine tiers

| Tier | Cost | Idea |
|------|------|------|
| **ALRR Spatial** | very cheap | optimized bilinear/bicubic + controlled sharpening |
| **ALRR Edge** | cheap | edge/contrast-aware reconstruction — different treatment for edges vs flat areas |
| **NIS fallback** | cheap | NVIDIA Image Scaling shader as an interchangeable reconstruction backend (not the product identity) |
| **ALRR Temporal** | medium | frame history + jitter + motion info, once available |

## Dynamic resolution — GPU-bound *and* CPU-aware

RenderLift doesn't blindly drop resolution. The controller watches both units:

```text
GPU ~99% busy  → step internal resolution down
GPU ~85% busy  → hold
GPU ~65% busy  → step internal resolution up
CPU ~100%      → do NOT lower (the game is CPU-bound;
                 a smaller render target would only waste quality)
```

Ladder presets (16:9 reference):

| Mode | Internal res |
|------|--------------|
| Ultra Performance | 320×180 |
| Extreme | 426×240 |
| Performance | 480×270 |
| Balanced | 640×360 |
| Quality | 854×480 |
| Native-ish | 1024×576 |

Per-game profiles can pin exact ladders (e.g. GTA V pins `426×240 → 1366×768`).

## First lab: GTA V on Intel UHD 620

- **Backend:** D3D11 · **Display:** 1366×768 · **Internal start:** 640×360
- **Fallback:** 854×480 · **Extreme:** 426×240
- **UI:** native resolution · **Reconstruction:** ALRR Edge · **Sharpen:** low
- Dynamic resolution lands after the base pipeline is proven.

Profiles live in [`profiles/`](profiles/) (game + hardware JSON, schema documented in [`profiles/README.md`](profiles/README.md)).

## Repository layout

```text
renderlift/
├── src/                 # ALRR Core + engine modules (API-agnostic)
│   ├── core/            #   types, config, JSON, game profiles
│   ├── resolution/      #   resolution manager + dynamic resolution controller
│   ├── reconstruction/  #   ALRR stages: Spatial / Edge (CPU reference impls)
│   ├── renderer/        #   frame pipeline description (where ALRR hooks in)
│   ├── detection/       #   GPU/CPU/OS probing (interface + platform impls)
│   └── profiling/       #   frame timers, rolling stats
├── backends/            # per-API capture/injection modules (D3D9→12, Vulkan)
├── shaders/             # HLSL (+GLSL) reconstruction & sharpening shaders
├── profiles/            # game / generic / hardware profiles (JSON)
├── tests/               # core + controller + reconstruction tests
├── tools/               # RenderLift CLI (profile inspector, DRS simulator)
├── docs/                # architecture, API notes, graphics research, compatibility
└── third_party/         # (planned) MinHook, volk, … see THIRD_PARTY_NOTICES.md
```

## Building

Requires CMake ≥ 3.20 and a C++20 compiler (MSVC 2022 on Windows; GCC/Clang work for the core, tests and tools).

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Try the CLI:

```bash
./build/tools/renderlift-cli/RenderLift.CLI simulate profiles/games/grand-theft-auto-v.json
./build/tools/renderlift-cli/RenderLift.CLI ladder 1366x768
```

Windows presets (future hook-capable backends) are in [`CMakePresets.json`](CMakePresets.json).

## Roadmap

- **0.1** — repo scaffold, ALRR Core (resolution manager, dynamic controller, CPU reference reconstruction), profiles, CLI, CI ✅ *(this commit)*
- **0.2** — D3D11 injection/proxy plumbing, hook engine (MinHook), offscreen render-target redirection
- **0.3** — ALRR Spatial HLSL path live in D3D11, UI-at-native-res composition
- **0.5** — ALRR Edge, dynamic resolution active in-game, benchmark overlay
- **1.0** — ALRR Temporal, D3D9 + Vulkan backends, hardware auto-profiles

## Safety & fair-play

RenderLift is intended for **single-player / offline** use. Injecting DLLs or hooking graphics APIs in online modes (e.g. GTA Online) can trigger anti-cheat systems and bans. See [SECURITY.md](SECURITY.md).

## Contributing / License

Contributions welcome — see [CONTRIBUTING.md](CONTRIBUTING.md).
Licensed under the [MIT License](LICENSE). Third-party components (when vendored) keep their own licenses — see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
