// RenderLift — ALRR: reconstruction stage interface.
//
// This is the heart of the engine. Every reconstruction tier (ALRR Spatial,
// ALRR Edge, NIS fallback, ALRR Temporal) implements IReconstructionStage and
// processes plain image views — no D3D, no Vulkan. Backends adapt their GPU
// resources to these stages; the CPU reference implementations below double
// as correctness baselines for the shader ports under shaders/.
#pragma once

#include "renderlift/core/Types.hpp"

#include <cstdint>
#include <memory>

namespace rl::alrr {

// RGBA8 view over externally-owned memory. `stride` is bytes per row
// (0 == tightly packed).
struct ConstImageView {
    const std::uint8_t* data = nullptr;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t stride = 0;
};

struct ImageView {
    std::uint8_t* data = nullptr;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t stride = 0;

    // Mutable views decay freely into const views.
    operator ConstImageView() const {  // NOLINT(google-explicit-constructor)
        return {data, width, height, stride};
    }
};

struct ReconstructParams {
    float sharpening = 0.35f;  // 0..1 — kept low for very low-res sources
};

class IReconstructionStage {
public:
    virtual ~IReconstructionStage() = default;

    [[nodiscard]] virtual ReconstructionMode mode() const noexcept = 0;
    [[nodiscard]] virtual Version tierVersion() const noexcept = 0;

    // Reconstructs src (internal resolution) into dst (display resolution).
    // Implementations must support arbitrary up/down scaling between the two.
    // Throws std::invalid_argument on malformed views.
    virtual void apply(ConstImageView src, ImageView dst,
                       const ReconstructParams& params) = 0;
};

// Factory. Current support:
//   Spatial     → ALRR Spatial (CPU reference, always available)
//   Edge        → ALRR Edge (CPU reference, always available)
//   NisFallback → throws: requires a GPU backend (planned 0.2)
//   Temporal    → throws: planned for RenderLift 1.0
[[nodiscard]] std::unique_ptr<IReconstructionStage> createReconstructionStage(
    ReconstructionMode mode);

}  // namespace rl::alrr
