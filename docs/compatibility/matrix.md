# Compatibility matrix

Status legend: 🧪 in-lab (active target) · 📋 planned · 🧊 icebox · ✅ working

## Games (labs)

| Title | API | Status | Notes |
|---|---|---|---|
| Grand Theft Auto V | D3D11 | 🧪 **first lab** | UHD 620 @ 1366×768; profile `games/grand-theft-auto-v.json`; single-player only |
| The Elder Scrolls V: Skyrim (2011) | D3D9 | 📋 | classic low-end catalog; D3D9 backend wave |
| Fallout 4 | D3D11 | 📋 | second D3D11 lab — different RT chain, good generalization test |
| Grand Theft Auto IV | D3D9 | 📋 | notoriously CPU-bound: perfect demo of the "hold when CPU-bound" rule |
| Custom / generic | any | 📋 | `generic/default.json` fallback profile |

A game graduates to ✅ only with measured before/after FPS + image quality
notes committed to `docs/research/` (or linked benches).

## Hardware classes (profiles/hardware/)

| Class | Examples | Status | Suggested start |
|---|---|---|---|
| Intel UHD 6xx (8th-gen mobile) | UHD 620 | 🧪 reference lab GPU | extreme tier, D3D11, ALRR Edge @ 0.35 |
| Intel Iris Xe | Iris Xe G7 | 📋 | performance tier |
| AMD Vega APU | Vega 8/11 mobile | 📋 | performance tier, Vulkan viable |
| Older NVIDIA dGPU | Kepler/Maxwell low-end | 🧊 | quality tier, NIS fallback candidate |

## Explicitly out of scope

- Online/multiplayer modes of any title (anti-cheat — see `SECURITY.md`).
- Consoles, ARM, macOS Metal: architecture stays API-agnostic, but no active
  work tracks them.
