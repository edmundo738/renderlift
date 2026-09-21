// RenderLift tests — render-target steering policy ("steer, don't resize").
#include "renderlift/backend/Steering.hpp"

#include "rl_test.hpp"

#include <stdexcept>
#include <vector>

using rl::Resolution;
using rl::backend::PassKind;
using rl::backend::RenderTargetRegistry;
using rl::backend::SteeringPolicy;
using rl::backend::TargetInfo;

namespace {

SteeringPolicy gtaLabPolicy(std::size_t startLevel = 2) {
    // The GTA V lab ladder (see profiles/games/grand-theft-auto-v.json).
    return SteeringPolicy({1366, 768},
                          {{426, 240}, {480, 270}, {640, 360}, {854, 480}, {1024, 576}, {1366, 768}},
                          startLevel);
}

}  // namespace

RL_TEST(steering_policy_constructs_and_validates) {
    RL_CHECK_THROWS(SteeringPolicy({0, 0}, {{640, 360}}, 0), std::invalid_argument);
    RL_CHECK_THROWS(SteeringPolicy({1366, 768}, {}, 0), std::invalid_argument);
    SteeringPolicy p = gtaLabPolicy(99);  // start level clamps to the top rung
    RL_CHECK(p.level() == 5);
    RL_CHECK(p.internalResolution() == (Resolution{1366, 768}));
}

RL_TEST(steering_display_sized_color_target_is_scene) {
    SteeringPolicy p = gtaLabPolicy();  // internal 640×360 at rung 2

    const TargetInfo scene{7, {1366, 768}, false, false};
    RL_CHECK(p.classify(scene) == PassKind::Scene);
    RL_CHECK(p.shouldSteer(scene));
    RL_CHECK(p.internalResolution() == (Resolution{640, 360}));
    // The lift: only ~22% of native pixels for the 3D scene.
    RL_CHECK_NEAR(640.0 * 360.0 / (1366.0 * 768.0), 0.22, 0.01);
}

RL_TEST(steering_never_touches_auxiliary_surfaces) {
    SteeringPolicy p = gtaLabPolicy();

    const TargetInfo depth{1, {1366, 768}, /*hasDepth=*/true, false};
    const TargetInfo shadowMap{2, {2048, 2048}, false, false};
    const TargetInfo smallPost{3, {640, 360}, false, false};   // already internal-sized
    RL_CHECK(p.classify(depth) == PassKind::Aux);
    RL_CHECK(p.classify(shadowMap) == PassKind::Aux);
    RL_CHECK(p.classify(smallPost) == PassKind::Aux);
    RL_CHECK(!p.shouldSteer(depth));
    RL_CHECK(!p.shouldSteer(shadowMap));
    RL_CHECK(!p.shouldSteer(smallPost));
}

RL_TEST(steering_on_native_rung_is_identity_and_skipped) {
    SteeringPolicy p = gtaLabPolicy(5);  // native rung
    const TargetInfo scene{7, {1366, 768}, false, false};
    RL_CHECK(p.classify(scene) == PassKind::Scene);
    RL_CHECK(!p.shouldSteer(scene));  // internal == display: no-op, don't bother
}

RL_TEST(steering_dynamic_level_tracks_controller) {
    SteeringPolicy p = gtaLabPolicy(3);
    const TargetInfo scene{7, {1366, 768}, false, false};
    RL_CHECK(p.internalResolution() == (Resolution{854, 480}));
    p.setLevel(0);
    RL_CHECK(p.internalResolution() == (Resolution{426, 240}));
    RL_CHECK(p.shouldSteer(scene));
    p.setLevel(5);
    RL_CHECK(!p.shouldSteer(scene));
}

RL_TEST(registry_tracks_binds_per_frame) {
    RenderTargetRegistry registry;
    registry.track({11, {1366, 768}, false, false});
    registry.track({12, {2048, 2048}, false, false});

    registry.noteBind(11);
    registry.noteBind(11);
    registry.noteBind(12);
    RL_CHECK(registry.find(11)->bindsThisFrame == 2);
    RL_CHECK(registry.find(12)->bindsThisFrame == 1);
    RL_CHECK(registry.find(13) == nullptr);
    RL_CHECK(registry.size() == 2);

    registry.beginFrame();
    RL_CHECK(registry.find(11)->bindsThisFrame == 0);
    RL_CHECK(registry.size() == 2);  // entries persist across frames
}

RL_TEST(registry_reregistration_refreshes_entry) {
    RenderTargetRegistry registry;
    registry.track({11, {1366, 768}, false, false});
    registry.noteBind(11);
    registry.find(11)->redirected = true;

    // Game resized and re-created the target with the same handle.
    registry.track({11, {1920, 1080}, false, false});
    const auto* entry = registry.find(11);
    RL_CHECK(entry->info.size == (Resolution{1920, 1080}));
    RL_CHECK(!entry->redirected);
    RL_CHECK(entry->bindsThisFrame == 0);
}

RL_TEST(registry_marking_redirected_surfaces) {
    RenderTargetRegistry registry;
    auto& entry = registry.track({11, {1366, 768}, false, false});
    entry.redirected = true;
    RL_CHECK(registry.find(11)->redirected);
}
