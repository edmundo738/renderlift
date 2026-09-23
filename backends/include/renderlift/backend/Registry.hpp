// RenderLift — backend registry: every module, one include.
//
// Tools (and later the loader/injector) enumerate backends through this
// header. Each factory returns a module implementing rl::backend::IBackend.
#pragma once

#include "renderlift/backend/Backend.hpp"

#include <memory>
#include <vector>

namespace rl::backend {

[[nodiscard]] std::unique_ptr<IBackend> createD3D9Backend();
[[nodiscard]] std::unique_ptr<IBackend> createD3D10Backend();
[[nodiscard]] std::unique_ptr<IBackend> createD3D11Backend();
[[nodiscard]] std::unique_ptr<IBackend> createD3D12Backend();
[[nodiscard]] std::unique_ptr<IBackend> createVulkanBackend();

[[nodiscard]] inline std::vector<std::unique_ptr<IBackend>> createAllBackends() {
    std::vector<std::unique_ptr<IBackend>> all;
    all.push_back(createD3D9Backend());
    all.push_back(createD3D10Backend());
    all.push_back(createD3D11Backend());
    all.push_back(createD3D12Backend());
    all.push_back(createVulkanBackend());
    return all;
}

}  // namespace rl::backend
