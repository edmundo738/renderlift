// RenderLift — backend: vtable access utilities.
//
// COM interfaces (D3D/DXGI/Vulkan-loader trampolines) are vtable-based on
// every supported compiler, which is exactly why graphics hooking works at
// all: given an object, its vtable slots hold the process-wide addresses of
// interface methods, and those addresses are stable hook anchors per
// interface index. Slot layouts per interface are documented in docs/apis/.
#pragma once

#include <cstddef>

namespace rl::backend {

class VTable {
public:
    // Reads the object's vptr: the first word of a polymorphic object holds
    // the address of its vtable (an array of function pointers).
    explicit VTable(const void* object)
        : slots_(*reinterpret_cast<void* const* const*>(object)) {}

    // Address of the function registered at `index` (e.g. the process-wide
    // Present address read from any IDXGISwapChain instance — the MinHook
    // anchor).
    [[nodiscard]] void* functionAt(std::size_t index) const { return slots_[index]; }

    // NOTE: deliberately read-only. Patching a slot writes into a vtable that
    // lives in a read-only section (.rdata/.rodata) for every compiler we
    // target — doing it without VirtualProtect/mprotect crashes the process,
    // and doing it at all races with other readers. MinHook detours on the
    // function body are RenderLift's interception strategy.
private:
    void* const* slots_;  // vtable base, treated as const (read-only by design)
};

}  // namespace rl::backend
