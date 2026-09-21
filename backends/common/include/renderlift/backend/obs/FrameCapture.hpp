// RenderLift — observation layer: frame capture aggregation.
//
// FrameCapture ingests a stream of obs::Event (parsed capture lines, or live
// events inside the backend) and builds the frame model the inspector and
// the classifier work on:
//
//   - every texture created (dims/format/bind flags),
//   - which RTV/DSV views point at which textures,
//   - per-resource render-target binding statistics (how often, MRT or solo),
//   - an estimate of the display resolution (largest viewport),
//   - draw-call counts per frame.
//
// Pure data — no graphics API types — so it compiles and tests everywhere.
#pragma once

#include "renderlift/backend/obs/CaptureFormat.hpp"
#include "renderlift/core/Types.hpp"

#include <cstdint>
#include <unordered_map>

namespace rl::backend::obs {

struct ResourceUsage {
    TextureCreatedEvent info{};
    bool created = false;

    // View bindings
    std::uint64_t bindsAsTarget = 0;      // times one of its views was bound
    std::uint64_t framesBound = 0;        // distinct frames it appeared in
    bool boundThisFrame = false;
    bool boundInMrt = false;              // ever bound with other targets
    std::uint32_t maxViewsInBind = 0;     // largest simultaneous RTV set seen
};

struct CaptureContext {
    Resolution displayEstimate{};   // from the largest viewport observed
    std::uint64_t frames = 0;       // frames completed (last present index)
    std::uint64_t drawCallsThisFrame = 0;
    std::uint64_t maxDrawCallsInFrame = 0;
    std::uint32_t maxSingleDrawCount = 0;  // largest one-call index/vertex count
};

class FrameCapture {
public:
    // Ingest one parsed event. Cheap and allocation-free after warmup.
    void ingest(const Event& event);

    [[nodiscard]] const std::unordered_map<std::uint64_t, ResourceUsage>& resources() const {
        return resources_;
    }
    [[nodiscard]] const CaptureContext& context() const { return context_; }
    [[nodiscard]] std::uint64_t currentFrame() const { return currentFrame_; }

    [[nodiscard]] const ResourceUsage* find(std::uint64_t resourceId) const {
        const auto it = resources_.find(resourceId);
        return it == resources_.end() ? nullptr : &it->second;
    }

private:
    void onRenderTargets(const RenderTargetsEvent& e);

    std::unordered_map<std::uint64_t, ResourceUsage> resources_;
    std::unordered_map<std::uint64_t, std::uint64_t> viewToResource_;
    CaptureContext context_;
    std::uint64_t currentFrame_ = 0;
};

}  // namespace rl::backend::obs
