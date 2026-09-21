// RenderLift — ALRR Core: bridge from GameProfile (data) to runtime plans.
//
// Profiles under profiles/ are pure data. This header turns them into the
// concrete objects the runtime uses: a resolved resolution ladder, a sensible
// starting rung and a dynamic-resolution controller configuration.
#pragma once

#include "renderlift/core/GameProfile.hpp"
#include "renderlift/resolution/DynamicResolutionController.hpp"
#include "renderlift/resolution/ResolutionManager.hpp"

#include <algorithm>
#include <vector>

namespace rl::resolution {

// The ladder the game will actually use: the profile's explicit ladder
// (normalized) or, when absent, the generic factor ladder for its display.
[[nodiscard]] inline std::vector<Resolution> resolveLadder(const core::GameProfile& profile,
                                                           Resolution runtimeDisplay = {}) {
    const Resolution display = profile.display.valid() ? profile.display : runtimeDisplay;
    if (!display.valid()) {
        throw std::invalid_argument(
            "resolveLadder: profile has no display resolution and none was provided");
    }
    if (profile.internalLadder.empty()) {
        return ResolutionManager::ladderForDisplay(display);
    }
    return ResolutionManager::normalizeLadder(profile.internalLadder, display);
}

[[nodiscard]] inline std::size_t resolveStartLevel(const core::GameProfile& profile,
                                                   std::size_t ladderSize) {
    if (ladderSize == 0) {
        throw std::invalid_argument("resolveStartLevel: empty ladder");
    }
    return std::min(profile.defaultLevelIndex, ladderSize - 1);
}

[[nodiscard]] inline ControllerConfig controllerConfigFrom(const core::GameProfile& profile) {
    ControllerConfig config;
    config.targetFrameMs = 1000.0 / profile.dynamicResolution.targetFps;
    config.warmupFrames = profile.dynamicResolution.warmupFrames;
    config.cooldownFrames = profile.dynamicResolution.cooldownFrames;
    return config;
}

}  // namespace rl::resolution
