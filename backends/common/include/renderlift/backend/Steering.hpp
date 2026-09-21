// RenderLift — backend: render-target steering (the "steer, don't resize" core).
//
// Platform-neutral decision engine shared by every backend. During a frame a
// backend observes the game's render-target creations and bindings; this
// module answers two questions:
//
//   shouldSteer(target)  — must this target render at the internal
//                          resolution? (display-sized color surfaces: yes.
//                          depth/stencil, shadow maps, non-display sizes: no.)
//   internalResolution() — the current ladder rung the game should render at.
//
// Backends implement the "how": scaling a CreateTexture2D desc, clamping a
// viewport, wrapping an RTV. This module owns the "whether" and "how small",
// and stays free of graphics-API types so it compiles and tests everywhere.
#pragma once

#include "renderlift/core/Types.hpp"

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace rl::backend {

// What a backend knows about one render target the game created.
struct TargetInfo {
    std::uint64_t id = 0;      // opaque handle (resource pointer value is fine)
    Resolution size{};
    bool hasDepth = false;     // depth/stencil surface
    bool fromSwapChain = false;  // backbuffer obtained from the game's swap chain
};

// Book of every render target observed in the current process, with
// per-frame binding counters (game-pass classification input).
class RenderTargetRegistry {
public:
    struct Entry {
        TargetInfo info;
        std::uint32_t bindsThisFrame = 0;
        bool redirected = false;  // backend already swapped this one for internal-res
    };

    // Registers the target (idempotent by id — re-registration refreshes
    // the stored info and binds) and returns the live entry.
    Entry& track(const TargetInfo& info);

    [[nodiscard]] Entry* find(std::uint64_t id) noexcept;
    [[nodiscard]] const Entry* find(std::uint64_t id) const noexcept;

    // Records one additional bind of `id` within the current frame.
    void noteBind(std::uint64_t id);

    // Frame boundary: clears per-frame counters (entries persist).
    void beginFrame() noexcept;

    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }

private:
    std::unordered_map<std::uint64_t, Entry> entries_;
};

enum class PassKind : std::uint8_t {
    Aux,    // depth, shadow maps, G-buffer tiles, anything not display-sized
    Scene,  // display-sized color surface — the 3D scene: steer it
    Ui,     // display-sized color surface owning the UI pass — native res
};

class SteeringPolicy {
public:
    // `ladder` must be ascending (rungs 0..N-1, N-1 == native). It is the
    // resolved profile ladder — see rl::resolution::resolveLadder.
    SteeringPolicy(Resolution display, std::vector<Resolution> ladder, std::size_t startLevel);

    // Redirect this target to internal resolution?
    // False for anything that is not a display-sized color surface, and false
    // while the ladder sits on the native rung (steering would be identity).
    [[nodiscard]] bool shouldSteer(const TargetInfo& target) const noexcept;

    // Scene = display-sized color. Aux = everything else.
    // (Ui is returned only by per-profile pass-split hints — 0.3 scope; for
    // now display-sized color is always Scene.)
    [[nodiscard]] PassKind classify(const TargetInfo& target) const noexcept;

    [[nodiscard]] Resolution internalResolution() const { return ladder_[level_]; }
    [[nodiscard]] const std::vector<Resolution>& ladder() const noexcept { return ladder_; }
    [[nodiscard]] std::size_t level() const noexcept { return level_; }

    // Dynamic-resolution hook: the ALRR controller moves the rung.
    void setLevel(std::size_t level) noexcept;

private:
    [[nodiscard]] bool isDisplaySized(Resolution size) const noexcept {
        return size == display_;
    }

    Resolution display_{};
    std::vector<Resolution> ladder_;
    std::size_t level_ = 0;
};

}  // namespace rl::backend
