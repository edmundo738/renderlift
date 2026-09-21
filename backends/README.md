# RenderLift backends

One module per graphics API. The backend is the **only** place allowed to know
about D3D or Vulkan — ALRR Core (`src/`) stays API-agnostic.

| Module (target name) | Ships as (Windows) | Status |
|---|---|---|
| `rl-backend-d3d9` | `RenderLift.D3D9.dll` | stub — planned |
| `rl-backend-d3d10` | `RenderLift.D3D10.dll` | stub — planned |
| `rl-backend-d3d11` | `RenderLift.D3D11.dll` | stub — **first target (0.2)** |
| `rl-backend-d3d12` | `RenderLift.D3D12.dll` | stub — after D3D11 |
| `rl-backend-vulkan` | `RenderLift.Vulkan.dll` | stub — planned |

Every module implements [`rl::backend::IBackend`](common/include/renderlift/backend/Backend.hpp)
and declares its planned hook targets (enforced by tests). Per-API integration
plans live in [`docs/apis/`](../docs/apis/).

## Becoming a real backend (0.2 checklist for D3D11)

1. Vendor MinHook under `third_party/` (see `THIRD_PARTY_NOTICES.md`).
2. Flip the module to `SHARED` with `OUTPUT_NAME "RenderLift.D3D11"`.
3. Implement export discovery + detours for the declared hook targets.
4. Wire the frame pipeline contract from `src/renderer` (reconstruction between
   post-process and UI composition; UI at native resolution).
5. Feed `FrameSample`s to the dynamic resolution controller; apply ladder steps
   through render-target/viewport steering — never a Present-time resize.
