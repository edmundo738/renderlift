// RenderLift — umbrella header for the ALRR Core public API.
//
// RenderLift = the product. ALRR (Adaptive Low-Resolution Reconstruction) =
// the reconstruction engine inside it. Everything under rl:: and rl::alrr::
// is API-agnostic; API-specific interception lives in backends/.
#pragma once

#include "renderlift/core/Types.hpp"
#include "renderlift/core/Json.hpp"
#include "renderlift/core/GameProfile.hpp"

// Defined by the build system (CMake): "0.1.0" / ALRR version string.
#ifndef RENDERLIFT_VERSION
#define RENDERLIFT_VERSION "0.0.0-dev"
#endif
#ifndef ALRR_CORE_VERSION
#define ALRR_CORE_VERSION "0.0.0-dev"
#endif

namespace rl {

[[nodiscard]] inline constexpr const char* productName() noexcept { return "RenderLift"; }
[[nodiscard]] inline constexpr const char* engineName() noexcept { return "ALRR Core"; }
[[nodiscard]] inline const char* productVersion() noexcept { return RENDERLIFT_VERSION; }
[[nodiscard]] inline const char* engineVersion() noexcept { return ALRR_CORE_VERSION; }

}  // namespace rl
