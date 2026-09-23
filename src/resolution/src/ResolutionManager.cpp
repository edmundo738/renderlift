#include "renderlift/resolution/ResolutionManager.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace rl::resolution {

std::uint32_t ResolutionManager::roundUpToEven(double value) noexcept {
    if (value <= 0.0) return 0;
    auto rounded = static_cast<std::uint32_t>(std::lround(value));
    if (rounded & 1u) ++rounded;
    return rounded;
}

Resolution ResolutionManager::scale(Resolution display, double factor) noexcept {
    if (!display.valid() || factor <= 0.0) return {};

    const auto clampDim = [](double dim, double f, std::uint32_t minimum, std::uint32_t maximum) {
        std::uint32_t v = roundUpToEven(dim * f);
        v = std::max(v, minimum);
        v = std::min(v, maximum);
        return v;
    };

    return Resolution{clampDim(display.width, factor, kMinInternalWidth, display.width),
                      clampDim(display.height, factor, kMinInternalHeight, display.height)};
}

std::vector<Resolution> ResolutionManager::ladderForDisplay(Resolution display) {
    if (!display.valid()) {
        throw std::invalid_argument("ResolutionManager: display resolution must be non-zero");
    }

    std::vector<Resolution> ladder;
    ladder.reserve(kDefaultLadderFactors.size() + 1);
    for (const double factor : kDefaultLadderFactors) {
        ladder.push_back(scale(display, factor));
    }
    // Guarantee native is the final rung (scale() may clamp it otherwise).
    ladder.back() = display;

    return normalizeLadder(std::move(ladder), display);
}

std::vector<Resolution> ResolutionManager::normalizeLadder(std::vector<Resolution> ladder,
                                                           Resolution display) {
    if (!display.valid()) {
        throw std::invalid_argument("ResolutionManager: display resolution must be non-zero");
    }

    std::vector<Resolution> out;
    out.reserve(ladder.size() + 1);
    for (const Resolution r : ladder) {
        if (!r.valid() || r.pixelCount() > display.pixelCount()) continue;
        out.push_back(Resolution{std::clamp(r.width, kMinInternalWidth, display.width),
                                 std::clamp(r.height, kMinInternalHeight, display.height)});
    }
    // Native must always be reachable.
    out.push_back(display);

    std::sort(out.begin(), out.end(),
              [](const Resolution a, const Resolution b) { return a.pixelCount() < b.pixelCount(); });
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

}  // namespace rl::resolution
