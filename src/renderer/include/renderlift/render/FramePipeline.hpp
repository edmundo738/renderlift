// RenderLift — ALRR Core: the frame pipeline contract.
//
// This module documents, in code, where ALRR sits in a game's frame. The
// whole point of the project: reconstruction happens on the *internal* 3D
// image AFTER post-processing, while HUD/UI is composited afterwards at
// native display resolution. A Present-only resize would land too late —
// the expensive 3D work would already be done at display resolution.
//
//   InternalRender → ShadowMaps → Lighting → PostProcess →
//   ▶ ALRR Reconstruction → Sharpening → UiComposite (native) → Present
//
// Backends enforce this order per API; this header is the shared vocabulary.
#pragma once

#include "renderlift/core/Types.hpp"

#include <cstdint>
#include <stdexcept>

namespace rl::render {

enum class PipelineStage : std::uint8_t {
    InternalRender,  // 3D scene at internal resolution
    ShadowMaps,
    Lighting,
    PostProcess,     // SSR / AO / bloom — must run at internal resolution
    Reconstruction,  // ◀ ALRR starts here
    Sharpening,
    UiComposite,     // HUD/menus/text at native display resolution
    Present,
};

// Everything a backend needs to set up one frame path.
struct RenderPath {
    Resolution display{};               // final output resolution
    Resolution internal{};              // 3D render resolution (ladder rung)
    bool uiNative = true;               // compose HUD/UI at display res
    ReconstructionMode reconstruction = ReconstructionMode::Edge;
    float sharpening = 0.35f;

    // Fraction of native pixel workload the 3D render actually costs
    // (e.g. 640×360 over 1366×768 ≈ 0.22 — the "lift" RenderLift buys).
    [[nodiscard]] double renderLoadRatio() const {
        if (!display.valid() || !internal.valid()) return 1.0;
        return static_cast<double>(internal.pixelCount()) /
               static_cast<double>(display.pixelCount());
    }
};

// Throws std::invalid_argument when the path is incoherent.
inline void validate(const RenderPath& path) {
    if (!path.display.valid()) {
        throw std::invalid_argument("RenderPath: display resolution must be non-zero");
    }
    if (!path.internal.valid()) {
        throw std::invalid_argument("RenderPath: internal resolution must be non-zero");
    }
    if (path.internal.pixelCount() > path.display.pixelCount()) {
        throw std::invalid_argument("RenderPath: internal resolution exceeds display resolution");
    }
    if (path.internal.width < 160 || path.internal.height < 90) {
        throw std::invalid_argument("RenderPath: internal resolution below the 160×90 minimum");
    }
    if (path.sharpening < 0.0f || path.sharpening > 1.0f) {
        throw std::invalid_argument("RenderPath: sharpening must be within [0, 1]");
    }
}

}  // namespace rl::render
