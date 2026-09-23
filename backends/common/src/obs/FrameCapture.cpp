#include "renderlift/backend/obs/FrameCapture.hpp"

#include <algorithm>
#include <cmath>
#include <type_traits>

namespace rl::backend::obs {

void FrameCapture::ingest(const Event& event) {
    std::visit(
        [this](const auto& e) {
            using E = std::decay_t<decltype(e)>;
            if constexpr (std::is_same_v<E, TextureCreatedEvent>) {
                ResourceUsage& u = resources_[e.id];
                u.info = e;
                u.created = true;
            } else if constexpr (std::is_same_v<E, ViewCreatedEvent>) {
                viewToResource_[e.viewId] = e.resourceId;
            } else if constexpr (std::is_same_v<E, RenderTargetsEvent>) {
                onRenderTargets(e);
            } else if constexpr (std::is_same_v<E, ViewportEvent>) {
                const std::uint32_t w = static_cast<std::uint32_t>(std::lround(e.w));
                const std::uint32_t h = static_cast<std::uint32_t>(std::lround(e.h));
                if (static_cast<std::uint64_t>(w) * h > context_.displayEstimate.pixelCount()) {
                    context_.displayEstimate = Resolution{w, h};
                }
            } else if constexpr (std::is_same_v<E, DrawEvent>) {
                ++context_.drawCallsThisFrame;
                context_.maxSingleDrawCount = std::max(context_.maxSingleDrawCount, e.count);
            } else if constexpr (std::is_same_v<E, DrawStatEvent>) {
                context_.drawCallsThisFrame += e.calls;
                context_.maxSingleDrawCount = std::max(
                    context_.maxSingleDrawCount, std::max(e.maxIndices, e.maxVertices));
            } else if constexpr (std::is_same_v<E, PresentEvent>) {
                currentFrame_ = e.frame;
                context_.frames = std::max(context_.frames, e.frame);
                context_.maxDrawCallsInFrame =
                    std::max(context_.maxDrawCallsInFrame, context_.drawCallsThisFrame);
                context_.drawCallsThisFrame = 0;
                for (auto& [id, u] : resources_) u.boundThisFrame = false;
            } else if constexpr (std::is_same_v<E, FrameMarkerEvent>) {
                if (e.frame > 0) currentFrame_ = e.frame;
            }
        },
        event);
}

void FrameCapture::onRenderTargets(const RenderTargetsEvent& e) {
    std::uint32_t liveViews = 0;
    std::uint64_t boundResource[kMaxRtvs]{};

    // First pass: count live views and resolve rtv → texture.
    for (std::uint32_t i = 0; i < e.count && i < kMaxRtvs; ++i) {
        if (e.rtvs[i] == 0) continue;
        ++liveViews;
        const auto vt = viewToResource_.find(e.rtvs[i]);
        if (vt != viewToResource_.end()) boundResource[i] = vt->second;
    }

    // Second pass: bind statistics per texture.
    for (std::uint32_t i = 0; i < e.count && i < kMaxRtvs; ++i) {
        if (boundResource[i] == 0) continue;
        ResourceUsage& u = resources_[boundResource[i]];
        ++u.bindsAsTarget;
        if (!u.boundThisFrame) {
            u.boundThisFrame = true;
            ++u.framesBound;
        }
        u.maxViewsInBind = std::max(u.maxViewsInBind, liveViews);
        if (liveViews > 1) u.boundInMrt = true;
    }

    (void)e.dsv;  // depth binds handled via viewToResource_ when needed (0.3)
}

}  // namespace rl::backend::obs
