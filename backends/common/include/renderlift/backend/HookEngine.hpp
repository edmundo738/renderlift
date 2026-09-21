// RenderLift — backend: hook engine interface.
//
// Thin RAII layer over the process-wide detour engine. On Windows this is
// the vendored MinHook (third_party/minhook, BSD-2); everywhere else an
// honest stub so engine-agnostic code keeps compiling and the behavior
// ("hooking unsupported here") stays testable.
//
//   auto engine = createHookEngine();
//   engine->initialize();
//   engine->create(targetAddress, &MyDetour, &trampolineOut);
//   engine->enableAll();      // process starts seeing detours
//   ...
//   engine->shutdown();       // disable + uninstall (module unload)
#pragma once

#include <memory>

namespace rl::backend {

enum class HookStatus : unsigned char {
    Ok,
    Unsupported,            // no engine on this platform (non-Windows stub)
    InitializationFailed,
    CreateFailed,
    EnableFailed,
    DisableFailed,
};

[[nodiscard]] inline bool succeeded(HookStatus status) noexcept {
    return status == HookStatus::Ok;
}

class IHookEngine {
public:
    virtual ~IHookEngine() = default;

    [[nodiscard]] virtual const char* name() const = 0;

    // Prepare the engine for this process. Idempotent.
    virtual HookStatus initialize() = 0;

    // Install a detour for one function address. `originalOut` receives the
    // trampoline that calls through to the real implementation.
    virtual HookStatus create(void* target, void* detour, void** originalOut) = 0;

    // Apply / revert every installed detour.
    virtual HookStatus enableAll() = 0;
    virtual HookStatus disableAll() = 0;

    // Disable everything and release the engine (module unload path).
    virtual void shutdown() = 0;
};

// MinHook engine on Windows; stub elsewhere.
[[nodiscard]] std::unique_ptr<IHookEngine> createHookEngine();

}  // namespace rl::backend
