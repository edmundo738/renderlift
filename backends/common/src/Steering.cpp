#include "renderlift/backend/Steering.hpp"

#include <algorithm>
#include <stdexcept>

namespace rl::backend {

// ── RenderTargetRegistry ────────────────────────────────────────────────────

RenderTargetRegistry::Entry& RenderTargetRegistry::track(const TargetInfo& info) {
    auto [it, inserted] = entries_.try_emplace(info.id, Entry{info, 0, false});
    if (!inserted) {
        it->second.info = info;  // re-created target (resize): refresh
        it->second.redirected = false;
        it->second.bindsThisFrame = 0;
    }
    return it->second;
}

RenderTargetRegistry::Entry* RenderTargetRegistry::find(std::uint64_t id) noexcept {
    const auto it = entries_.find(id);
    return it == entries_.end() ? nullptr : &it->second;
}

const RenderTargetRegistry::Entry* RenderTargetRegistry::find(std::uint64_t id) const noexcept {
    const auto it = entries_.find(id);
    return it == entries_.end() ? nullptr : &it->second;
}

void RenderTargetRegistry::noteBind(std::uint64_t id) {
    if (Entry* entry = find(id)) {
        ++entry->bindsThisFrame;
    }
}

void RenderTargetRegistry::beginFrame() noexcept {
    for (auto& [id, entry] : entries_) {
        entry.bindsThisFrame = 0;
    }
}

// ── SteeringPolicy ──────────────────────────────────────────────────────────

SteeringPolicy::SteeringPolicy(Resolution display, std::vector<Resolution> ladder,
                               std::size_t startLevel)
    : display_(display), ladder_(std::move(ladder)) {
    if (!display_.valid()) {
        throw std::invalid_argument("SteeringPolicy: display resolution must be non-zero");
    }
    if (ladder_.empty()) {
        throw std::invalid_argument("SteeringPolicy: ladder must have at least one rung");
    }
    level_ = std::min(startLevel, ladder_.size() - 1);
}

bool SteeringPolicy::shouldSteer(const TargetInfo& target) const noexcept {
    if (classify(target) != PassKind::Scene) return false;
    // On the native rung the internal resolution IS the display resolution;
    // steering would be an identity copy — skip the work.
    return internalResolution() != display_;
}

PassKind SteeringPolicy::classify(const TargetInfo& target) const noexcept {
    if (target.hasDepth) return PassKind::Aux;
    if (!isDisplaySized(target.size)) return PassKind::Aux;
    return PassKind::Scene;
}

void SteeringPolicy::setLevel(std::size_t level) noexcept {
    level_ = std::min(level, ladder_.size() - 1);
}

}  // namespace rl::backend
