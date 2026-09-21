// RenderLift tests — frame pipeline contract and profiling primitives.
#include "renderlift/prof/FrameTimer.hpp"
#include "renderlift/render/FramePipeline.hpp"

#include "rl_test.hpp"

#include <stdexcept>

RL_TEST(render_path_load_ratio) {
    rl::render::RenderPath path;
    path.display = {1366, 768};
    path.internal = {640, 360};
    RL_CHECK_NEAR(path.renderLoadRatio(), (640.0 * 360.0) / (1366.0 * 768.0), 1e-9);
    RL_CHECK(path.renderLoadRatio() < 0.23);  // the "lift": ~78% of 3D load removed
}

RL_TEST(render_path_validation) {
    rl::render::RenderPath good;
    good.display = {1366, 768};
    good.internal = {426, 240};
    good.sharpening = 0.35f;
    try {
        rl::render::validate(good);
    } catch (...) {
        RL_CHECK(false);
    }

    rl::render::RenderPath missingDisplay = good;
    missingDisplay.display = {};
    RL_CHECK_THROWS(rl::render::validate(missingDisplay), std::invalid_argument);

    rl::render::RenderPath inverted = good;
    inverted.internal = {1920, 1080};
    RL_CHECK_THROWS(rl::render::validate(inverted), std::invalid_argument);

    rl::render::RenderPath tooSmall = good;
    tooSmall.internal = {64, 64};
    RL_CHECK_THROWS(rl::render::validate(tooSmall), std::invalid_argument);

    rl::render::RenderPath badSharpen = good;
    badSharpen.sharpening = 1.2f;
    RL_CHECK_THROWS(rl::render::validate(badSharpen), std::invalid_argument);
}

RL_TEST(profiling_rolling_average) {
    rl::prof::RollingAverage avg(4);
    RL_CHECK(avg.count() == 0);
    RL_CHECK_NEAR(avg.mean(), 0.0, 1e-12);
    avg.push(1.0);
    avg.push(3.0);
    RL_CHECK_NEAR(avg.mean(), 2.0, 1e-12);
    for (int i = 0; i < 10; ++i) avg.push(10.0);  // overflow the window
    RL_CHECK(avg.count() == 4);
    RL_CHECK_NEAR(avg.mean(), 10.0, 1e-12);
}

RL_TEST(profiling_frame_timer) {
    rl::prof::FrameTimer timer(8);
    for (int i = 0; i < 4; ++i) timer.recordFrameMs(25.0);
    RL_CHECK_NEAR(timer.lastFrameMs(), 25.0, 1e-12);
    RL_CHECK_NEAR(timer.averageFrameMs(), 25.0, 1e-12);
    RL_CHECK_NEAR(timer.fps(), 40.0, 1e-9);
}
