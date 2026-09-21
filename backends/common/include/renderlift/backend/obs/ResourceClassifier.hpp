// RenderLift — observation layer: resource classification.
//
// Given what FrameCapture observed, classify() answers: "what does this
// texture probably do in the frame?" — the first step toward choosing the
// steering set in integration mode "steer" (0.3). Rules are published
// heuristics from public D3D11 deferred-renderer analyses (GTA V's pipeline
// among them), each with an explicit confidence and a plain-English reason:
// the human researcher confirms or corrects the mapping in the inspector.
//
// D3D11_BIND_* bits used here: SRV 0x08 · RT 0x20 · DS 0x40 · UAV 0x80.
#pragma once

#include "renderlift/backend/obs/FrameCapture.hpp"

#include <cstdint>
#include <string>

namespace rl::backend::obs {

enum class ResourceClass : std::uint8_t {
    Unknown,
    DepthBuffer,         // primary scene depth (display-sized)
    ShadowMapCandidate,  // depth at smaller/square dims, DS|SRV
    HdrSceneCandidate,   // fp16/R11G11B10 display-sized RT|SRV — HDR scene chain
    LdrPostTarget,       // unorm display-sized RT|SRV (post-process chain)
    GBufferCandidate,    // display-sized unorm, bound in MRT sets (deferred)
    DownsamplePass,      // RT at a display-derived fraction (bloom/mips/SSAO)
    BackBufferLike,      // display-sized RT without SRV — swapchain-ish/UI host
    Texture,             // no render-target depth bits at all — regular texture
    Auxiliary,           // anything else
};

[[nodiscard]] std::string_view toString(ResourceClass cls) noexcept;

struct ClassHint {
    ResourceClass cls = ResourceClass::Unknown;
    float confidence = 0.0f;   // 0..1
    std::string reason;        // human-readable justification for the report
};

// DXGI_FORMAT numbers considered depth formats.
[[nodiscard]] bool isDepthFormat(std::uint32_t dxgiFormat) noexcept;

// DXGI_FORMAT numbers treated as HDR render-target formats.
[[nodiscard]] bool isHdrTargetFormat(std::uint32_t dxgiFormat) noexcept;

[[nodiscard]] ClassHint classifyResource(const ResourceUsage& usage,
                                         const CaptureContext& context);

}  // namespace rl::backend::obs
