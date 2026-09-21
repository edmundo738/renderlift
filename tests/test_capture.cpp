// RenderLift tests — the RLCAP1 capture wire format and the frame
// aggregation model (Research Layer 0.2; platform-neutral).
#include "renderlift/backend/obs/FrameCapture.hpp"

#include "rl_test.hpp"

#include <cmath>
#include <variant>

namespace obs = rl::backend::obs;

namespace {

RL_TEST(capture_format_textures_roundtrip) {
    const obs::TextureCreatedEvent tex{.device = 0x1000,
                                       .id = 0x2000,
                                       .width = 1366,
                                       .height = 768,
                                       .dxgiFormat = 28,
                                       .bindFlags = 0x28,
                                       .mipLevels = 1,
                                       .arraySize = 1,
                                       .samples = 1};
    const std::string line = obs::formatEvent(obs::Event{tex});
    RL_CHECK(line.rfind("RLCAP1 tex ", 0) == 0);

    const auto parsed = obs::parseEvent(line);
    RL_CHECK(parsed.has_value());
    const auto& out = std::get<obs::TextureCreatedEvent>(*parsed);
    RL_CHECK(out.device == 0x1000 && out.id == 0x2000);
    RL_CHECK(out.width == 1366 && out.height == 768);
    RL_CHECK(out.dxgiFormat == 28 && out.bindFlags == 0x28);
    RL_CHECK(out.samples == 1);
}

RL_TEST(capture_format_parses_all_event_kinds) {
    {
        const auto e = obs::parseEvent("RLCAP1 frame=42");
        RL_CHECK(e.has_value());
        RL_CHECK(std::get<obs::FrameMarkerEvent>(*e).frame == 42);
    }
    {
        const auto e = obs::parseEvent("RLCAP1 rt ctx=0x30 n=2 rtvs=0xAAA,0xBBB dsv=0xCCC");
        RL_CHECK(e.has_value());
        const auto& rt = std::get<obs::RenderTargetsEvent>(*e);
        RL_CHECK(rt.count == 2 && rt.rtvs[0] == 0xAAA && rt.rtvs[1] == 0xBBB);
        RL_CHECK(rt.dsv == 0xCCC);
    }
    {
        const auto e = obs::parseEvent("RLCAP1 vp ctx=0x30 n=1 x=0 y=0 w=1366 h=768");
        RL_CHECK(e.has_value());
        const auto& vp = std::get<obs::ViewportEvent>(*e);
        RL_CHECK(vp.count == 1 && std::fabs(vp.w - 1366.0f) < 0.1f);
    }
    {
        const auto e =
            obs::parseEvent("RLCAP1 drawstat frame=7 calls=1523 maxidx=8192 maxvtx=1024");
        RL_CHECK(e.has_value());
        const auto& ds = std::get<obs::DrawStatEvent>(*e);
        RL_CHECK(ds.frame == 7 && ds.calls == 1523 && ds.maxIndices == 8192);
    }
    {
        const auto e = obs::parseEvent("RLCAP1 present sc=0x9D40 frame=42");
        RL_CHECK(e.has_value());
        RL_CHECK(std::get<obs::PresentEvent>(*e).frame == 42);
    }
    {
        // rtv/dsv lines written by the backend via formatViewCreated round-trip.
        const auto rtv = obs::parseEvent("RLCAP1 rtv dev=0x10 id=0x77aa res=0x8a1f");
        RL_CHECK(rtv.has_value());
        const auto& v = std::get<obs::ViewCreatedEvent>(*rtv);
        RL_CHECK(v.viewId == 0x77AA && v.resourceId == 0x8A1F);

        const auto dsvLine =
            obs::formatDsvCreated(obs::ViewCreatedEvent{0x10, 0x77BB, 0x8A20});
        RL_CHECK(obs::parseEvent(dsvLine).has_value());
    }
}

RL_TEST(capture_format_rejects_garbage) {
    RL_CHECK(!obs::parseEvent("").has_value());
    RL_CHECK(!obs::parseEvent("hello world").has_value());
    RL_CHECK(!obs::parseEvent("RLCAP2 tex dev=0x1").has_value());
    RL_CHECK(!obs::parseEvent("RLCAP1 tex dev=").has_value());
    RL_CHECK(!obs::parseEvent("RLCAP1 unknown_tag x=1").has_value());
}

RL_TEST(frame_capture_aggregates_frame_state) {
    obs::FrameCapture cap;

    cap.ingest(obs::Event{obs::ViewportEvent{.context = 0x30, .count = 1,
                                             .x = 0, .y = 0, .w = 1366, .h = 768}});
    cap.ingest(obs::Event{obs::TextureCreatedEvent{.device = 0x10, .id = 0xA,
                                                   .width = 1366, .height = 768,
                                                   .dxgiFormat = 10, .bindFlags = 0x28,
                                                   .mipLevels = 1, .arraySize = 1,
                                                   .samples = 1}});
    cap.ingest(obs::Event{obs::ViewCreatedEvent{.device = 0x10, .viewId = 0x77AA,
                                                .resourceId = 0xA}});

    obs::RenderTargetsEvent rt{};
    rt.context = 0x30;
    rt.count = 1;
    rt.rtvs[0] = 0x77AA;
    cap.ingest(obs::Event{rt});

    RL_CHECK(cap.find(0xA) != nullptr);
    RL_CHECK(!cap.find(0xA)->boundInMrt);

    // Two live views bound at once, one unknown: binds count only the
    // resolved texture, but MRT detection must be honest about what the GPU
    // actually had bound (liveViews == 2 → MRT).
    rt.count = 2;
    rt.rtvs[1] = 0x77BB;  // unknown view — ignored for binds, counts as live
    cap.ingest(obs::Event{rt});

    cap.ingest(obs::Event{obs::DrawStatEvent{.frame = 1, .calls = 1523,
                                             .maxIndices = 8192, .maxVertices = 0}});
    cap.ingest(obs::Event{obs::PresentEvent{.swapchain = 0x9D40, .frame = 1}});

    RL_CHECK(cap.context().displayEstimate.width == 1366);
    RL_CHECK(cap.context().displayEstimate.height == 768);
    RL_CHECK(cap.context().frames == 1);
    RL_CHECK(cap.context().maxDrawCallsInFrame == 1523);
    RL_CHECK(cap.context().maxSingleDrawCount == 8192);

    const auto* usage = cap.find(0xA);
    RL_CHECK(usage != nullptr);
    RL_CHECK(usage->bindsAsTarget == 2);
    RL_CHECK(usage->framesBound == 1);
    RL_CHECK(usage->boundInMrt);
    RL_CHECK(usage->maxViewsInBind == 2);
}

RL_TEST(capture_display_helpers) {
    RL_CHECK(obs::dxgiFormatName(28) == "R8G8B8A8_UNORM");
    RL_CHECK(obs::dxgiFormatName(10) == "R16G16B16A16_FLOAT");
    RL_CHECK(obs::dxgiFormatName(999) == "fmt=999");
    RL_CHECK(obs::bindFlagsName(0x28) == "SRV|RT");
    RL_CHECK(obs::bindFlagsName(0) == "-");
}

}  // namespace
