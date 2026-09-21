# ADR 0002 — MinHook detours + loader injection for the D3D backends

- Status: **accepted**
- Date: 2026-09-21
- Deciders: edmundo738 + Arena agent

## Context

Interception of D3D calls can be done several ways. We evaluated:

| Technique | How | Assessment |
|---|---|---|
| **MinHook detours** on vtable-resolved addresses | patch function prologues, trampoline to ours | battle-tested (ReShade, Special-K lineage), uninstalls cleanly, works per-process |
| vtable slot patching | overwrite a slot in the interface vtable | **rejected**: vtables live in read-only sections (.rdata/.rodata) — writing without changing page protection crashes; compiler devirtualization can also bypass slots entirely |
| vptr swap per object | point one object at our own vtable | viable for the swap chain instance only; doesn't cover device/context methods; fragile when games recreate objects — kept as fallback |
| proxy `d3d11.dll`/`dxgi.dll` in the game folder | intercept exports at load time | strong (covers everything), but intrusive to install/uninstall per game and flagged by more anti-cheats — kept as optional later |

Delivery mechanisms: ASI-style plugins require per-game middleware that many
titles lack; `LoadLibrary` injection from a small loader is uniform and lets
us install hooks lazily, once the game created its device.

## Decision

1. **Engine: vendored MinHook 1.3.4** (`third_party/minhook`, BSD-2) behind
   the `rl::backend::IHookEngine` interface (swappable if a better engine
   appears). Default and only strategy: **detours on function bodies**.
2. **Address discovery: probe-device technique** — create a throwaway
   device + swap chain on a hidden window, read the (stable, process-wide)
   vtable slots (`Present` #8, `ResizeBuffers` #13 for `IDXGISwapChain`).
3. **Delivery: loader injection** — `RenderLift.exe` starts/attaches to the
   game process, `LoadLibrary`s `RenderLift.D3D11.dll`, calls its exported
   `RenderLiftInstall()`. Hooking never runs inside `DllMain` (loader lock).
4. **Uninstall is a first-class path**: `RenderLiftUninstall`, plus a
   `DLL_PROCESS_DETACH` safety net so a crashing loader can never leave
   calls into unloaded code.
5. VTable access stays **read-only** (`rl::backend::VTable`) — documented
   after the read-only-section segfault in early tests.

## Consequences

- D3D11 module flips to a shared library on Windows (`RenderLift.D3D11.dll`).
- Every future backend inherits the same shape: probe → resolve → detour →
  enable; only slot indices and COM types differ.
- Anti-cheat stance unchanged (SECURITY.md): single-player/offline only;
  proxy-DLL deployment, if ever added, must ship behind an explicit opt-in.
