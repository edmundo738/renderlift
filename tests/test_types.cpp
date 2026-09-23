// RenderLift tests — core shared types.
#include "renderlift/RenderLift.hpp"

#include "rl_test.hpp"

#include <cmath>

RL_TEST(types_resolution_basics) {
    constexpr rl::Resolution r{1366, 768};
    RL_CHECK(r.valid());
    RL_CHECK(r.pixelCount() == 1366ull * 768ull);
    RL_CHECK_NEAR(r.aspectRatio(), 1366.0 / 768.0, 1e-9);
    RL_CHECK(!rl::Resolution{}.valid());
}

RL_TEST(types_graphics_api_roundtrip) {
    RL_CHECK(rl::graphicsApiFromString("d3d11").value() == rl::GraphicsApi::D3D11);
    RL_CHECK(rl::graphicsApiFromString("vulkan").value() == rl::GraphicsApi::Vulkan);
    RL_CHECK(rl::toString(rl::GraphicsApi::D3D9) == "d3d9");
    RL_CHECK(rl::graphicsApiFromString("auto").value() == rl::GraphicsApi::Unknown);
    RL_CHECK(!rl::graphicsApiFromString("metal").has_value());
}

RL_TEST(types_reconstruction_mode_roundtrip) {
    RL_CHECK(rl::reconstructionModeFromString("edge").value() == rl::ReconstructionMode::Edge);
    RL_CHECK(rl::reconstructionModeFromString("nis").value() == rl::ReconstructionMode::NisFallback);
    RL_CHECK(rl::toString(rl::ReconstructionMode::Temporal) == "temporal");
    RL_CHECK(!rl::reconstructionModeFromString("dlss").has_value());
}

RL_TEST(types_version_strings) {
    RL_CHECK(rl::kAlrrSpatialVersion.toString() == "0.1.0");
    RL_CHECK(rl::kAlrrTemporalVersion.toString() == "1.0.0");
    RL_CHECK(rl::kAlrrSpatialVersion < rl::kAlrrEdgeVersion ||
             rl::kAlrrSpatialVersion.toString() != rl::kAlrrEdgeVersion.toString());
}
