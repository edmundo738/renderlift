# RenderLift architecture

RenderLift has two layers.

1. ALRR/Core: API-agnostic resolution, reconstruction policy, profiles and metrics.
2. Backends: API-specific integration and resource tracking.

The desired path is:

Game renderer
  -> lower-cost internal render target
  -> ALRR reconstruction
  -> sharpening
  -> native UI composition
  -> Present

A Present hook by itself may only resize an already-rendered image. The performance objective therefore requires a game/API integration capable of reducing the expensive internal 3D render.

D3D11 is the first practical laboratory. D3D12 is intentionally treated as a different architecture because command lists, resource states and barriers are explicit. Vulkan likewise has its own swapchain and presentation lifecycle.
