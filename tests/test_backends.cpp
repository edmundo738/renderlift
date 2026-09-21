// RenderLift tests — backend registry & module contract.
#include "renderlift/backend/Registry.hpp"

#include "rl_test.hpp"

RL_TEST(backends_registry_covers_all_five_apis) {
    auto all = rl::backend::createAllBackends();
    RL_CHECK(all.size() == 5);

    bool haveD3D9 = false, haveD3D10 = false, haveD3D11 = false, haveD3D12 = false,
         haveVulkan = false;
    for (const auto& backend : all) {
        switch (backend->info().api) {
            case rl::GraphicsApi::D3D9:   haveD3D9 = true; break;
            case rl::GraphicsApi::D3D10:  haveD3D10 = true; break;
            case rl::GraphicsApi::D3D11:  haveD3D11 = true; break;
            case rl::GraphicsApi::D3D12:  haveD3D12 = true; break;
            case rl::GraphicsApi::Vulkan: haveVulkan = true; break;
            default: RL_CHECK(false);
        }
    }
    RL_CHECK(haveD3D9 && haveD3D10 && haveD3D11 && haveD3D12 && haveVulkan);
}

RL_TEST(backends_module_contract) {
    auto all = rl::backend::createAllBackends();
    for (const auto& backend : all) {
        // Module naming: RenderLift.<Api>
        RL_CHECK(backend->info().moduleName.find("RenderLift.") == 0);
        // Stubs must be honest about not having hooks yet.
        RL_CHECK(!backend->info().hooksImplemented);
        // Every backend declares what it plans to intercept.
        RL_CHECK(!backend->plannedHookTargets().empty());
        // Lifecycle.
        RL_CHECK(backend->state() == rl::backend::BackendState::Uninitialized);
        RL_CHECK(backend->initialize());
        RL_CHECK(backend->state() == rl::backend::BackendState::Ready);
        backend->shutdown();
        RL_CHECK(backend->state() == rl::backend::BackendState::Uninitialized);
    }
}

RL_TEST(backends_hook_targets_match_api_philosophy) {
    auto d3d11 = rl::backend::createD3D11Backend();
    const auto targets = d3d11->plannedHookTargets();

    auto has = [&](const char* needle) {
        for (const auto t : targets)
            if (t.find(needle) != std::string_view::npos) return true;
        return false;
    };

    // Presentation…
    RL_CHECK(has("Present"));
    // …and, critically, internal-resolution steering — not just a resize.
    RL_CHECK(has("CreateTexture2D") || has("CreateRenderTargetView"));
    RL_CHECK(has("RSSetViewports"));
}

RL_TEST(backends_d3d12_tracks_explicit_states) {
    auto d3d12 = rl::backend::createD3D12Backend();
    bool tracksBarriers = false;
    for (const auto t : d3d12->plannedHookTargets())
        if (t.find("ResourceBarrier") != std::string_view::npos) tracksBarriers = true;
    RL_CHECK(tracksBarriers);  // D3D12 is not "D3D11 with different names"
}
