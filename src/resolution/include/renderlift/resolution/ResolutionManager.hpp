// RenderLift — ALRR Core: internal-resolution ladder management.
//
// The resolution manager owns the math behind "how small can the 3D render
// go". A profile either pins an explicit ladder (e.g. GTA V: 426×240 →
// 1366×768) or lets the manager derive one from scale factors of the display
// resolution. Ladders are always ascending: rung 0 is the lowest internal
// resolution, the last rung is native.
#pragma once

#include "renderlift/core/Types.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace rl::resolution {

class ResolutionManager {
public:
    // Fractions of the display resolution used to derive a generic ladder.
    // For a 1366×768 display these produce (after even rounding):
    //   342×192 · 428×240 · 512×288 · 684×384 · 854×480 · 1026×576 · 1366×768
    static constexpr std::array<double, 7> kDefaultLadderFactors{
        0.25, 0.3125, 0.375, 0.5, 0.625, 0.75, 1.0};

    // Never go below this, no matter the factor — below it reconstruction is
    // pointless and some games misbehave creating tiny render targets.
    static constexpr std::uint32_t kMinInternalWidth = 160;
    static constexpr std::uint32_t kMinInternalHeight = 90;

    // Rounds to the nearest integer and then up to an even dimension.
    // Even dimensions keep subsampled chroma formats and 8×8 compute groups
    // happy, and avoid 1-pixel aspect drift. (683 → 684, 341.5 → 342.)
    [[nodiscard]] static std::uint32_t roundUpToEven(double value) noexcept;

    // Scales a display resolution by `factor`, clamps to the internal minimum
    // and never exceeds the display resolution itself.
    [[nodiscard]] static Resolution scale(Resolution display, double factor) noexcept;

    // Builds the generic ladder for a display resolution from
    // kDefaultLadderFactors: ascending, deduplicated, last rung == display.
    [[nodiscard]] static std::vector<Resolution> ladderForDisplay(Resolution display);

    // Normalizes an explicit (profile-provided) ladder: drops invalid
    // entries, clamps to the display resolution, sorts ascending by pixel
    // count, deduplicates, and guarantees the native display resolution is
    // present as the last rung.
    [[nodiscard]] static std::vector<Resolution> normalizeLadder(
        std::vector<Resolution> ladder, Resolution display);
};

}  // namespace rl::resolution
