// RenderLift tests — game-profile loading (runs against the real profiles/).
#include "renderlift/core/GameProfile.hpp"
#include "renderlift/resolution/ProfilePlan.hpp"

#include "rl_test.hpp"

#ifndef RENDERLIFT_PROFILES_DIR
#define RENDERLIFT_PROFILES_DIR "profiles"
#endif

RL_TEST(profiles_load_gta5) {
    namespace rlres = rl::resolution;
    const rl::core::GameProfile p =
        rl::core::GameProfile::loadFile(std::string(RENDERLIFT_PROFILES_DIR) +
                                        "/games/grand-theft-auto-v.json");

    RL_CHECK(p.id == "gta5");
    RL_CHECK(p.title.find("Theft") != std::string::npos);
    RL_CHECK(p.executable == "GTA5.exe");
    RL_CHECK(p.api == rl::GraphicsApi::D3D11);
    RL_CHECK(p.display == (rl::Resolution{1366, 768}));
    RL_CHECK(p.internalLadder.size() == 6);
    RL_CHECK(p.internalLadder.front() == (rl::Resolution{426, 240}));
    RL_CHECK(p.uiNative);
    RL_CHECK(p.reconstruction.mode == rl::ReconstructionMode::Edge);
    RL_CHECK_NEAR(p.reconstruction.sharpening, 0.35, 1e-6);
    RL_CHECK(p.dynamicResolution.enabled);
    RL_CHECK_NEAR(p.dynamicResolution.targetFps, 30.0, 1e-9);

    const std::vector<rl::Resolution> ladder = rlres::resolveLadder(p);
    RL_CHECK(ladder.size() == 6);
    RL_CHECK(ladder.back() == p.display);
    RL_CHECK(rlres::resolveStartLevel(p, ladder.size()) == 2);
    RL_CHECK(ladder[2] == (rl::Resolution{640, 360}));

    const rlres::ControllerConfig cfg = rlres::controllerConfigFrom(p);
    RL_CHECK_NEAR(cfg.targetFrameMs, 1000.0 / 30.0, 1e-6);
    RL_CHECK(cfg.cooldownFrames == 45);
}

RL_TEST(profiles_load_generic_default) {
    const rl::core::GameProfile p =
        rl::core::GameProfile::loadFile(std::string(RENDERLIFT_PROFILES_DIR) +
                                        "/generic/default.json");
    RL_CHECK(p.id == "generic.default");
    RL_CHECK(p.api == rl::GraphicsApi::Unknown);  // "auto"
    RL_CHECK(p.internalLadder.empty());           // derived at runtime
    RL_CHECK(p.uiNative);
}

RL_TEST(profiles_reject_bad_documents) {
    RL_CHECK_THROWS(rl::core::GameProfile::fromJson(rl::json::parse("{}")), std::runtime_error);
    RL_CHECK_THROWS(rl::core::GameProfile::fromJson(rl::json::parse(R"({"id":"x"})")),
                    std::runtime_error);
    RL_CHECK_THROWS(
        rl::core::GameProfile::fromJson(
            rl::json::parse(R"({"id":"x","title":"X","api":"glide"})")),
        std::runtime_error);
    RL_CHECK_THROWS(
        rl::core::GameProfile::fromJson(rl::json::parse(
            R"({"id":"x","title":"X","reconstruction":{"mode":"edge","sharpening":1.5}})")),
        std::runtime_error);
    RL_CHECK_THROWS(rl::core::GameProfile::loadFile("does/not/exist.json"), std::runtime_error);
}
