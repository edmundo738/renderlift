// ─────────────────────────────────────────────────────────────────────────────
// RenderLift — D3D11 module (Windows-only translation unit).
//
// Compiled into RenderLift.D3D11.dll on Windows. Two halves:
//
//   1. Bootstrap: build a throwaway device + swap chain on a hidden window so
//      we can read the real, driver-filled vtables and learn the process-wide
//      addresses of Present/ResizeBuffers (classic, reliable bootstrap).
//   2. Interception: MinHook detours on those addresses. 0.2 installs
//      measured passthroughs (frame boundary + swap-chain-observed display
//      tracking feeding a SteeringPolicy); 0.3 wires CreateTexture2D /
//      RSSetViewports / OMSetRenderTargets so the 3D scene truly renders at
//      the internal resolution, with the ALRR compute pass before UI.
//
// Injection contract: the loader (RenderLift.exe) LoadLibrary()s this DLL and
// calls RenderLiftInstall once the game is running — never hook from DllMain
// (loader lock). RenderLiftUninstall is the safeunload path.
// ─────────────────────────────────────────────────────────────────────────────

#if !defined(_WIN32)
#error "D3D11Module.cpp is Windows-only; CMake guards this."
#endif

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <d3d11.h>
#include <dxgi.h>

#include "renderlift/backend/HookEngine.hpp"
#include "renderlift/backend/Steering.hpp"
#include "renderlift/backend/VTable.hpp"

#include <cstdint>
#include <memory>
#include <mutex>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

#if defined(renderlift_d3d11_EXPORTS) || defined(RENDERLIFT_D3D11_EXPORTS)
#define RENDERLIFT_D3D11_API __declspec(dllexport)
#else
#define RENDERLIFT_D3D11_API __declspec(dllimport)
#endif

namespace rl::backend {
namespace {

// IDXGISwapChain vtable slots — stable ABI across all D3D11-era drivers.
// (IUnknown 0-2 · IDXGIObject 3-6 · IDXGIDeviceSubObject::GetDevice 7 · then:)
constexpr std::size_t kSlotPresent = 8;
constexpr std::size_t kSlotResizeBuffers = 13;

using PresentFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
using ResizeBuffersFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT, UINT,
                                                    DXGI_FORMAT, UINT);

// ── Module state ────────────────────────────────────────────────────────────

struct ModuleState {
    std::mutex mutex;
    std::unique_ptr<IHookEngine> hooks;

    PresentFn originalPresent = nullptr;
    ResizeBuffersFn originalResizeBuffers = nullptr;

    bool installed = false;
    std::uint64_t presentCount = 0;          // frame counter (per-frame boundary)
    Resolution observedDisplay{};            // from swap-chain desc / ResizeBuffers

