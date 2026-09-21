# RenderLift

**Render less. Reconstruct more.**

RenderLift is a cross-API low-resolution rendering and reconstruction framework for low-end gaming PCs. It aims to reduce the cost of 3D rendering by running a game at a lower internal resolution and reconstructing the final image at the target display resolution.

**ALRR** means Adaptive Low-Resolution Reconstruction. ALRR is the internal rendering/reconstruction technology; RenderLift is the product and project name.

## First laboratory

The first laboratory target is GTA V Legacy on low-end hardware, especially integrated GPUs such as Intel UHD 620.

- API: Direct3D 11
- Display: 1366x768
- Internal ladder: 426x240, 480x270, 640x360, 854x480, 1024x576, 1366x768
- Reconstruction: ALRR Edge
- Native-resolution UI when the game integration permits it
- Dynamic resolution only after fixed-resolution reconstruction is proven

## Architecture

The shared core owns resolution, reconstruction policy, profiling and metrics. API backends own API-specific resource tracking and integration.

Desired rendering path:

Game renderer -> lower-cost internal render -> ALRR reconstruction -> sharpening -> native UI composition -> Present

A Present hook alone is not sufficient to guarantee a performance gain: the project ultimately needs to reduce the cost of the game's internal 3D rendering.

## Backends

| Backend | Role | Status |
| --- | --- | --- |
| Direct3D 11 | First real laboratory | Scaffold |
| Direct3D 9 | Older games | Scaffold |
| Direct3D 10 | Compatibility | Scaffold |
| Direct3D 12 | Explicit-resource API | Scaffold |
| Vulkan | Cross-platform graphics API | Scaffold |

## Reconstruction roadmap

- ALRR Spatial: very cheap spatial reconstruction.
- ALRR Edge: edge-aware reconstruction for aggressive low resolutions.
- ALRR Temporal: future temporal reconstruction using reliable frame history and motion information.
- Optional NIS fallback: compatibility technology, not the identity of RenderLift.

## Status

This is an early engineering/research project. The foundation deliberately keeps real game interception behind backend interfaces until each integration can be tested safely and measurably.

## Fair-play

RenderLift is intended for offline and single-player graphics experimentation. Do not use injection or hooking components in online games protected by anti-cheat systems. GTA Online is not a project target.

## License

MIT.
