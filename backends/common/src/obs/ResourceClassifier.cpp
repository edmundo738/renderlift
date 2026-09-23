#include "renderlift/backend/obs/ResourceClassifier.hpp"

#include <algorithm>
#include <cmath>

namespace rl::backend::obs {

namespace bind {
constexpr std::uint32_t Srv = 0x08;
constexpr std::uint32_t Rt = 0x20;
constexpr std::uint32_t Ds = 0x40;
}  // namespace bind

namespace fmt {
constexpr std::uint32_t R32G32B32A32_Float = 2;
constexpr std::uint32_t R16G16B16A16_Float = 10;
constexpr std::uint32_t R11G11B10_Float = 87;
constexpr std::uint32_t D32_Float = 40;
constexpr std::uint32_t D32_Float_S8X24 = 20;
constexpr std::uint32_t D24_Unorm_S8 = 45;
constexpr std::uint32_t R16_Float = 54;
constexpr std::uint32_t R16_Unorm = 56;
constexpr std::uint32_t R8G8B8A8_Unorm = 28;
constexpr std::uint32_t R8G8B8A8_UnormSrgb = 29;
}  // namespace fmt

std::string_view toString(ResourceClass cls) noexcept {
    switch (cls) {
        case ResourceClass::DepthBuffer:        return "depth";
        case ResourceClass::ShadowMapCandidate: return "shadow-map?";
        case ResourceClass::HdrSceneCandidate:  return "hdr-scene?";
        case ResourceClass::LdrPostTarget:      return "ldr-post?";
        case ResourceClass::GBufferCandidate:   return "g-buffer?";
        case ResourceClass::DownsamplePass:     return "downsample?";
        case ResourceClass::BackBufferLike:     return "backbuffer-like?";
        case ResourceClass::Texture:            return "texture";
        case ResourceClass::Auxiliary:          return "aux";
        default:                                return "unknown";
    }
}

bool isDepthFormat(std::uint32_t f) noexcept {
    return f == fmt::D32_Float || f == fmt::D32_Float_S8X24 || f == fmt::D24_Unorm_S8;
}

bool isHdrTargetFormat(std::uint32_t f) noexcept {
    return f == fmt::R16G16B16A16_Float || f == fmt::R11G11B10_Float ||
           f == fmt::R32G32B32A32_Float;
}

namespace {

[[nodiscard]] bool sameAspect(Resolution a, Resolution b) noexcept {
    if (!a.valid() || !b.valid()) return false;
    return std::fabs(a.aspectRatio() - b.aspectRatio()) < 0.05;
}

[[nodiscard]] bool isUnormTarget(std::uint32_t f) noexcept {
    return f == fmt::R8G8B8A8_Unorm || f == fmt::R8G8B8A8_UnormSrgb ||
           f == fmt::R16_Unorm || f == 24 /*R10G10B10A2_UNORM*/;
}

[[nodiscard]] bool isDownsampledFrom(Resolution size, Resolution display) noexcept {
    if (!size.valid() || !display.valid()) return false;
    if (size.pixelCount() >= display.pixelCount()) return false;
    return sameAspect(size, display);
}

}  // namespace

ClassHint classifyResource(const ResourceUsage& u, const CaptureContext& ctx) {
    const TextureCreatedEvent& t = u.info;
    if (!u.created) return {ResourceClass::Unknown, 0.0f, "never observed as created"};

    const Resolution size{t.width, t.height};
    const Resolution display = ctx.displayEstimate;
    const bool displaySized = display.valid() && size == display;
    const auto has = [&](std::uint32_t bit) { return (t.bindFlags & bit) != 0; };

    // 1) Depth first — formats are unambiguous. Shadow textures are typically
    //    square (atlas cascades) and one dimension can exceed the display's,
    //    so bound the total area, not each dimension.
    if (isDepthFormat(t.dxgiFormat)) {
        if (displaySized) {
            return {ResourceClass::DepthBuffer, 0.95,
                    "depth format at display size — primary scene depth"};
        }
        if (!display.valid() || size.pixelCount() <= display.pixelCount()) {
            return {ResourceClass::ShadowMapCandidate, 0.85,
                    "depth format smaller than display (square/atlas ok) — shadow candidate"};
        }
        return {ResourceClass::Auxiliary, 0.4f, "depth format at unusual size"};
    }

    // 2) No render-target/depth usage at all → regular texture.
    if (!has(bind::Rt) && !has(bind::Ds)) {
        return {ResourceClass::Texture, 0.9f, "never bound as a render target"};
    }

    // 3) MSAA-heavy high-sample targets are usually scene/gbuffer intermediates.
    if (t.samples > 1 && displaySized && has(bind::Rt)) {
        return {ResourceClass::GBufferCandidate, 0.7f,
                "display-sized MSAA render target (samples=" + std::to_string(t.samples) + ")"};
    }

    // 4) Display-sized HDR chains.
    if (displaySized && has(bind::Rt) && has(bind::Srv) && isHdrTargetFormat(t.dxgiFormat)) {
        return {ResourceClass::HdrSceneCandidate, 0.9f,
                dxgiFormatName(t.dxgiFormat) +
                    " at display size, RT|SRV — HDR scene/lighting chain"};
    }

    // 5) Deferred G-buffer: display-sized unorm RTs bound together (MRT).
    if (displaySized && has(bind::Rt) && has(bind::Srv) && isUnormTarget(t.dxgiFormat) &&
        u.boundInMrt) {
        return {ResourceClass::GBufferCandidate, 0.8f,
                "display-sized unorm RT|SRV bound in MRT sets (deferred G-buffer)"};
    }

    // 6) Display-sized unorm, RT|SRV, solo binds → post-process chain.
    if (displaySized && has(bind::Rt) && has(bind::Srv) && isUnormTarget(t.dxgiFormat)) {
        return {ResourceClass::LdrPostTarget, 0.7f,
                "display-sized unorm RT|SRV, solo binds — LDR post chain"};
    }

    // 7) RT without SRV at display size → backbuffer-ish (often the UI host).
    if (displaySized && has(bind::Rt) && !has(bind::Srv)) {
        return {ResourceClass::BackBufferLike, 0.75f,
                "display-sized RT without SRV — backbuffer-like (likely UI host)"};
    }

    // 8) Display-derived fractions are downsample passes (bloom, SSAO, blur).
    if (has(bind::Rt) && isDownsampledFrom(size, display)) {
        return {ResourceClass::DownsamplePass, 0.75f,
                "render target at a display-derived fraction (downsample pass)"};
    }

    return {ResourceClass::Auxiliary, 0.3f, "no strong classification signals"};
}

}  // namespace rl::backend::obs
