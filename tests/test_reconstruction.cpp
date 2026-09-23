// RenderLift tests — ALRR CPU reference reconstruction stages.
#include "renderlift/alrr/EdgeUpscaler.hpp"
#include "renderlift/alrr/SpatialUpscaler.hpp"

#include "rl_test.hpp"

#include <stdexcept>
#include <vector>

using rl::alrr::ConstImageView;
using rl::alrr::ImageView;
using rl::alrr::ReconstructParams;

namespace {

struct Image {
    std::uint32_t w, h;
    std::vector<std::uint8_t> px;
    explicit Image(std::uint32_t width, std::uint32_t height, std::uint8_t fill = 0)
        : w(width), h(height), px(std::size_t(width) * height * 4, fill) {}
    ImageView view() { return {px.data(), w, h, 0}; }
    ConstImageView view() const { return {px.data(), w, h, 0}; }
    std::uint8_t at(std::uint32_t x, std::uint32_t y, std::uint32_t c) const {
        return px[(std::size_t(y) * w + x) * 4 + c];
    }
    void set(std::uint32_t x, std::uint32_t y, std::uint8_t r, std::uint8_t g, std::uint8_t b) {
        px[(std::size_t(y) * w + x) * 4 + 0] = r;
        px[(std::size_t(y) * w + x) * 4 + 1] = g;
        px[(std::size_t(y) * w + x) * 4 + 2] = b;
        px[(std::size_t(y) * w + x) * 4 + 3] = 255;
    }
};

}  // namespace

RL_TEST(reconstruction_factory_support_matrix) {
    RL_CHECK(rl::alrr::createReconstructionStage(rl::ReconstructionMode::Spatial) != nullptr);
    RL_CHECK(rl::alrr::createReconstructionStage(rl::ReconstructionMode::Edge) != nullptr);
    RL_CHECK_THROWS(rl::alrr::createReconstructionStage(rl::ReconstructionMode::NisFallback),
                    std::runtime_error);
    RL_CHECK_THROWS(rl::alrr::createReconstructionStage(rl::ReconstructionMode::Temporal),
                    std::runtime_error);
}

RL_TEST(spatial_corner_exactness_2x) {
    Image src(2, 2);
    src.set(0, 0, 255, 0, 0);
    src.set(1, 0, 0, 255, 0);
    src.set(0, 1, 0, 0, 255);
    src.set(1, 1, 255, 255, 255);

    Image dst(4, 4);
    rl::alrr::SpatialUpscaler up;
    up.apply(src.view(), dst.view(), ReconstructParams{0.0f});

    // Corners of the destination sample exactly the source corners.
    RL_CHECK(dst.at(0, 0, 0) == 255 && dst.at(0, 0, 2) == 0);
    RL_CHECK(dst.at(3, 0, 1) == 255);
    RL_CHECK(dst.at(0, 3, 2) == 255);
    RL_CHECK(dst.at(3, 3, 0) == 255 && dst.at(3, 3, 1) == 255 && dst.at(3, 3, 2) == 255);
}

RL_TEST(spatial_flat_stays_flat_with_sharpen) {
    Image src(8, 8, 128);
    Image dst(32, 32);
    rl::alrr::SpatialUpscaler up;
    up.apply(src.view(), dst.view(), ReconstructParams{1.0f});
    for (std::uint32_t i = 0; i < 3; ++i) {
        RL_CHECK(dst.at(0, 0, i) == 128);
        RL_CHECK(dst.at(16, 16, i) == 128);
        RL_CHECK(dst.at(31, 31, i) == 128);
    }
}

RL_TEST(spatial_arbitrary_scale_and_stride) {
    Image src(213, 120, 64);
    Image dst(640, 360);
    rl::alrr::SpatialUpscaler up;
    up.apply(src.view(), dst.view(), ReconstructParams{0.35f});
    RL_CHECK(dst.at(639, 359, 0) == 64);
    RL_CHECK(dst.at(0, 0, 0) == 64);
}

RL_TEST(spatial_rejects_bad_views) {
    rl::alrr::SpatialUpscaler up;
    Image src(4, 4);
    Image dst(8, 8);
    RL_CHECK_THROWS(up.apply(ConstImageView{}, dst.view(), {}), std::invalid_argument);
    RL_CHECK_THROWS(up.apply(src.view(), ImageView{}, {}), std::invalid_argument);
}

RL_TEST(edge_flat_area_unsharpened) {
    // A flat gradient-free source must come out unsharpened even at sharpen 1.
    Image src(16, 16, 90);
    Image dst(64, 64);
    rl::alrr::EdgeUpscaler up;
    up.apply(src.view(), dst.view(), ReconstructParams{1.0f});
    RL_CHECK(dst.at(32, 32, 0) == 90);
    RL_CHECK(dst.at(0, 0, 0) == 90);
}

RL_TEST(edge_edge_area_gets_sharpened) {
    // Hard vertical step: dark left half, bright right half.
    Image src(16, 16, 0);
    for (std::uint32_t y = 0; y < 16; ++y)
        for (std::uint32_t x = 8; x < 16; ++x) src.set(x, y, 220, 220, 220);

    Image dst(64, 64);
    rl::alrr::EdgeUpscaler up;
    up.apply(src.view(), dst.view(), ReconstructParams{1.0f});

    // Near the step, sharpening must produce values beyond the pure bilinear range.
    bool foundBelow = false;
    bool foundAbove = false;
    for (std::uint32_t x = 24; x < 40; ++x) {
        const std::uint8_t v = dst.at(x, 32, 0);
        if (v < 3) foundBelow = true;      // overshoot below dark side
        if (v > 252) foundAbove = true;    // overshoot above bright side
    }
    RL_CHECK(foundBelow || foundAbove);

    // Interior of the flat right half stays flat.
    RL_CHECK(dst.at(60, 8, 0) == 220);
}

RL_TEST(edge_tier_versions) {
    rl::alrr::SpatialUpscaler spatial;
    rl::alrr::EdgeUpscaler edge;
    RL_CHECK(spatial.tierVersion().toString() == "0.1.0");
    RL_CHECK(edge.tierVersion().toString() == "0.5.0");
    RL_CHECK(spatial.mode() == rl::ReconstructionMode::Spatial);
    RL_CHECK(edge.mode() == rl::ReconstructionMode::Edge);
}
