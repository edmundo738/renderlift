// RenderLift tests — resolution ladder math.
#include "renderlift/resolution/ResolutionManager.hpp"

#include "rl_test.hpp"

#include <stdexcept>

using rl::Resolution;
using rl::resolution::ResolutionManager;

RL_TEST(resolution_round_up_to_even) {
    RL_CHECK(ResolutionManager::roundUpToEven(683.0) == 684);   // odd → next even
    RL_CHECK(ResolutionManager::roundUpToEven(341.5) == 342);   // .5 → nearest even already
    RL_CHECK(ResolutionManager::roundUpToEven(640.0) == 640);
    RL_CHECK(ResolutionManager::roundUpToEven(0.0) == 0);
    RL_CHECK(ResolutionManager::roundUpToEven(-5.0) == 0);
}

RL_TEST(resolution_scale_1366x768) {
    const Resolution display{1366, 768};
    RL_CHECK(ResolutionManager::scale(display, 0.5) == (Resolution{684, 384}));
    RL_CHECK(ResolutionManager::scale(display, 0.625) == (Resolution{854, 480}));
    RL_CHECK(ResolutionManager::scale(display, 1.0) == display);
    // Factor too small → clamped to the engine minimum, never zero.
    const Resolution tiny = ResolutionManager::scale(display, 0.01);
    RL_CHECK(tiny.width >= ResolutionManager::kMinInternalWidth);
    RL_CHECK(tiny.height >= ResolutionManager::kMinInternalHeight);
    // Factor above 1 → never exceeds display.
    RL_CHECK(ResolutionManager::scale(display, 1.5) == display);
}

RL_TEST(resolution_generic_ladder) {
    const Resolution display{1366, 768};
    const std::vector<Resolution> ladder = ResolutionManager::ladderForDisplay(display);

    RL_CHECK(ladder.size() == 7);
    RL_CHECK(ladder.back() == display);
    for (std::size_t i = 1; i < ladder.size(); ++i) {
        RL_CHECK(ladder[i].pixelCount() > ladder[i - 1].pixelCount());  // strictly ascending
        RL_CHECK(ladder[i].width % 2 == 0 && ladder[i].height % 2 == 0);
    }
    RL_CHECK(ladder[0] == (Resolution{342, 192}));   // 0.25 factor
    RL_CHECK(ladder[3] == (Resolution{684, 384}));   // 0.5 factor
}

RL_TEST(resolution_normalize_explicit_ladder) {
    const Resolution display{1366, 768};

    // Unsorted, with dupes and out-of-range junk.
    std::vector<Resolution> messy = {
        {640, 360}, {426, 240}, {640, 360}, {0, 0}, {1920, 1080}, {854, 480}};

    const std::vector<Resolution> clean =
        ResolutionManager::normalizeLadder(std::move(messy), display);

    RL_CHECK(clean.size() == 4);  // 426×240, 640×360, 854×480, +native
    RL_CHECK(clean[0] == (Resolution{426, 240}));
    RL_CHECK(clean.back() == display);
    for (std::size_t i = 1; i < clean.size(); ++i) {
        RL_CHECK(clean[i].pixelCount() > clean[i - 1].pixelCount());
    }
}

RL_TEST(resolution_invalid_display_rejected) {
    RL_CHECK_THROWS(ResolutionManager::ladderForDisplay({0, 0}), std::invalid_argument);
    RL_CHECK_THROWS(ResolutionManager::normalizeLadder({}, {0, 0}), std::invalid_argument);
}
