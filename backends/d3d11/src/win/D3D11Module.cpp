// ─────────────────────────────────────────────────────────────────────────────
// RenderLift — D3D11 module (Windows-only translation unit). RESEARCH LAYER.
//
// Current integration mode: OBSERVE (ADR 0003 — never alter rendering before
// being able to observe it). Every detour below is a measured passthrough:
// it records what the game does into an RLCAP1 capture log and returns
// control to the original function untouched.
//
// Observed surface:
//   ID3D11Device:        CreateTexture2D(5) · CreateRenderTargetView(9)
//                        CreateDepthStencilView(10)
//   ID3D11DeviceContext: DrawIndexed(12) · Draw(13) · OMSetRenderTargets(33)
//                        RSSetViewports(44)        (aggregated per frame)
//   IDXGISwapChain:      Present(8) · ResizeBuffers(13)
//
// Output: "RenderLift.D3D11.log" (RLCAP1 lines) — inspect offline with:
//   RenderLift.CLI inspect RenderLift.D3D11.log
//
// Env overrides: RENDERLIFT_LOG (path) · RENDERLIFT_OBSERVE_FRAMES (cap).
// Injection contract: loader LoadLibrary()s the DLL, calls RenderLiftInstall
// once the game is up — never hook from DllMain (loader lock).
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
#include "renderlift/backend/VTable.hpp"
#include "renderlift/backend/obs/CaptureFormat.hpp"

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

#if defined(renderlift_d3d11_EXPORTS) || defined(RENDERLIFT_D3D11_EXPORTS)
#define RENDERLIFT_D3D11_API __declspec(dllexport)
#else
#define RENDERLIFT_D3D11_API __declspec(dllimport)
#endif

namespace rl::backend {
namespace {

namespace obs = rl::backend::obs;

// ── Vtable slots (stable COM ABI; see docs/apis/d3d11.md) ───────────────────
namespace slot {
// IDXGISwapChain
constexpr std::size_t Present = 8;
constexpr std::size_t ResizeBuffers = 13;
// ID3D11Device (IUnknown 0-2 · CreateBuffer 3 · CreateTexture1D 4)
constexpr std::size_t CreateTexture2D = 5;
constexpr std::size_t CreateRenderTargetView = 9;
constexpr std::size_t CreateDepthStencilView = 10;
// ID3D11DeviceContext (IUnknown 0-2 · ID3D11DeviceChild 3-6 · VSSetConstantBuffers 7)
constexpr std::size_t DrawIndexed = 12;
constexpr std::size_t Draw = 13;
constexpr std::size_t OMSetRenderTargets = 33;
constexpr std::size_t RSSetViewports = 44;
}  // namespace slot

using PresentFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
using ResizeBuffersFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT, UINT,
                                                    DXGI_FORMAT, UINT);
using CreateTexture2DFn = HRESULT(STDMETHODCALLTYPE*)(ID3D11Device*,
                                                      const D3D11_TEXTURE2D_DESC*,
                                                      const D3D11_SUBRESOURCE_DATA*,
                                                      ID3D11Texture2D**);
using CreateRtvFn = HRESULT(STDMETHODCALLTYPE*)(ID3D11Device*, ID3D11Resource*,
                                                const D3D11_RENDER_TARGET_VIEW_DESC*,
                                                ID3D11RenderTargetView**);
using CreateDsvFn = HRESULT(STDMETHODCALLTYPE*)(ID3D11Device*, ID3D11Resource*,
                                                const D3D11_DEPTH_STENCIL_VIEW_DESC*,
                                                ID3D11DepthStencilView**);
using DrawIndexedFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT, INT);
using DrawFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT);
using OMSetRenderTargetsFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT,
                                                      ID3D11RenderTargetView* const*,
                                                      ID3D11DepthStencilView*);
using RSSetViewportsFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT,
                                                  const D3D11_VIEWPORT*);

// ── Capture log: one mutex, one file, flushed periodically ──────────────────

class CaptureLog {
public:
    bool open() {
        const char* fromEnv = std::getenv("RENDERLIFT_LOG");
        const std::string path = (fromEnv != nullptr && fromEnv[0] != '\0')
                                     ? fromEnv
                                     : "RenderLift.D3D11.log";
        out_.open(path, std::ios::out | std::ios::trunc);
        return out_.is_open();
    }

