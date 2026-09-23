// RenderLift — shared implementation for pre-hook backend stubs.
//
// Every backend currently derives from StubBackend: it implements the
// IBackend bookkeeping so each backend file only carries what is unique —
// its identity and its planned hook targets. When the hook engine lands, the
// per-API classes will replace this base with real interception code without
// changing the IBackend contract.
#pragma once

#include "renderlift/backend/Backend.hpp"

namespace rl::backend {

class StubBackend : public IBackend {
public:
    StubBackend(GraphicsApi api, std::string moduleName, std::vector<std::string_view> hookTargets)
        : info_{api, std::move(moduleName), Version{0, 1, 0}, /*hooksImplemented=*/false},
          hookTargets_(std::move(hookTargets)) {}

    [[nodiscard]] const BackendInfo& info() const noexcept override { return info_; }
    [[nodiscard]] BackendState state() const noexcept override { return state_; }

    [[nodiscard]] std::vector<std::string_view> plannedHookTargets() const override {
        return hookTargets_;
    }

    bool initialize() override {
        state_ = BackendState::Ready;
        return true;
    }

    void shutdown() noexcept override { state_ = BackendState::Uninitialized; }

private:
    BackendInfo info_;
    std::vector<std::string_view> hookTargets_;
    BackendState state_ = BackendState::Uninitialized;
};

}  // namespace rl::backend
