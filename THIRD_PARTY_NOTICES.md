# Third-Party Notices

RenderLift itself is licensed under the [MIT License](LICENSE).

No third-party code is vendored in the repository at this time. The following
dependencies are planned and will be vendored under `third_party/` with their
licenses preserved and recorded here when integrated:

| Component | Purpose | License |
|-----------|---------|---------|
| [MinHook](https://github.com/TsudaKageyu/minhook) | API hooking backend for D3D9–D3D12 | BSD 2-Clause |
| [volk](https://github.com/zeux/volk) | Vulkan function loading for the Vulkan backend | MIT |
| [NVIDIA Image Scaling SDK](https://github.com/NVIDIADeveloper/NVIDIAImageScaling) *(reference/optional)* | NIS reconstruction fallback shaders | MIT |

Research/reference materials (not redistributed) are credited in `docs/research/`.
If you add a dependency, update this file and `third_party/README.md` in the same commit.
