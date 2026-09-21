// RenderLift tests — vtable utilities (ABI-agnostic, runs on every platform).
#include "renderlift/backend/HookEngine.hpp"
#include "renderlift/backend/VTable.hpp"

#include "rl_test.hpp"

#include <cstring>
#include <string>

namespace {

// void* ↔ function-pointer without the pedantic direct-cast warnings
// (bit-identical on every supported ABI).
template <typename Fn>
Fn asFunction(const void* address) {
    Fn fn;
    std::memcpy(&fn, &address, sizeof fn);
    return fn;
}

template <typename Fn>
void* asAddress(const Fn& fn) {
    void* address;
    std::memcpy(&address, &fn, sizeof address);
    return address;
}

}  // namespace

RL_TEST(vtable_reads_distinct_slots) {
    struct Probe {
        virtual int a() { return 7; }
        virtual int b() { return 9; }
        virtual ~Probe() = default;
    } object;

    const rl::backend::VTable vt(&object);
    void* slot0 = vt.functionAt(0);
    void* slot1 = vt.functionAt(1);

    RL_CHECK(slot0 != nullptr);
    RL_CHECK(slot1 != nullptr);
    RL_CHECK(slot0 != slot1);

    using Method = int (*)(Probe*);
    const Method callA = asFunction<Method>(slot0);
    const Method callB = asFunction<Method>(slot1);
    RL_CHECK(callA(&object) == 7);
    RL_CHECK(callB(&object) == 9);
}

RL_TEST(vtable_slots_are_stable_across_instances) {
    struct Swappable {
        virtual int value() { return 1; }
        virtual ~Swappable() = default;
    };
    using Method = int (*)(Swappable*);

    Swappable one;
    Swappable two;
    const rl::backend::VTable vt1(&one);
    const rl::backend::VTable vt2(&two);

    // Same class ⇒ same vtable address and same slots (that's why a slot read
    // from a throwaway probe object anchors a process-wide hook).
    RL_CHECK(vt1.functionAt(0) == vt2.functionAt(0));
    RL_CHECK(vt1.functionAt(0) != nullptr);

    const Method viaSlot = asFunction<Method>(vt1.functionAt(0));
    RL_CHECK(viaSlot(&one) == 1);
    RL_CHECK(viaSlot(&two) == 1);
}

RL_TEST(hook_engine_reports_honest_status) {
    const auto engine = rl::backend::createHookEngine();
#if defined(_WIN32)
    RL_CHECK(std::string(engine->name()).find("MinHook") != std::string::npos);
    RL_CHECK(rl::backend::succeeded(engine->initialize()));
    engine->shutdown();
#else
    RL_CHECK(std::string(engine->name()).find("stub") != std::string::npos);
    RL_CHECK(!rl::backend::succeeded(engine->initialize()));
    void* original = nullptr;
    RL_CHECK(engine->create(nullptr, nullptr, &original) == rl::backend::HookStatus::Unsupported);
    RL_CHECK(original == nullptr);
    engine->shutdown();  // must tolerate shutdown-before-init
#endif
}
