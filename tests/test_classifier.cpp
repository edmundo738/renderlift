// RenderLift tests — research-layer resource classifier: every heuristic
// exposes a confidence AND a plain reason, and the map drives the 0.3
// steering set. The golden-rule check below guarantees the backbuffer
// (home of the native-res UI) is never confused with a steering candidate.
#include "renderlift/backend/obs/ResourceClassifier.hpp"

#include "rl_test.hpp"

namespace obs = rl::backend::obs;

namespace {

using RC = obs::ResourceClass;

constexpr std::uint32_t Srv = 0x08;
constexpr std::uint32_t Rt = 0x20;
constexpr std::uint32_t Ds = 0x40;

obs::ResourceUsage makeUsage(std::uint32_t w, std::uint32_t h, std::uint32_t format,
                             std::uint32_t bind, std::uint32_t samples = 1) {
    obs::ResourceUsage u{};
    u.created = true;
    u.info = obs::TextureCreatedEvent{.device = 0x10,
                                      .id = 0xAA00,
                                      .width = w,
                                      .height = h,
                                      .dxgiFormat = format,
                                      .bindFlags = bind,
                                      .mipLevels = 1,
                                      .arraySize = 1,
                                      .samples = samples};
    return u;
}

obs::CaptureContext displayCtx(std::uint32_t w, std::uint32_t h) {
    obs::CaptureContext c{};
    c.displayEstimate = rl::Resolution{w, h};
    return c;
}

RL_TEST(classifier_depth_forms) {
    const auto ctx = displayCtx(1366, 768);

    const auto depth = obs::classifyResource(makeUsage(1366, 768, 40, Ds), ctx);
    RL_CHECK(depth.cls == RC::DepthBuffer);
    RL_CHECK(depth.confidence >= 0.9f);

    const auto shadow = obs::classifyResource(makeUsage(512, 512, 45, Ds | Srv), ctx);
    RL_CHECK(shadow.cls == RC::ShadowMapCandidate);
    RL_CHECK(shadow.confidence > 0.5f);
}

RL_TEST(classifier_hdr_scene_chain) {
    const auto ctx = displayCtx(1366, 768);
    const auto hint = obs::classifyResource(
        makeUsage(1366, 768, /*R16G16B16A16_FLOAT*/ 10, Rt | Srv), ctx);
    RL_CHECK(hint.cls == RC::HdrSceneCandidate);
    RL_CHECK(hint.confidence >= 0.85f);
    RL_CHECK(!hint.reason.empty());
}

RL_TEST(classifier_gbuffer_mrt) {
    const auto ctx = displayCtx(1366, 768);
    auto usage = makeUsage(1366, 768, /*R8G8B8A8_UNORM*/ 28, Rt | Srv);
    usage.boundInMrt = true;
    usage.bindsAsTarget = 40;
    const auto hint = obs::classifyResource(usage, ctx);
    RL_CHECK(hint.cls == RC::GBufferCandidate);
}

RL_TEST(classifier_backbuffer_like_is_never_steered) {
    const auto ctx = displayCtx(1366, 768);
    auto usage = makeUsage(1366, 768, 28, Rt);  // RT only, no SRV — swapchain-ish
    usage.bindsAsTarget = 600;
    const auto hint = obs::classifyResource(usage, ctx);
    RL_CHECK(hint.cls == RC::BackBufferLike);
    // Golden-rule guard: the native-res UI host must remain a distinct class
    // that steering code treats as untouchable.
    RL_CHECK(obs::toString(RC::BackBufferLike) == "backbuffer-like?");
}

RL_TEST(classifier_downsample_and_plain_textures) {
    const auto ctx = displayCtx(1366, 768);
    auto hint = obs::classifyResource(makeUsage(683, 384, 28, Rt | Srv), ctx);
    RL_CHECK(hint.cls == RC::DownsamplePass);

    hint = obs::classifyResource(makeUsage(64, 64, 28, Srv), ctx);
    RL_CHECK(hint.cls == RC::Texture);

    hint = obs::classifyResource(makeUsage(100, 100, 28, Rt), ctx);
    RL_CHECK(hint.cls == RC::Auxiliary);
}

RL_TEST(classifier_uav_only_is_not_hdr_scene) {
    // A UAV-only compute texture lives outside the RT chain we steer.
    const auto ctx = displayCtx(1366, 768);
    const auto hint = obs::classifyResource(makeUsage(1366, 768, 10, /*UAV*/ 0x80 | Srv), ctx);
    RL_CHECK(hint.cls != RC::HdrSceneCandidate);
}

}  // namespace
