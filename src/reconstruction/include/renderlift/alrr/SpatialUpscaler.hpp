// RenderLift — ALRR Spatial (tier 0.1): spatial reconstruction, CPU reference.
//
// Two passes:
//   1. center-aligned bilinear resampling (arbitrary scale factors), followed by
//   2. a 3×3 unsharp mask whose strength is driven by ReconstructParams.
//
// The GPU twin is shaders/spatial/alrr_spatial.* — this implementation defines
// the expected output within tolerance and is used by tests and by offline
// tooling (profile previews, A/B screenshots).
#pragma once

#include "renderlift/alrr/Reconstruction.hpp"

namespace rl::alrr {

class SpatialUpscaler final : public IReconstructionStage {
public:
    [[nodiscard]] ReconstructionMode mode() const noexcept override {
        return ReconstructionMode::Spatial;
    }
    [[nodiscard]] Version tierVersion() const noexcept override { return kAlrrSpatialVersion; }

    void apply(ConstImageView src, ImageView dst, const ReconstructParams& params) override;
};

}  // namespace rl::alrr
