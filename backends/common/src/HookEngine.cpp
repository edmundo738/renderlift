#include "renderlift/backend/HookEngine.hpp"

#if defined(_WIN32)
#include <MinHook.h>
#endif

namespace rl::backend {
namespace {

#if defined(_WIN32)

class MinHookEngine final : public IHookEngine {
public:
    [[nodiscard]] const char* name() const override { return "MinHook 1.3.4"; }

    HookStatus initialize() override {
        if (ready_) return HookStatus::Ok;
        if (MH_Initialize() != MH_OK) return HookStatus::InitializationFailed;
        ready_ = true;
        return HookStatus::Ok;
    }

    HookStatus create(void* target, void* detour, void** originalOut) override {
        if (!ready_ || target == nullptr || detour == nullptr || originalOut == nullptr) {
            return HookStatus::CreateFailed;
        }
        return MH_CreateHook(target, detour, originalOut) == MH_OK ? HookStatus::Ok
                                                                   : HookStatus::CreateFailed;
    }

    HookStatus enableAll() override {
        if (!ready_ || MH_EnableHook(MH_ALL_HOOKS) != MH_OK) return HookStatus::EnableFailed;
        return HookStatus::Ok;
    }

    HookStatus disableAll() override {
        if (!ready_ || MH_DisableHook(MH_ALL_HOOKS) != MH_OK) return HookStatus::DisableFailed;
        return HookStatus::Ok;
    }

    void shutdown() override {
        if (!ready_) return;
        (void)MH_DisableHook(MH_ALL_HOOKS);
        (void)MH_Uninitialize();
        ready_ = false;
    }

private:
    bool ready_ = false;
};

#else

class StubHookEngine final : public IHookEngine {
public:
    [[nodiscard]] const char* name() const override { return "stub (non-Windows build)"; }
    HookStatus initialize() override { return HookStatus::Unsupported; }
    HookStatus create(void*, void*, void**) override { return HookStatus::Unsupported; }
    HookStatus enableAll() override { return HookStatus::Unsupported; }
    HookStatus disableAll() override { return HookStatus::Unsupported; }
    void shutdown() override {}
};

#endif

}  // namespace

std::unique_ptr<IHookEngine> createHookEngine() {
#if defined(_WIN32)
    return std::make_unique<MinHookEngine>();
#else
    return std::make_unique<StubHookEngine>();
#endif
}

}  // namespace rl::backend
