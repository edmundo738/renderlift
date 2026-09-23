// RenderLift — ALRR Edge (tier 0.5, experimental): edge-adaptive reconstruction.
//
// Same center-aligned resampling as ALRR Spatial, but sharpening is modulated
// per pixel by the local luma gradient: strong edges get real sharpening,
// flat areas stay clean (no halos/noise amplification in sky, fog, skin).
//
// Deliberately conservative today — it exists to anchor the interface and the
// tests while the GPU-first version (shaders/edge/alrr_edge.hlsl) evolves.
#pragma once

#include "renderlift/alrr/Reconstruction.hpp"

namespace rl::alrr {

class EdgeUpscaler final : public IReconstructionStage {
public:
    [[nodiscard]] ReconstructionMode mode() const noexcept override {
        return ReconstructionMode::Edge;
    }
    [[nodiscard]] Version tierVersion() const noexcept override { return kAlrrEdgeVersion; }

    void apply(ConstImageView src, ImageView dst, const ReconstructParams& params) override;
};

}  // namespace rl::alrr