    void write(const obs::Event& event) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!out_.is_open()) return;
        out_ << obs::formatEvent(event) << '\n';
        if (++sinceFlush_ >= 128) {
            sinceFlush_ = 0;
            out_.flush();
        }
    }

    void writeView(bool dsv, const obs::ViewCreatedEvent& e) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!out_.is_open()) return;
        out_ << (dsv ? obs::formatDsvCreated(e) : obs::formatRtvCreated(e)) << '\n';
        if (++sinceFlush_ >= 128) {
            sinceFlush_ = 0;
            out_.flush();
        }
    }

    void writeRaw(const std::string& line) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!out_.is_open()) return;
        out_ << line << '\n';
    }

    void flush() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (out_.is_open()) out_.flush();
    }

private:
    std::mutex mutex_;
    std::ofstream out_;
    std::uint32_t sinceFlush_ = 0;
};

// ── Module state ────────────────────────────────────────────────────────────

struct FrameDrawCounters {
    std::uint32_t calls = 0;
    std::uint32_t maxIndices = 0;
    std::uint32_t maxVertices = 0;
};

struct ModuleState {
    std::mutex mutex;    // guards the non-atomic members below
    std::unique_ptr<IHookEngine> hooks;
    CaptureLog log;

    PresentFn originalPresent = nullptr;
    ResizeBuffersFn originalResizeBuffers = nullptr;
    CreateTexture2DFn originalCreateTexture2D = nullptr;
    CreateRtvFn originalCreateRtv = nullptr;
    CreateDsvFn originalCreateDsv = nullptr;
    DrawIndexedFn originalDrawIndexed = nullptr;
    DrawFn originalDraw = nullptr;
    OMSetRenderTargetsFn originalOMSetRenderTargets = nullptr;
    RSSetViewportsFn originalRSSetViewports = nullptr;

    bool installed = false;
    std::atomic<bool> observing{false};  // read on the hot path without the mutex
    std::uint64_t frame = 0;
    std::uint32_t observationCap = 600;  // frames; 0 = unlimited
    std::uint64_t presentCount = 0;
    FrameDrawCounters draw;
};

ModuleState& state() {
    static ModuleState s;
    return s;
}

std::uint32_t observationCapFromEnv() {
    if (const char* env = std::getenv("RENDERLIFT_OBSERVE_FRAMES")) {
        const unsigned long v = std::strtoul(env, nullptr, 10);
        return static_cast<std::uint32_t>(v);
    }
    return 600;
}

// ── Vtable bootstrap ────────────────────────────────────────────────────────