    // Steering defaults until profile plumbing arrives (0.3): generic ladder
    // at 1366×768 with the controller free to move the rung.
    std::unique_ptr<SteeringPolicy> steering;
};

ModuleState& state() {
    static ModuleState s;
    return s;
}

// ── Vtable bootstrap ────────────────────────────────────────────────────────

struct SwapChainVtable {
    void* present = nullptr;
    void* resizeBuffers = nullptr;
};

bool tryCreateProbe(D3D_DRIVER_TYPE driverType, HWND hwnd, IDXGISwapChain** swapchainOut,
                    ID3D11Device** deviceOut, ID3D11DeviceContext** contextOut) {
    DXGI_SWAP_CHAIN_DESC desc{};
    desc.BufferCount = 1;
    desc.BufferDesc.Width = 8;
    desc.BufferDesc.Height = 8;
    desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.OutputWindow = hwnd;
    desc.SampleDesc.Count = 1;
    desc.Windowed = TRUE;
    desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    return SUCCEEDED(D3D11CreateDeviceAndSwapChain(nullptr, driverType, nullptr, 0, nullptr, 0,
                                                   D3D11_SDK_VERSION, &desc, swapchainOut,
                                                   deviceOut, nullptr, contextOut));
}

// Creates a throwaway device + swap chain on a hidden window and reads the
// interface vtable. Tries hardware first (real driver layout), falls back to
// WARP (headless machines / CI).
bool resolveSwapChainVtable(SwapChainVtable& out) {
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = DefWindowProcW;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.lpszClassName = L"RenderLiftD3D11Probe";
    (void)RegisterClassExW(&windowClass);  // fine if already registered

    HWND hwnd = CreateWindowExW(0, windowClass.lpszClassName, L"RenderLift",
                                WS_DISABLED | WS_POPUP, 0, 0, 8, 8, nullptr, nullptr,
                                windowClass.hInstance, nullptr);
    if (hwnd == nullptr) return false;

    IDXGISwapChain* swapchain = nullptr;
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    bool ok = tryCreateProbe(D3D_DRIVER_TYPE_HARDWARE, hwnd, &swapchain, &device, &context) ||
              tryCreateProbe(D3D_DRIVER_TYPE_WARP, hwnd, &swapchain, &device, &context);

    if (ok) {
        const VTable vt(swapchain);
        out.present = vt.functionAt(kSlotPresent);
        out.resizeBuffers = vt.functionAt(kSlotResizeBuffers);
        ok = out.present != nullptr && out.resizeBuffers != nullptr;
    }

    if (context != nullptr) context->Release();
    if (device != nullptr) device->Release();
    if (swapchain != nullptr) swapchain->Release();
    DestroyWindow(hwnd);
    UnregisterClassW(windowClass.lpszClassName, windowClass.hInstance);
    return ok;
}

// ── Detours (0.2: measured passthrough + display tracking) ──────────────────

HRESULT STDMETHODCALLTYPE Present_Hook(IDXGISwapChain* swapchain, UINT syncInterval, UINT flags) {
    ModuleState& s = state();
    {
        std::lock_guard<std::mutex> lock(s.mutex);
        ++s.presentCount;

        // Learn the real display resolution from the swap chain itself.
        DXGI_SWAP_CHAIN_DESC desc{};
        if (SUCCEEDED(swapchain->GetDesc(&desc))) {
            s.observedDisplay = Resolution{desc.BufferDesc.Width, desc.BufferDesc.Height};
        }
        // 0.3: reconstruction dispatch happens HERE, between the game's last
        // scene draw and Present:
        //   steering→internalResolution() chooses the current ladder rung;
        //   ALRR compute: internal target → swap-chain backbuffer;
        //   UI pass stays native; frame timing feeds the dynamic controller.
    }
    return s.originalPresent(swapchain, syncInterval, flags);
}

HRESULT STDMETHODCALLTYPE ResizeBuffers_Hook(IDXGISwapChain* swapchain, UINT bufferCount,
                                             UINT width, UINT height, DXGI_FORMAT format,
                                             UINT flags) {
    ModuleState& s = state();
    {
        std::lock_guard<std::mutex> lock(s.mutex);
        s.observedDisplay = Resolution{width, height};
        // 0.3: re-derive the ladder for the new display resolution and reset
        // the steering policy before the chain reallocates.
    }
    return s.originalResizeBuffers(swapchain, bufferCount, width, height, format, flags);
}

HRESULT installLocked(ModuleState& s) {
    SwapChainVtable vt;
    if (!resolveSwapChainVtable(vt)) return E_FAIL;

    if (!succeeded(s.hooks->create(vt.present, reinterpret_cast<void*>(&Present_Hook),
                                   reinterpret_cast<void**>(&s.originalPresent)))) {
        return E_FAIL;
    }
    if (!succeeded(s.hooks->create(vt.resizeBuffers, reinterpret_cast<void*>(&ResizeBuffers_Hook),
                                   reinterpret_cast<void**>(&s.originalResizeBuffers)))) {
        return E_FAIL;
    }
    if (!succeeded(s.hooks->enableAll())) return E_FAIL;

    s.installed = true;
    return S_OK;
}

HRESULT uninstallLocked(ModuleState& s) {
    if (s.hooks) {
        s.hooks->shutdown();
        s.hooks.reset();
    }
    s.originalPresent = nullptr;
    s.originalResizeBuffers = nullptr;
    s.installed = false;
    return S_OK;
}

}  // namespace
}  // namespace rl::backend

// ── Injection contract ──────────────────────────────────────────────────────

extern "C" {

// Called by the loader (RenderLift.exe) once the game process is up.
RENDERLIFT_D3D11_API HRESULT RenderLiftInstall(void) {
    rl::backend::ModuleState& s = rl::backend::state();
    std::lock_guard<std::mutex> lock(s.mutex);
    if (s.installed) return S_OK;

    s.hooks = rl::backend::createHookEngine();
    if (s.hooks == nullptr || !rl::backend::succeeded(s.hooks->initialize())) {
        s.hooks.reset();
        return E_FAIL;
    }
    return rl::backend::installLocked(s);
}

RENDERLIFT_D3D11_API HRESULT RenderLiftUninstall(void) {
    rl::backend::ModuleState& s = rl::backend::state();
    std::lock_guard<std::mutex> lock(s.mutex);
    return rl::backend::uninstallLocked(s);
}

// Diagnostic for the loader/overlay: how many presents flowed through.
RENDERLIFT_D3D11_API std::uint64_t RenderLiftFrameCount(void) {
    rl::backend::ModuleState& s = rl::backend::state();
    std::lock_guard<std::mutex> lock(s.mutex);
    return s.presentCount;
}

}  // extern "C"

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        // Deliberately nothing else: loader lock. RenderLiftInstall does the work.
    } else if (reason == DLL_PROCESS_DETACH) {
        // If the loader didn't uninstall (crash path), drop hooks so the
        // process never calls into unloaded code.
        rl::backend::ModuleState& s = rl::backend::state();
        if (s.installed) (void)rl::backend::uninstallLocked(s);
    }
    return TRUE;
}
