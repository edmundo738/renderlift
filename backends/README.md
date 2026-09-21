# RenderLift backends

One module per graphics API. The backend is the **only** place allowed to know
about D3D or Vulkan — ALRR Core (`src/`) stays API-agnostic.

| Module (target name) | Ships as (Windows) | Status |
|---|---|---|
| `rl-backend-d3d9` | `RenderLift.D3D9.dll` | stub — planned |
| `rl-backend-d3d10` | `RenderLift.D3D10.dll` | stub — planned |
| `rl-backend-d3d11` | `RenderLift.D3D11.dll` | **0.2 groundwork: SHARED on Windows, MinHook-detours `Present`/`ResizeBuffers` (measured passthrough + display tracking)** |
| `rl-backend-d3d12` | `RenderLift.D3D12.dll` | stub — after D3D11 |
| `rl-backend-vulkan` | `RenderLift.Vulkan.dll` | stub — planned |

Every module implements [`rl::backend::IBackend`](common/include/renderlift/backend/Backend.hpp)
and declares its planned hook targets (enforced by tests). Per-API integration
plans live in [`docs/apis/`](../docs/apis/).

## Becoming a real backend (checklist for D3D11)

1. ✅ Vendor MinHook under `third_party/` (see `THIRD_PARTY_NOTICES.md`).
2. ✅ Flip the module to `SHARED` with `OUTPUT_NAME "RenderLift.D3D11"` (Windows).
3. 🔶 Hook engine (`rl::backend::IHookEngine` over MinHook) + probe-device
   vtable resolution; detours installed for `Present`/`ResizeBuffers`.
4. 🔶 Platform-neutral steering core (`rl::backend::SteeringPolicy`,
   `RenderTargetRegistry`) — decides which targets render at the internal
   resolution; D3D11 wiring lands in 0.3 (CreateTexture2D/RSSetViewports
   steering + ALRR compute dispatch).
5. ☐ Wire the frame pipeline contract from `src/renderer` (reconstruction
   between post-process and UI composition; UI at native resolution).
6. ☐ Feed `FrameSample`s to the dynamic resolution controller (CPU frame
   timing first; GPU-busy estimator documented in `docs/apis/d3d11.md`).
