# Changelog

All notable changes to RenderLift are documented here.
Format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project uses [Semantic Versioning](https://semver.org/).

The engine inside the product — **ALRR Core** — keeps its own version and is noted per release.

## [Unreleased]

### Added — 0.2 Research Layer (observation-first, ADR 0003)
- **`integration.mode` ladder** (`observe → steer → reconstruct → full`) in
  core types, game-profile JSON, and the CI validator. DRS stays in the
  schema but is documented as inert until `full`. GTA V and generic-default
  profiles set `observe`, `observationFrames: 600`.
- **RLCAP1 capture format + observation layer** (`backends/common/src/obs/`):
  line-syntax wire format with strict round-trip parser, `FrameCapture`
  aggregator (views→textures, MRT tracking, display estimate from viewports,
  per-frame draw aggregates) and `ResourceClassifier` — confidence + plain
  reason for every class (HDR scene, G-buffer, LDR post, depth, shadow,
  downsample, backbuffer-like, texture, aux).
- **`RenderLift.CLI inspect <capture.log>`** — the offline Frame Resource
  Inspector: parses a capture, prints the classified resource map plus the
  0.3 steering candidates vs the untouchable backbuffer-like set; runs on
  any OS (sample capture in `docs/research/samples/gta-v-simulated.rlcap`).
- `RenderLift.D3D11.dll` Research Layer hooks (measured passthrough):
  device `CreateTexture2D/CreateRenderTargetView/CreateDepthStencilView`,
  context `Draw/DrawIndexed` (counters only) + `OMSetRenderTargets` +
  `RSSetViewports`, swapchain `Present/ResizeBuffers`; mutex-protected
  buffered log (`RENDERLIFT_LOG` env, default `RenderLift.D3D11.log`),
  auto-shutdown at the frame cap (`RENDERLIFT_OBSERVE_FRAMES`).
- `RenderLift.Loader` — Windows lab injector (Toolhelp PID search,
  remote `LoadLibraryW`, ASLR-safe remote call of `RenderLiftInstall`),
  single-player authorized labs only.
- Docs: ADR 0003 (observation-first as project law),
  `docs/research/d3d11-research-layer.md` field manual, `docs/learn/`
  5-layer study path, `docs/apis/d3d11.md` full device/context slot maps.
- Tests: 16 new checks (wire-format round-trips, garbage rejection,
  aggregation, classifier rules incl. square/shadow atlases, integration
  modes, GTA V profile = observe) — 67 checks total.

### Added — 0.2 groundwork (hook engine + D3D11 module)
- **MinHook 1.3.4 vendored** (`third_party/minhook`, BSD-2) — the D3D backend
  hook engine; notices updated.
- `rl::backend::IHookEngine` (MinHook engine on Windows, honest stub elsewhere)
  and read-only `rl::backend::VTable` (COM slot reading/anchor resolution).
- `rl::backend`: platform-neutral **steering core** — `SteeringPolicy`
  (display-sized color targets → internal resolution; depth/aux never
  touched; native rung is identity/skip) and `RenderTargetRegistry`
  (per-frame bind tracking feeding pass classification).
- `RenderLift.D3D11.dll` is now a real SHARED module on Windows: probe-device
  vtable bootstrap, MinHook detours on `IDXGISwapChain::Present` and
  `ResizeBuffers` (measured passthrough + display tracking feeding the
  steering policy), injection contract `RenderLiftInstall/Uninstall/
  FrameCount`, DllMain loader-lock discipline.
- Tests: vtable slot stability/calling, hook-engine stub honesty, steering
  policy + registry suites (51 checks total).
- Docs: ADR 0002 (hook engine + injection decisions), D3D11 vtable slot map.

## [0.1.0] — 2026-09-21 · ALRR Spatial 0.1

Initial public scaffold. GTA V is declared the first test lab; the project is born multi-game and multi-API.

### Added — repository
- Repo-wide structure: `src/`, `backends/`, `shaders/`, `profiles/`, `tests/`, `tools/`, `docs/`, `third_party/`.
- MIT license, contributing and security policies, third-party notices file.
- CMake build (≥ 3.20, C++20) + `CMakePresets.json`; GitHub Actions CI (Linux + Windows).
- Original GTA V feasibility research moved to `docs/research/2026-09-gta-v-alrr-feasibility.md`.

### Added — ALRR Core (API-agnostic engine)
- `core`: shared types (`Resolution`, `GraphicsApi`, `ReconstructionMode`, `Bottleneck`), minimal JSON parser, game-profile loader.
- `resolution`: `ResolutionManager` (scale-factor ladder with even-dimension rounding, explicit per-game ladders) and `DynamicResolutionController` (GPU/CPU-aware, with warmup + cooldown hysteresis; refuses to downscale when CPU-bound).
- `reconstruction`: `IReconstructionStage` interface, **ALRR Spatial** and **ALRR Edge** CPU reference implementations; NIS-fallback/Temporal reserved behind the factory.
- `renderer`: frame-pipeline description (where reconstruction sits: after post-process, before UI composite and present).
- `detection`: system probe interface + null implementation (DXGI/WMI probing planned).
- `profiling`: frame timer + rolling averages.

### Added — backends
- Backend module interface (`rl::backend::IBackend`) with per-API planned hook targets.
- Module stubs: D3D9, D3D10, D3D11 (first target), D3D12, Vulkan.

### Added — shaders
- `shaders/spatial`: ALRR Spatial upscale + sharpen (HLSL compute + GLSL/Vulkan variant).
- `shaders/edge`: ALRR Edge edge-adaptive reconstruction (HLSL, reference-quality).
- `shaders/sharpen`: standalone CAS-lite sharpen pass.
- `shaders/temporal`: design notes.

### Added — profiles, tools, tests
- Profiles: `generic/default`, `games/grand-theft-auto-v` (1366×768 lab, pinned ladder 426×240→1366×768), hardware `intel-uhd-620`, `amd-vega-mobile`.
- `RenderLift.CLI`: profile inspector, resolution-ladder calculator, dynamic-resolution simulator.
- Test suite (core types, JSON, profiles, resolution manager, controller behavior, CPU reconstruction, backend registry) wired to CTest.

[Unreleased]: https://github.com/edmundo738/renderlift/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/edmundo738/renderlift/releases/tag/v0.1.0
