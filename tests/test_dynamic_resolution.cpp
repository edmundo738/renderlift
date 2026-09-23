// RenderLift tests — the dynamic resolution controller's behavior contract.
#include "renderlift/resolution/DynamicResolutionController.hpp"

#include "rl_test.hpp"

#include <stdexcept>
#include <string>
#include <vector>

using rl::Bottleneck;
using rl::resolution::ControllerConfig;
using rl::resolution::Decision;
using rl::resolution::DecisionInfo;
using rl::resolution::DynamicResolutionController;
using rl::resolution::FrameSample;

namespace {

ControllerConfig testConfig() {
    ControllerConfig c;
    c.targetFrameMs = 33.3;  // 30 fps target
    c.warmupFrames = 20;
    c.cooldownFrames = 45;
    return c;
}

// Run `frames` identical samples; return every non-Hold decision with its frame index.
std::vector<DecisionInfo> collectDecisions(DynamicResolutionController& c, const FrameSample& s,
                                           int frames) {
    std::vector<DecisionInfo> out;
    for (int i = 0; i < frames; ++i) {
        DecisionInfo d = c.update(s);
        if (d.decision != Decision::Hold) out.push_back(d);
    }
    return out;
}

}  // namespace

RL_TEST(controller_constructs_and_clamps) {
    RL_CHECK_THROWS(DynamicResolutionController(0, 0), std::invalid_argument);
    DynamicResolutionController c(6, 99);  // start level beyond ladder → clamped
    RL_CHECK(c.level() == 5);
}

RL_TEST(controller_steps_down_when_gpu_bound) {
    DynamicResolutionController c(6, 3, testConfig());
    const FrameSample heavy{40.0, 0.99, 0.30};  // 40ms over budget, GPU saturated

    const auto decisions = collectDecisions(c, heavy, 260);

    RL_CHECK(decisions.size() == 3);                       // 3 → 2 → 1 → 0
    RL_CHECK(decisions[0].decision == Decision::StepDown);
    RL_CHECK(decisions[0].level == 2);
    RL_CHECK(decisions[0].bottleneck == Bottleneck::Gpu);
    RL_CHECK(c.level() == 0);                              // bottomed out
    const DecisionInfo settled = c.update(heavy);
    RL_CHECK(settled.decision == Decision::Hold);          // can't go lower
}

RL_TEST(controller_respects_warmup_and_cooldown) {
    DynamicResolutionController c(6, 3, testConfig());
    const FrameSample heavy{40.0, 0.99, 0.30};

    for (int i = 0; i < 20; ++i) {
        RL_CHECK(c.update(heavy).decision == Decision::Hold);  // warmup: no changes
    }
    RL_CHECK(c.update(heavy).decision == Decision::StepDown);  // frame 21: first change
    for (int i = 0; i < 44; ++i) {
        RL_CHECK(c.update(heavy).decision == Decision::Hold);  // cooldown: no changes
    }
    RL_CHECK(c.update(heavy).decision == Decision::StepDown);  // cooldown elapsed
}

RL_TEST(controller_never_downscales_when_cpu_bound) {
    DynamicResolutionController c(6, 4, testConfig());
    const FrameSample cpuJam{40.0, 0.55, 0.99};  // slow frames, CPU saturated

    const auto decisions = collectDecisions(c, cpuJam, 300);
    RL_CHECK(decisions.empty());  // quality preserved: downscaling wouldn't help
    RL_CHECK(c.level() == 4);

    const DecisionInfo d = c.update(cpuJam);
    RL_CHECK(d.bottleneck == Bottleneck::Cpu);
    RL_CHECK(std::string(d.reason).find("cpu") != std::string::npos);
}

RL_TEST(controller_steps_up_with_headroom) {
    DynamicResolutionController c(6, 1, testConfig());
    const FrameSample easy{18.0, 0.50, 0.40};  // fast frames, GPU half idle

    const auto decisions = collectDecisions(c, easy, 400);
    RL_CHECK(decisions.size() == 4);                       // 1 → 2 → 3 → 4 → 5 (top)
    for (const auto& d : decisions) {
        RL_CHECK(d.decision == Decision::StepUp);
    }
    RL_CHECK(c.level() == 5);

    const DecisionInfo settled = c.update(easy);
    RL_CHECK(settled.decision == Decision::Hold);          // already native
}

RL_TEST(controller_holds_when_slow_but_not_gpu_saturated) {
    DynamicResolutionController c(6, 3, testConfig());
    const FrameSample weird{40.0, 0.80, 0.60};  // slow frame, neither saturated (vsync?)

    const auto decisions = collectDecisions(c, weird, 200);
    RL_CHECK(decisions.empty());
    const DecisionInfo d = c.update(weird);
    RL_CHECK(std::string(d.reason).find("pacing") != std::string::npos);
}

RL_TEST(controller_manual_override_respects_cooldown_afterwards) {
    DynamicResolutionController c(6, 3, testConfig());
    const FrameSample heavy{40.0, 0.99, 0.30};

    for (int i = 0; i < 25; ++i) (void)c.update(heavy);    // past warmup
    c.setLevel(5);                                          // user forces native
    RL_CHECK(c.level() == 5);
    for (int i = 0; i < 10; ++i) {
        // Controller must not immediately undo the user's choice.
        RL_CHECK(c.update(heavy).decision == Decision::Hold);
    }
}
