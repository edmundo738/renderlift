#include "renderlift/alrr/Reconstruction.hpp"

#include "renderlift/alrr/EdgeUpscaler.hpp"
#include "renderlift/alrr/SpatialUpscaler.hpp"

#include <stdexcept>

namespace rl::alrr {

std::unique_ptr<IReconstructionStage> createReconstructionStage(ReconstructionMode mode) {
    switch (mode) {
        case ReconstructionMode::Spatial:
            return std::make_unique<SpatialUpscaler>();
        case ReconstructionMode::Edge:
            return std::make_unique<EdgeUpscaler>();
        case ReconstructionMode::NisFallback:
            throw std::runtime_error(
                "ALRR: NIS fallback requires a GPU backend (planned for RenderLift 0.2)");
        case ReconstructionMode::Temporal:
            throw std::runtime_error("ALRR: Temporal reconstruction is planned for RenderLift 1.0");
        default:
            throw std::invalid_argument("ALRR: unknown reconstruction mode");
    }
}

}  // namespace rl::alrr
