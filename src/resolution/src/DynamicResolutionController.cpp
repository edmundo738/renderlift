#include "renderlift/resolution/DynamicResolutionController.hpp"

#include <algorithm>
#include <stdexcept>

namespace rl::resolution {

DynamicResolutionController::DynamicResolutionController(std::size_t ladderSize,
                                                         std::size_t startLevel,
                                                         ControllerConfig config)
    : config_(config), ladderSize_(ladderSize), level_(0) {
    if (ladderSize == 0) {
        throw std::invalid_argument("DynamicResolutionController: ladder must have at least one rung");
    }
    level_ = std::min(startLevel, ladderSize_ - 1);
}

Bottleneck DynamicResolutionController::classify(const FrameSample& s) const noexcept {
    if (s.cpuBusy >= config_.cpuHighWatermark) return Bottleneck::Cpu;
    if (s.gpuBusy >= config_.gpuHighWatermark) return Bottleneck::Gpu;
    return Bottleneck::Balanced;
}

bool DynamicResolutionController::changeAllowed() const noexcept {
    if (frame_ <= config_.warmupFrames) return false;
    if (lastChangeFrame_ == 0) return true;  // first change after warmup
    return (frame_ - lastChangeFrame_) >= config_.cooldownFrames;
}

void DynamicResolutionController::setLevel(std::size_t level) noexcept {
    level_ = std::min(level, ladderSize_ - 1);
    lastChangeFrame_ = frame_;
}

DecisionInfo DynamicResolutionController::update(const FrameSample& s) {
    ++frame_;

    const Bottleneck bottleneck = classify(s);
    const bool slow = s.frameMs > config_.targetFrameMs * config_.slowFrameRatio;
    const bool fast = s.frameMs < config_.targetFrameMs * config_.fastFrameRatio;

    DecisionInfo info;
    info.level = level_;
    info.bottleneck = bottleneck;

    // Rule 1: CPU-bound — do not downscale. It would cost quality, not frames.
    if (bottleneck == Bottleneck::Cpu) {
        info.reason = "cpu-bound: holding resolution (downscaling does not help)";
        return info;
    }

    // Rule 2: GPU saturated and over budget — step down one rung.
    if (slow && bottleneck == Bottleneck::Gpu) {
        if (!changeAllowed()) {
            info.reason = "gpu saturated but change locked (warmup/cooldown)";
            return info;
        }
        if (level_ == 0) {
            info.reason = "gpu saturated but already at the lowest rung";
            return info;
        }
        --level_;
        lastChangeFrame_ = frame_;
        info.decision = Decision::StepDown;
        info.level = level_;
        info.reason = "gpu saturated & frame over budget: stepping down";
        return info;
    }

    // Rule 3: comfortable headroom — step up one rung.
    if (fast && s.gpuBusy <= config_.gpuLowWatermark) {
        if (!changeAllowed()) {
            info.reason = "headroom but change locked (warmup/cooldown)";
            return info;
        }
        if (level_ + 1 >= ladderSize_) {
            info.reason = "headroom but already at native resolution";
            return info;
        }
        ++level_;
        lastChangeFrame_ = frame_;
        info.decision = Decision::StepUp;
        info.level = level_;
        info.reason = "headroom detected: stepping up";
        return info;
    }

    if (slow) {
        info.reason = "frame over budget but gpu not saturated (pacing/vsync?)";
        return info;
    }

    info.reason = "within tolerance";
    return info;
}

}  // namespace rl::resolution