struct Vtables {
    void* present = nullptr;
    void* resizeBuffers = nullptr;
    void* createTexture2D = nullptr;
    void* createRtv = nullptr;
    void* createDsv = nullptr;
    void* drawIndexed = nullptr;
    void* draw = nullptr;
    void* omSetRenderTargets = nullptr;
    void* rsSetViewports = nullptr;
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

bool resolveVtables(Vtables& out) {
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = DefWindowProcW;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.lpszClassName = L"RenderLiftD3D11Probe";
    (void)RegisterClassExW(&windowClass);

    HWND hwnd = CreateWindowExW(0, windowClass.lpszClassName, L"RenderLift",
                                WS_DISABLED | WS_POPUP, 0, 0, 8, 8, nullptr, nullptr,
                                windowClass.hInstance, nullptr);
    if (hwnd == nullptr) return false;

    IDXGISwapChain* swapchain = nullptr;
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    const bool created =
        tryCreateProbe(D3D_DRIVER_TYPE_HARDWARE, hwnd, &swapchain, &device, &context) ||
        tryCreateProbe(D3D_DRIVER_TYPE_WARP, hwnd, &swapchain, &device, &context);

    bool ok = false;
    if (created) {
        const VTable sc(swapchain);
        const VTable dev(device);
        const VTable ctx(context);
        out.present = sc.functionAt(slot::Present);
        out.resizeBuffers = sc.functionAt(slot::ResizeBuffers);
        out.createTexture2D = dev.functionAt(slot::CreateTexture2D);
        out.createRtv = dev.functionAt(slot::CreateRenderTargetView);
        out.createDsv = dev.functionAt(slot::CreateDepthStencilView);
        out.drawIndexed = ctx.functionAt(slot::DrawIndexed);
        out.draw = ctx.functionAt(slot::Draw);
        out.omSetRenderTargets = ctx.functionAt(slot::OMSetRenderTargets);
        out.rsSetViewports = ctx.functionAt(slot::RSSetViewports);
        ok = out.present && out.resizeBuffers && out.createTexture2D && out.createRtv &&
             out.omSetRenderTargets && out.rsSetViewports;
    }

    if (context != nullptr) context->Release();
    if (device != nullptr) device->Release();
    if (swapchain != nullptr) swapchain->Release();
    DestroyWindow(hwnd);
    UnregisterClassW(windowClass.lpszClassName, windowClass.hInstance);
    return ok;
}

// ── Detours (observation-only) ──────────────────────────────────────────────

bool observing() { return state().observing.load(std::memory_order_relaxed); }

// Draw* are the hottest hooks in the game — counting is all they may do.
void STDMETHODCALLTYPE DrawIndexed_Hook(ID3D11DeviceContext* ctx, UINT indexCount,
                                        UINT startIndexLocation, INT baseVertexLocation) {
    ModuleState& s = state();
    if (observing()) {
        std::lock_guard<std::mutex> lock(s.mutex);
        ++s.draw.calls;
        if (indexCount > s.draw.maxIndices) s.draw.maxIndices = indexCount;
    }
    s.originalDrawIndexed(ctx, indexCount, startIndexLocation, baseVertexLocation);
}

void STDMETHODCALLTYPE Draw_Hook(ID3D11DeviceContext* ctx, UINT vertexCount,
                                 UINT startVertexLocation) {
    ModuleState& s = state();
    if (observing()) {
        std::lock_guard<std::mutex> lock(s.mutex);
        ++s.draw.calls;
        if (vertexCount > s.draw.maxVertices) s.draw.maxVertices = vertexCount;
    }
    s.originalDraw(ctx, vertexCount, startVertexLocation);
}

void STDMETHODCALLTYPE OMSetRenderTargets_Hook(ID3D11DeviceContext* ctx, UINT numViews,
                                               ID3D11RenderTargetView* const* ppRtv,
                                               ID3D11DepthStencilView* pDsv) {
    ModuleState& s = state();
    if (observing()) {
        obs::RenderTargetsEvent e{};
        e.context = reinterpret_cast<std::uint64_t>(ctx);
        e.count = (numViews > obs::kMaxRtvs) ? obs::kMaxRtvs : numViews;
        for (UINT i = 0; i < e.count && ppRtv != nullptr; ++i) {
            e.rtvs[i] = reinterpret_cast<std::uint64_t>(ppRtv[i]);
        }
        e.dsv = reinterpret_cast<std::uint64_t>(pDsv);
        s.log.write(e);
    }
    s.originalOMSetRenderTargets(ctx, numViews, ppRtv, pDsv);
}

void STDMETHODCALLTYPE RSSetViewports_Hook(ID3D11DeviceContext* ctx, UINT numViewports,
                                           const D3D11_VIEWPORT* pViewports) {
    ModuleState& s = state();
    if (observing() && pViewports != nullptr && numViewports > 0) {
        obs::ViewportEvent e{};
        e.context = reinterpret_cast<std::uint64_t>(ctx);
        e.count = numViewports;
        e.x = pViewports[0].TopLeftX;
        e.y = pViewports[0].TopLeftY;
        e.w = pViewports[0].Width;
        e.h = pViewports[0].Height;
        s.log.write(e);
    }
    s.originalRSSetViewports(ctx, numViewports, pViewports);
}

HRESULT STDMETHODCALLTYPE CreateTexture2D_Hook(ID3D11Device* dev,
                                               const D3D11_TEXTURE2D_DESC* pDesc,
                                               const D3D11_SUBRESOURCE_DATA* pInitialData,
                                               ID3D11Texture2D** ppTexture2D) {
    ModuleState& s = state();
    const HRESULT hr = s.originalCreateTexture2D(dev, pDesc, pInitialData, ppTexture2D);
    if (observing() && SUCCEEDED(hr) && ppTexture2D != nullptr && *ppTexture2D != nullptr) {
        D3D11_TEXTURE2D_DESC desc{};
        (*ppTexture2D)->GetDesc(&desc);
        obs::TextureCreatedEvent e{};
        e.device = reinterpret_cast<std::uint64_t>(dev);
        e.id = reinterpret_cast<std::uint64_t>(*ppTexture2D);
        e.width = desc.Width;
        e.height = desc.Height;
        e.dxgiFormat = static_cast<std::uint32_t>(desc.Format);
        e.bindFlags = desc.BindFlags;
        e.mipLevels = desc.MipLevels;
        e.arraySize = desc.ArraySize;
        e.samples = desc.SampleDesc.Count;
        s.log.write(e);
    }
    return hr;
}

HRESULT STDMETHODCALLTYPE CreateRtv_Hook(ID3D11Device* dev, ID3D11Resource* pResource,
                                         const D3D11_RENDER_TARGET_VIEW_DESC* pDesc,
                                         ID3D11RenderTargetView** ppRTView) {
    ModuleState& s = state();
    const HRESULT hr = s.originalCreateRtv(dev, pResource, pDesc, ppRTView);
    if (observing() && SUCCEEDED(hr) && ppRTView != nullptr && *ppRTView != nullptr) {
        obs::ViewCreatedEvent e{};
        e.device = reinterpret_cast<std::uint64_t>(dev);
        e.viewId = reinterpret_cast<std::uint64_t>(*ppRTView);
        e.resourceId = reinterpret_cast<std::uint64_t>(pResource);
        s.log.writeView(/*dsv=*/false, e);
    }
    return hr;
}

HRESULT STDMETHODCALLTYPE CreateDsv_Hook(ID3D11Device* dev, ID3D11Resource* pResource,
                                         const D3D11_DEPTH_STENCIL_VIEW_DESC* pDesc,
                                         ID3D11DepthStencilView** ppDSView) {
    ModuleState& s = state();
    const HRESULT hr = s.originalCreateDsv(dev, pResource, pDesc, ppDSView);
    if (observing() && SUCCEEDED(hr) && ppDSView != nullptr && *ppDSView != nullptr) {
        obs::ViewCreatedEvent e{};
        e.device = reinterpret_cast<std::uint64_t>(dev);
        e.viewId = reinterpret_cast<std::uint64_t>(*ppDSView);
        e.resourceId = reinterpret_cast<std::uint64_t>(pResource);
        s.log.writeView(/*dsv=*/true, e);
    }
    return hr;
}

HRESULT STDMETHODCALLTYPE Present_Hook(IDXGISwapChain* swapchain, UINT syncInterval,
                                       UINT flags) {
    ModuleState& s = state();
    {
        std::lock_guard<std::mutex> lock(s.mutex);
        ++s.frame;
        ++s.presentCount;

        if (s.observing) {
            // Per-frame aggregate first, then the frame marker.
            obs::DrawStatEvent stat{};
            stat.frame = s.frame;
            stat.calls = s.draw.calls;
            stat.maxIndices = s.draw.maxIndices;
            stat.maxVertices = s.draw.maxVertices;
            s.log.write(stat);

            obs::PresentEvent e{};
            e.swapchain = reinterpret_cast<std::uint64_t>(swapchain);
            e.frame = s.frame;
            s.log.write(e);

            if (s.frame % 30 == 0) s.log.flush();

            if (s.observationCap > 0 && s.frame >= s.observationCap) {
                s.log.writeRaw("RLCAP1 cap frames=" + std::to_string(s.frame));
                s.log.flush();
                s.observing = false;  // passthrough overhead drops to ~nothing
            }
        }
        s.draw = FrameDrawCounters{};
    }
    return s.originalPresent(swapchain, syncInterval, flags);
}

HRESULT STDMETHODCALLTYPE ResizeBuffers_Hook(IDXGISwapChain* swapchain, UINT bufferCount,
                                             UINT width, UINT height, DXGI_FORMAT format,
                                             UINT swapchainFlags) {
    ModuleState& s = state();
    if (s.observing) {
        s.log.writeRaw("RLCAP1 resize w=" + std::to_string(width) +
                       " h=" + std::to_string(height));
    }
    return s.originalResizeBuffers(swapchain, bufferCount, width, height, format,
                                   swapchainFlags);
}

// ── Install / uninstall ─────────────────────────────────────────────────────

HookStatus createHook(ModuleState& s, void* target, void* detour, void** original) {
    return s.hooks->create(target, detour, original);
}

HRESULT installLocked(ModuleState& s) {
    Vtables vt;
    if (!resolveVtables(vt)) return E_FAIL;

    struct Install {
        void* target;
        void* detour;
        void** original;
    };
    const Install installs[] = {
        {vt.present, reinterpret_cast<void*>(&Present_Hook),
         reinterpret_cast<void**>(&s.originalPresent)},
        {vt.resizeBuffers, reinterpret_cast<void*>(&ResizeBuffers_Hook),
         reinterpret_cast<void**>(&s.originalResizeBuffers)},
        {vt.createTexture2D, reinterpret_cast<void*>(&CreateTexture2D_Hook),
         reinterpret_cast<void**>(&s.originalCreateTexture2D)},
        {vt.createRtv, reinterpret_cast<void*>(&CreateRtv_Hook),
         reinterpret_cast<void**>(&s.originalCreateRtv)},
        {vt.createDsv, reinterpret_cast<void*>(&CreateDsv_Hook),
         reinterpret_cast<void**>(&s.originalCreateDsv)},
        {vt.drawIndexed, reinterpret_cast<void*>(&DrawIndexed_Hook),
         reinterpret_cast<void**>(&s.originalDrawIndexed)},
        {vt.draw, reinterpret_cast<void*>(&Draw_Hook), reinterpret_cast<void**>(&s.originalDraw)},
        {vt.omSetRenderTargets, reinterpret_cast<void*>(&OMSetRenderTargets_Hook),
         reinterpret_cast<void**>(&s.originalOMSetRenderTargets)},
        {vt.rsSetViewports, reinterpret_cast<void*>(&RSSetViewports_Hook),
         reinterpret_cast<void**>(&s.originalRSSetViewports)},
    };
    for (const Install& i : installs) {
        if (i.target == nullptr) continue;
        if (!succeeded(createHook(s, i.target, i.detour, i.original))) return E_FAIL;
    }
    if (!succeeded(s.hooks->enableAll())) return E_FAIL;

    s.observing = true;
    s.installed = true;
    return S_OK;
}

HRESULT uninstallLocked(ModuleState& s) {
    s.observing = false;
    if (s.hooks) {
        s.hooks->shutdown();
        s.hooks.reset();
    }
    s.originalPresent = nullptr;
    s.originalResizeBuffers = nullptr;
    s.originalCreateTexture2D = nullptr;
    s.originalCreateRtv = nullptr;
    s.originalCreateDsv = nullptr;
    s.originalDrawIndexed = nullptr;
    s.originalDraw = nullptr;
    s.originalOMSetRenderTargets = nullptr;
    s.originalRSSetViewports = nullptr;
    s.log.flush();
    s.installed = false;
    return S_OK;
}

}  // namespace
}  // namespace rl::backend

