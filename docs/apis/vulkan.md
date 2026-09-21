# Vulkan backend — integration plan

**Priority: third wave / catalog coverage.** Own model: loader + layers,
explicit swapchain and synchronization.

## Presentation model

`vkCreateSwapchainKHR` → `vkAcquireNextImageKHR` → render →
`vkQueuePresentKHR`. The acquired image's layout transitions
(`UNDEFINED/PRESENT_SRC_KHR ↔ GENERAL`) are the counterpart of D3D12
barriers. Reconstruction = compute dispatch into the acquired image (or into
an engine image + final blit), submitted on the game's queue family or our
own.

## Planned hook targets (module contract)

- `vkCreateSwapchainKHR` — learn extent/format; steer where possible
- `vkAcquireNextImageKHR` / `vkQueuePresentKHR` — frame boundary
- `vkCreateImageView` — scene-RT registry heuristic
- `vkCmdSetViewport` — viewport steering (dynamic-viewport games)
- `vkCmdBeginRendering` / `vkCmdBeginRenderPass` — pass classification (VK 1.3 dynamic rendering + classic)

## Delivery vehicle

Two candidate shapes; decide at implementation time (log decision as ADR):

1. **Implicit layer** — maximum hook surface, loader-managed, but global
   (affects every Vulkan app while enabled).
2. **Per-process injection** like the D3D backends — consistent UX with the
   rest of RenderLift, less global surface.

Function loading through **volk** (planned third_party dep, MIT). No ICD
modification, ever.
