# Third-Party Notices

RenderLift itself is licensed under the [MIT License](LICENSE).

## Vendored components

| Component | Version | License | Purpose | Location |
|-----------|---------|---------|---------|----------|
| [MinHook](https://github.com/TsudaKageyu/minhook) | 1.3.4 | BSD 2-Clause | API hooking engine for the D3D9–D3D12 backends | `third_party/minhook/` |

MinHook copyright (c) 2009-2018 Tsuda Kageyu. Its license text is preserved in
`third_party/minhook/LICENSE.txt`.

## Planned (not yet vendored)

| Component | Purpose | License |
|-----------|---------|---------|
| [volk](https://github.com/zeux/volk) | Vulkan function loading for the Vulkan backend | MIT |
| [NVIDIA Image Scaling SDK](https://github.com/NVIDIADeveloper/NVIDIAImageScaling) *(optional)* | NIS reconstruction fallback shaders | MIT |

Research/reference materials (not redistributed) are credited in `docs/research/`.
If you add a dependency, update this file and `third_party/README.md` in the same commit.
