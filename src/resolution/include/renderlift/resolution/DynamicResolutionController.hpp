// RenderLift — ALRR Core: dynamic internal-resolution controller.
//
// The controller walks the resolution ladder up and down based on per-frame
// telemetry. Two design rules are the project's identity:
//
//   1. Resolution only helps when the GPU is the bottleneck. When the CPU is
//      saturated the controller HOLDS — a smaller render target would only
//      throw away quality without buying frames.
//   2. Hysteresis everywhere: a warmup period at startup, a cooldown between
//      changes, and frame-budget margins, so the picture doesn't pump.
//
// The controller is pure logic: it receives FrameSample structs and returns
// decisions. It never touches clocks, drivers or graphics APIs — backends and
// overlays feed it, tests drive it directly.
#pragma once

#include "renderlift/core/Types.hpp"

#include <cstddef>
#include <cstdint>

namespace rl::resolution {

struct ControllerConfig {
    double targetFrameMs = 33.3;      // frame budget (1000 / targetFps)
    double gpuHighWatermark = 0.97;   // gpu busy ≥ → GPU saturated
    double gpuLowWatermark = 0.75;    // gpu busy ≤ → headroom below
    double cpuHighWatermark = 0.95;   // cpu busy ≥ → CPU-bound: never downscale
    double slowFrameRatio = 1.15;     // frame > target × ratio → "slow"
    double fastFrameRatio = 0.85;     // frame < target × ratio → "fast"
    std::uint32_t warmupFrames = 20;  // frames ignored at startup
    std::uint32_t cooldownFrames = 45;  // min frames between ladder changes
};

struct FrameSample {
    double frameMs = 0.0;
    double gpuBusy = 0.0;  // 0..1
    double cpuBusy = 0.0;  // 0..1 (busiest logical core)
};

enum class Decision : std::uint8_t { Hold, StepDown, StepUp };

struct DecisionInfo {
    Decision decision = Decision::Hold;
    std::size_t level = 0;             // ladder rung after the decision
    Bottleneck bottleneck = Bottleneck::Unknown;
    const char* reason = "";           // static string, for HUD/logs
};

class DynamicResolutionController {
public:
    // ladderSize must be ≥ 1; startLevel is clamped into range.
    DynamicResolutionController(std::size_t ladderSize, std::size_t startLevel,
                                ControllerConfig config = {});

    // Feed one frame of telemetry; get back the decision for that frame.
    [[nodiscard]] DecisionInfo update(const FrameSample& sample);

    [[nodiscard]] std::size_t level() const noexcept { return level_; }
    [[nodiscard]] std::size_t ladderSize() const noexcept { return ladderSize_; }

    // External override (user picked a fixed internal resolution). Resets the
    // cooldown so the controller doesn't immediately fight the user.
    void setLevel(std::size_t level) noexcept;

private:
    [[nodiscard]] Bottleneck classify(const FrameSample& sample) const noexcept;
    [[nodiscard]] bool changeAllowed() const noexcept;

    ControllerConfig config_;
    std::size_t ladderSize_;
    std::size_t level_;
    std::uint64_t frame_ = 0;
    std::uint64_t lastChangeFrame_ = 0;   // 0 == never changed (first change allowed after warmup)
};

}  // namespace rl::resolution
