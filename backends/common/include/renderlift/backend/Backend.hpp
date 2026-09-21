// RenderLift — backend interface.
//
// ALRR Core is API-agnostic; a backend is the thin, API-specific layer that:
//   1. intercepts the game's device/swapchain creation,
//   2. steers render-target/viewport configuration to the internal
//      resolution chosen by the dynamic controller ("steer, don't resize"),
//   3. runs the ALRR reconstruction pass on the internal image,
//   4. composites UI at native resolution and presents.
//
// Backends ship as independent modules (RenderLift.D3D9.dll …
// RenderLift.Vulkan.dll) so the same engine binary serves every API. Until
// the hook engine lands (RenderLift 0.2, MinHook-based — see
// THIRD_PARTY_NOTICES.md) these classes are registry stubs that document
// their planned hook targets and compile everywhere.
#pragma once

#include "renderlift/core/Types.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace rl::backend {

struct BackendInfo {
    GraphicsApi api = GraphicsApi::Unknown;
    std::string moduleName;              // e.g. "RenderLift.D3D11"
    Version version{0, 1, 0};
    bool hooksImplemented = false;       // false while we are a stub
};

enum class BackendState : std::uint8_t {
    Uninitialized,
    Ready,      // module loaded/registered; hooking pending (see hooksImplemented)
    Hooked,     // actively intercepting a game process (0.2+)
    Failed,
};

class IBackend {
public:
    virtual ~IBackend() = default;

    [[nodiscard]] virtual const BackendInfo& info() const noexcept = 0;
    [[nodiscard]] virtual BackendState state() const noexcept = 0;

    // Export names this backend will intercept — the integration contract per
    // API (docs/apis/). Returned now, enforced by tests, wired by 0.2.
    [[nodiscard]] virtual std::vector<std::string_view> plannedHookTargets() const = 0;

    // Registers the module. Returns false when the API is unavailable on this
    // machine (e.g. no Vulkan loader). Hooking itself is not part of this call.
    virtual bool initialize() = 0;
    virtual void shutdown() noexcept = 0;
};

}  // namespace rl::backend