// ── Injection contract ──────────────────────────────────────────────────────

extern "C" {

RENDERLIFT_D3D11_API HRESULT RenderLiftInstall(void) {
    rl::backend::ModuleState& s = rl::backend::state();
    std::lock_guard<std::mutex> lock(s.mutex);
    if (s.installed) return S_OK;

    s.hooks = rl::backend::createHookEngine();
    if (s.hooks == nullptr || !rl::backend::succeeded(s.hooks->initialize())) {
        s.hooks.reset();
        return E_FAIL;
    }
    if (!s.log.open()) {
        // Best effort: observation without a file log is still useful for the
        // frame counter, but report failure so the loader can react.
        return HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED);
    }
    s.observationCap = rl::backend::observationCapFromEnv();
    s.log.writeRaw("RLCAP1 hello module=RenderLift.D3D11 mode=observe cap=" +
                   std::to_string(s.observationCap));
    return rl::backend::installLocked(s);
}

RENDERLIFT_D3D11_API HRESULT RenderLiftUninstall(void) {
    rl::backend::ModuleState& s = rl::backend::state();
    std::lock_guard<std::mutex> lock(s.mutex);
    s.log.writeRaw("RLCAP1 bye frames=" + std::to_string(s.frame));
    s.log.flush();
    return rl::backend::uninstallLocked(s);
}

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
        rl::backend::ModuleState& s = rl::backend::state();
        if (s.installed) (void)rl::backend::uninstallLocked(s);
    }
    return TRUE;
}
