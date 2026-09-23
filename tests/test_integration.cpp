// RenderLift tests — integration modes (ADR 0003). The mode ladder is the
// project's golden rule turned into a product feature: observe first, alter
// later — and dynamic resolution must stay inert until mode "full".
#include "renderlift/core/GameProfile.hpp"
#include "renderlift/core/Types.hpp"

#include "rl_test.hpp"

#include <stdexcept>

#ifndef RENDERLIFT_PROFILES_DIR
#define RENDERLIFT_PROFILES_DIR "profiles"
#endif

namespace {

using rl::IntegrationMode;

RL_TEST(integration_mode_strings_roundtrip) {
    for (const char* name : {"observe", "steer", "reconstruct", "full"}) {
        const auto mode = rl::integrationModeFromString(name);
        RL_CHECK(mode.has_value());
        RL_CHECK(rl::toString(*mode) == name);
    }
    RL_CHECK(!rl::integrationModeFromString("magic").has_value());
}

RL_TEST(integration_defaults_to_observe) {
    const auto profile =
        rl::core::GameProfile::fromJson(rl::json::parse(R"({"id":"x","title":"X"})"));
    RL_CHECK(profile.integration.mode == IntegrationMode::Observe);
    RL_CHECK(profile.integration.observationFrames == 600);
}

RL_TEST(integration_profile_block_parsed) {
    const auto profile = rl::core::GameProfile::fromJson(rl::json::parse(R"({
        "id": "gtav-lab",
        "title": "Lab",
        "integration": { "mode": "observe", "observationFrames": 900 }
    })"));
    RL_CHECK(profile.integration.mode == IntegrationMode::Observe);
    RL_CHECK(profile.integration.observationFrames == 900);
}

RL_TEST(integration_unknown_mode_rejected) {
    RL_CHECK_THROWS(
        rl::core::GameProfile::fromJson(
            rl::json::parse(R"({"id":"x","title":"X","integration":{"mode":"magic"}})")),
        std::runtime_error);
}

RL_TEST(gta_v_profile_is_observe_mode) {
    const auto profile = rl::core::GameProfile::loadFile(
        std::string(RENDERLIFT_PROFILES_DIR) + "/games/grand-theft-auto-v.json");
    RL_CHECK(profile.integration.mode == IntegrationMode::Observe);
    // The first real lab watches 600 frames before touching anything.
    RL_CHECK(profile.integration.observationFrames == 600);
}

}  // namespace
