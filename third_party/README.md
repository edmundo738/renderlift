# third_party

Third-party code vendored into RenderLift lives here. **Nothing is vendored
yet** — this directory and [THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md)
exist so the first dependency lands with its process already in place.

## Rules

1. The dependency's license must be compatible with MIT (MIT, BSD, Zlib, Apache-2.0).
2. Vendor sources under `third_party/<name>/` including its LICENSE file.
3. Record the component, version/commit and purpose in `THIRD_PARTY_NOTICES.md`
   in the same commit.
4. Prefer small, single-purpose libraries; discuss in an issue before adding
   anything with its own build system.
5. Never vendor anything that circumvents anti-cheat, DRM, or online
   protections (see SECURITY.md).

## Vendored

| Component | Version | For | Landed |
|---|---|---|---|
| MinHook | 1.3.4 | D3D9–D3D12 hook engine | RenderLift 0.2 groundwork |

## Planned

| Component | For | When |
|---|---|---|
| volk | Vulkan function loading | Vulkan backend |
| NVIDIA Image Scaling shaders | NIS reconstruction fallback | optional tier |
