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
- **Landscape study** `docs/research/2026-09-d3d11-landscape-and-gta-v.md`:
  comparison with gta5_fsr (proxy-DLL + native Frame Scaling is GTA V's real
  integration point), Courrèges pipeline anatomy, ReShade anti-cheat/depth
  heuristics, kiero/GH_D3D11_Hook hook-engine parity; roadmap re-prioritizes
  0.3 around native Frame Scaling + upscale-pass replacement.
- Windows CI: build on VS 2026 runners, capture compiler logs, publish the
  lab pack (DLL+Loader+CLI+tests) via the `dist-pack` branch; MSVC fixes
  (hook-table braces, dllimport switch, registry kernel split — LNK2019).
- Tests: 16 new checks (wire-format round-trips, garbage rejection,
  aggregation, classifier rules incl. square/shadow atlases, integration
  modes, GTA V profile = observe) — 67 checks total.

### Fixed — first GTA V live lab runs (2026-09-21, Legacy 1.0.3889.0)
- **Loader export resolution**: `LoadLibraryExW(…DATAFILE) + GetProcAddress`
  returned NULL against an intact export table (first live run failure).
  The loader now parses the PE Export Directory itself (`findExportRva`) and
  computes `remote VA = remote base + RVA`; CI enforces the export contract
  (`RenderLiftInstall/Uninstall/FrameCount` present, unmangled) and
  publishes the evidence file with every build.
- **D3D11 module init crash** (`0xc0000005`, WER fault offset bit-exact with
  the `RenderLiftInstall` entry VA, before any log line): install rewritten
  "evidence-first" per ADR 0003 — exports now carry the exact thread-proc
  shape `HRESULT WINAPI fn(LPVOID)`; durable `RLCAP1 install cp=N`
  checkpoints from the first effective instruction (`fopen/fputs/fclose`,
  no STL/globals/locks); `ModuleState` built explicitly (magic static
  removed); log path next to the DLL (never the game dir) with `%TEMP%`
  fallback; full install under an SEH guard whose filter records phase +
  exception code + exact `ExceptionAddress` and returns a coded HRESULT
  (`0xE<phase><code>`; ordinary failures `0x8000A001…A030`). Failure is
  reported with evidence, never masked; success still means fully armed.
  See `docs/research/d3d11-research-layer.md` §5.1 and
  `docs/apis/d3d11.md` (install evidence protocol).
- **CI dist-pack publish** is now additive (clone + refresh) instead of a
  force-pushed orphan branch, so manually published lab zips/notes survive
  subsequent runs.
- **v3 retest lesson → install evidence protocol v3.1**: cp=1 ran outside
  the entry `__try` and its chain was still CRT-heavy (`vsnprintf` +
  `fopen_s`/`fputs`/`fclose`), so a crash there was indistinguishable from a
  pre-DLL failure. The whole observable entry now runs inside the SEH
  guard on a **kernel32-only transport** (`CreateFileA`/`WriteFile`, hand-
  rolled formatter — zero CRT stdio/heap/locks, same path used by the SEH
  filter); a **binary entry mark** (`RenderLift.entry`, 32 bytes,
  `CREATE_ALWAYS`, magic `RLENT01`) is written first as a yes/no witness to
  reaching the first observable byte; every line goes to **two sinks**
  (module dir + `%TEMP%`). Exports unchanged; observe-only unchanged.
- **Frontier isolation experiment (v3.2)**: after the v3.1 retest returned
  `0xC0000005` with no entry mark AND no log in either sink, the boundary
  `CreateRemoteThread → remote entry` gets its own instrumented probe:
  new export `RenderLiftEntryProbe` (absolute-minimum stub — kernel32 IAT
  only, one read-only literal, the LPVOID param; no CRT/STL/globals/D3D11/
  MinHook/SEH; writes `RLPROBE1 ok` and always returns `0x12345678`), loader
  `--param <ansi>` staging, `VirtualQueryEx` validation of the entry page
  (State/Type/Protect/AllocationBase — aborts the call when non-executable,
  diagnosis without a crash), and process-mitigation dumping
  (DEP/ASLR/CFG/ACG/Signature/ImageLoad/ExtensionPoint). Probe works →
  frontier is healthy, focus returns to `RenderLiftInstall`; probe also
  dies `0xC0000005` → the failure is in remote-thread delivery itself.
- **ROOT CAUSE of the v2–v3.1 crash saga (loader)**: `GetExitCodeThread`
  returns a 32-bit DWORD, but remote `LoadLibraryW` returns a 64-bit
  HMODULE — when GTA V maps the module above 4 GB, the loader used the
  TRUNCATED low half as the remote base. RVA-correct + base-wrong =
  unmapped VA: the probe run proved it with `VirtualQueryEx` (FREE /
  NOACCESS) and the earlier runs crashed with instruction-fetch
  `0xC0000005` at that exact wrong VA (our DLL code never executed — the
  D3D11/MinHook/ABI candidates were innocent all along). Fixed: after a
  non-zero exit code (success proof only), the real base is resolved via
  `EnumProcessModulesEx(LIST_MODULES_ALL)` + `GetModuleBaseNameW`; the
  entry-page `VirtualQueryEx` guard stays as the final check.

### Added — 0.2 draw-path proof (v3.4, after v3.3 full-chain PASS)
- **Context slot coverage 9 → 14 hooks** in `RenderLift.D3D11.dll`: the
  drawstat legacy path (`DrawIndexed`(12)/`Draw`(13)) is joined by
  `DrawIndexedInstanced`(20), `DrawInstanced`(21), `DrawAuto`(38),
  `DrawIndexedInstancedIndirect`(39) and `DrawInstancedIndirect`(40) — all
  counting-only measured passthroughs (still ADR 0003 observe: no redirect,
  no resolution/shader/visual change, no steering).
- **First-fire evidence** for every context hook:
  `RLCAP1 first slot=<name> ctx=0x…` once per slot per window.
- **Distinct-context tracking**: `RLCAP1 context first=0x…` / `context
  new=0x…`, bounded 32-entry set, overflow counted for the summary.
- **Per-frame diagnostic line** `RLCAP1 draws frame=N d= di= diinst= dinst=
  dauto= diind= dinstind= om= vp=` — raw line (the legacy
  `drawstat`/`cap frames=` wire format is byte-identical; the parser
  tolerates unknown tags and the offline inspector skips them).
- **Window summary at the cap**: `RLCAP1 summary frames=N … draws=T ctxs=C
  ctxovf=O verdict=DRAWPATH_ACTIVE|DRAWPATH_ZERO` written before the legacy
  `RLCAP1 cap frames=N` terminator — answers "which path did the game
  actually use" without reading thousands of per-frame lines.
- **Re-arm**: a remote `RenderLiftInstall` call while already installed is
  no longer a silent no-op — it reopens a fresh observation window in place
  (`RLCAP1 rearm cap=N`): frame counter, per-frame/window counters,
  first-fire mask and context set reset; hooks stay installed; the DLL is
  never reloaded; the loader is untouched.
- **Default observation window 600 → 2000 frames** (the v3.4 main lab
  window; `RENDERLIFT_OBSERVE_FRAMES` still overrides, read from the target
  process environment — set before launching the game).
- Research doc §5.2 records the H1/H2/H3 hypothesis separation (slot
  coverage vs context path vs focus-state contamination) and the
  post-test decision tree; success criterion for this checkpoint is the
  empirical draw path, not `Install=0`/`Present observed`.

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
