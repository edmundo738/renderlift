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
// Output: "RenderLift.D3D11.log" (RLCAP1 lines, next to this DLL by default)
//   — inspect offline with:
//   RenderLift.CLI inspect RenderLift.D3D11.log
// Entry witness: "RenderLift.entry" (32-byte binary mark, CREATE_ALWAYS at
//   the top of RenderLiftInstall, kernel32-only — see the v3.1 evidence
//   transport block below).
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
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <mutex>
#include <new>
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

// ── Evidence transport v3.1: kernel32-only, entry-SEH-safe ───────────────────
//
// Lab history (GTA V Legacy 1.0.3889.0):
//   run 2 (v2): 0xc0000005, WER fault offset == RenderLiftInstall entry VA,
//               zero log bytes — crash before the buffered logger existed.
//   run 3 (v3): 0xc0000005 again, NO log file at all, no cp=1. Post-mortem:
//               cp=1 ran OUTSIDE the __try, and its chain was still CRT-heavy
//               (vsnprintf + fopen_s/fputs/fclose — CRT stdio allocates,
//               locks, and lazily initializes locale). An AV inside that
//               chain produces exactly "0xc0000005 + no cp=1 + no file",
//               so "missing cp=1" proved nothing about pre-DLL failure.
//
// v3.1 contract (the whole observable entry now lives INSIDE the SEH guard):
//   1. EVERY observable step — binary entry mark, cp=1, installSteps — runs
//      under __try; the filter reports phase + code + ExceptionAddress.
//   2. The evidence TRANSPORT below touches kernel32 only: CreateFileA/
//      WriteFile/CloseHandle + PEB env reads. No CRT stdio, no vsnprintf,
//      no heap, no locks — usable from the very first observable byte and
//      equally safe inside the SEH filter.
//   3. A binary entry mark (RenderLift.entry, CREATE_ALWAYS, 32 bytes) is
//      written FIRST: its mere existence + mtime answers "did execution
//      reach the first observable byte?" with a yes/no file artifact,
//      independent of text-log plumbing. Then the cp=1..80 text trail runs
//      on the same transport.
//
// This block deliberately does NOT use the CaptureLog class.
char gModuleDir[MAX_PATH] = "";      // set by DllMain(ATTACH); "" until then
char gMarkPath[MAX_PATH] = "";       // "<moduleDir>\RenderLift.entry" (DllMain)
volatile LONG gInstallPhase = 0;     // last checkpoint id written (SEH reads it)
volatile LONG gInstallAttempts = 0;  // incremented per RenderLiftInstall call

// Evidence path: RENDERLIFT_LOG env, else next to the MODULE (never next to
// GTA5.exe — the game dir may be read-only; the lab dir is ours).
bool resolveLogPath(char* out, size_t outSize) {
    const DWORD n = GetEnvironmentVariableA("RENDERLIFT_LOG", out, (DWORD)outSize);
    if (n > 0 && n < outSize) return true;
    if (gModuleDir[0] != '\0') {
        std::snprintf(out, outSize, "%s\\RenderLift.D3D11.log", gModuleDir);
        return true;
    }
    std::snprintf(out, outSize, "RenderLift.D3D11.log");
    return true;
}

// ── kernel32-only primitives (no CRT anywhere below) ────────────────────────

DWORD strLen(const char* s) {
    DWORD n = 0;
    while (s[n] != '\0') ++n;
    return n;
}

// Append bytes to a file; silent no-op on failure (evidence is best-effort,
// never fatal). CreateFileA/WriteFile/CloseHandle only.
void k32Append(const char* path, const char* data, DWORD len) {
    const HANDLE h = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                                 OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    SetFilePointer(h, 0, nullptr, FILE_END);
    DWORD wrote = 0;
    WriteFile(h, data, len, &wrote, nullptr);
    CloseHandle(h);
}

void k32Overwrite(const char* path, const void* data, DWORD len) {
    const HANDLE h = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                                 CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD wrote = 0;
    WriteFile(h, data, len, &wrote, nullptr);
    CloseHandle(h);
}

// "%TEMP%\<name>" for the fallback evidence copy (PEB env, kernel32 only).
bool tempPath(char* out, size_t outSize, const char* name) {
    char dir[MAX_PATH] = "";
    const DWORD n = GetEnvironmentVariableA("TEMP", dir, sizeof dir);
    if (n == 0 || n >= sizeof dir) return false;
    std::snprintf(out, outSize, "%s\\%s", dir, name);
    return true;
}

// One log line, kernel32-only, primary path then %TEMP% fallback.
void earlyLogRaw(const char* line) {
    DWORD len = strLen(line);
    char primary[MAX_PATH];
    resolveLogPath(primary, sizeof primary);
    k32Append(primary, line, len);
    k32Append(primary, "\n", 1);
    // Duplicate into %TEMP% so a primary ACL/silence problem can never eat
    // the only evidence: two sinks, one of them user-writable by design.
    char fallback[MAX_PATH];
    if (tempPath(fallback, sizeof fallback, "RenderLift.D3D11.log")) {
        k32Append(fallback, line, len);
        k32Append(fallback, "\n", 1);
    }
}

// ── CRT-free formatting (hand-rolled: %s %c %% %d %i %u %ld %lu %zu %x %08lx %p) ──
void fmtStr(char*& w, const char* end, const char* s) {
    if (s == nullptr) s = "(null)";
    while (*s != '\0' && w < end - 1) *w++ = *s++;
}
void fmtU64(char*& w, const char* end, unsigned long long v, unsigned base,
            int width, char pad) {
    char tmp[32];
    int n = 0;
    do {
        tmp[n++] = "0123456789abcdef"[v % base];
        v /= base;
    } while (v != 0 && n < 32);
    while (width-- > n && w < end - 1) *w++ = pad;
    while (n > 0 && w < end - 1) *w++ = tmp[--n];
}
void fmtI64(char*& w, const char* end, long long v) {
    if (v < 0) {
        if (w < end - 1) *w++ = '-';
        fmtU64(w, end, static_cast<unsigned long long>(-v), 10, 0, ' ');
    } else {
        fmtU64(w, end, static_cast<unsigned long long>(v), 10, 0, ' ');
    }
}

void earlyLogf(const char* fmt, ...) {
    char buf[512];
    char* w = buf;
    const char* end = buf + sizeof buf;
    va_list args;
    va_start(args, fmt);
    for (const char* p = fmt; *p != '\0' && w < end - 1; ++p) {
        if (*p != '%') {
            *w++ = *p;
            continue;
        }
        ++p;
        if (*p == '%') {
            *w++ = '%';
            continue;
        }
        char pad = ' ';
        int width = 0;
        if (*p == '0') {
            pad = '0';
            ++p;
        }
        while (*p >= '0' && *p <= '9') {
            width = width * 10 + (*p - '0');
            ++p;
        }
        char len = '\0';
        if (*p == 'l' || *p == 'z') len = *p++;
        switch (*p) {
        case 's':
            fmtStr(w, end, va_arg(args, const char*));
            break;
        case 'c':
            if (w < end - 1) *w++ = static_cast<char>(va_arg(args, int));
            break;
        case 'd':
        case 'i':
            if (len == 'l') fmtI64(w, end, va_arg(args, long));
            else fmtI64(w, end, va_arg(args, int));
            break;
        case 'u':
            if (len == 'l') fmtU64(w, end, va_arg(args, unsigned long), 10, width, pad);
            else if (len == 'z') fmtU64(w, end, va_arg(args, size_t), 10, width, pad);
            else fmtU64(w, end, va_arg(args, unsigned), 10, width, pad);
            break;
        case 'x':
            fmtU64(w, end, (len == 'l') ? va_arg(args, unsigned long) : va_arg(args, unsigned),
                   16, width, pad);
            break;
        case 'p': {
            fmtStr(w, end, "0x");
            const uintptr_t v = reinterpret_cast<uintptr_t>(va_arg(args, void*));
            fmtU64(w, end, static_cast<unsigned long long>(v), 16, 16, '0');
            break;
        }
        default:
            if (w < end - 1) *w++ = '%';
            if (*p != '\0' && w < end - 1) *w++ = *p;
            break;
        }
        if (*p == '\0') break;
    }
    va_end(args);
    *w = '\0';
    earlyLogRaw(buf);
}

// Checkpoint: bump the global phase AND put a durable line on disk. The
// install contract checks these to localize failures exactly.
void setPhase(LONG phase) {
    InterlockedExchange(&gInstallPhase, phase);
    earlyLogf("RLCAP1 install cp=%ld", phase);
}

// Binary entry witness: 32 bytes, CREATE_ALWAYS — the file's existence +
// mtime proves the entry code executed, even if every later line is lost.
struct EntryMark {
    char magic[8];      // "RLENT01"
    DWORD pid;
    DWORD tid;
    DWORD tick;         // GetTickCount at entry
    LONG installAttempt;
    LONG reserved;
    BYTE pad[8];
};

void writeEntryMark() {
    EntryMark m{};
    m.magic[0] = 'R'; m.magic[1] = 'L'; m.magic[2] = 'E'; m.magic[3] = 'N';
    m.magic[4] = 'T'; m.magic[5] = '0'; m.magic[6] = '1'; m.magic[7] = '\0';
    m.pid = GetCurrentProcessId();
    m.tid = GetCurrentThreadId();
    m.tick = GetTickCount();
    m.installAttempt = InterlockedIncrement(&gInstallAttempts);
    m.reserved = 0;
    if (gMarkPath[0] != '\0') {
        k32Overwrite(gMarkPath, &m, sizeof m);
        return;
    }
    char fallback[MAX_PATH];
    if (tempPath(fallback, sizeof fallback, "RenderLift.entry")) {
        k32Overwrite(fallback, &m, sizeof m);
    }
}

class CaptureLog {
public:
    bool open() {
        char path[MAX_PATH];
        resolveLogPath(path, sizeof path);
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

// The state block is constructed EXPLICITLY during install (checkpoint 2),
// never via a function-local magic static touched at entry: magic-static
// construction involves CRT guard variables, mutex and std::ofstream
// constructors running before the very first evidence line — exactly the
// kind of early work that must not be needed by the install contract.
ModuleState* gState = nullptr;

// Precondition: gState != nullptr (armed). All hook detours run only after
// the contract armed the module, so the premise holds on every hot path.
ModuleState& state() { return *gState; }

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
    setPhase(50);
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = DefWindowProcW;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.lpszClassName = L"RenderLiftD3D11Probe";
    (void)RegisterClassExW(&windowClass);

    setPhase(51);
    HWND hwnd = CreateWindowExW(0, windowClass.lpszClassName, L"RenderLift",
                                WS_DISABLED | WS_POPUP, 0, 0, 8, 8, nullptr, nullptr,
                                windowClass.hInstance, nullptr);
    if (hwnd == nullptr) {
        earlyLogRaw("RLCAP1 fail probe createwindow");
        return false;
    }

    setPhase(52);
    IDXGISwapChain* swapchain = nullptr;
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    bool created = tryCreateProbe(D3D_DRIVER_TYPE_HARDWARE, hwnd, &swapchain, &device, &context);
    earlyLogf("RLCAP1 probe hardware=%d device=%p", created ? 1 : 0, static_cast<void*>(device));
    if (!created) {
        setPhase(53);
        created = tryCreateProbe(D3D_DRIVER_TYPE_WARP, hwnd, &swapchain, &device, &context);
        earlyLogf("RLCAP1 probe warp=%d device=%p", created ? 1 : 0, static_cast<void*>(device));
    }

    setPhase(54);
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
        earlyLogf("RLCAP1 vtables present=%p draw=%p om=%p ok=%d", out.present, out.draw,
                  out.omSetRenderTargets, ok ? 1 : 0);
    }

    setPhase(55);
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
//
// Returned HRESULTs are part of the evidence protocol (never masked):
//
//   0x8000A001  state block allocation failed
//   0x8000A002  evidence channel (capture log) could not be opened
//   0x8000A003  hook engine initialization failed
//   0x8000A010  vtable resolution failed
//   0x8000A020+i  hook creation failed for slot i (0-based, install table)
//   0x8000A030  enabling hooks failed
//   0xE(cp)xxxxx  structured-exception evidence: high nibble carries the
//                 install phase, low 24 bits the exception code; the full
//                 exception record is written to the log by the SEH filter.
//
// Checkpoint ids (gInstallPhase / "RLCAP1 install cp=N"):
//   mark binary entry witness (RenderLift.entry) → "RLCAP1 entered" →
//   1 entered  · 2 state block · 3 log open · 4 hook engine
//   5 vtables  · 50..56 probe sub-steps · 60+i per-hook · 70 enableAll
//   80 armed
// All of them — including the mark and cp=1 — run INSIDE the entry __try,
// on the CRT-free kernel32 transport (v3.1 protocol).

constexpr HRESULT RL_E_STATE_ALLOC = static_cast<HRESULT>(0x8000A001UL);
constexpr HRESULT RL_E_LOG_OPEN = static_cast<HRESULT>(0x8000A002UL);
constexpr HRESULT RL_E_ENGINE_INIT = static_cast<HRESULT>(0x8000A003UL);
constexpr HRESULT RL_E_VTABLES = static_cast<HRESULT>(0x8000A010UL);
constexpr HRESULT RL_E_HOOK_CREATE_0 = static_cast<HRESULT>(0x8000A020UL);  // + slot idx
constexpr HRESULT RL_E_HOOK_ENABLE = static_cast<HRESULT>(0x8000A030UL);

HookStatus createHook(ModuleState& s, void* target, void* detour, void** original) {
    return s.hooks->create(target, detour, original);
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

// Happy path. Runs fully inside the SEH guard of RenderLiftInstall; every
// externally-visible step is checkpointed first.
HRESULT installSteps() {
    setPhase(2);
    if (gState == nullptr) {
        gState = new (std::nothrow) ModuleState();
        if (gState == nullptr) return RL_E_STATE_ALLOC;
    }
    ModuleState& s = *gState;
    if (s.installed) return S_OK;
    std::lock_guard<std::mutex> lock(s.mutex);

    setPhase(3);
    if (!s.log.open()) return RL_E_LOG_OPEN;
    s.log.writeRaw("RLCAP1 hello module=RenderLift.D3D11 mode=observe");
    s.observationCap = observationCapFromEnv();
    earlyLogf("RLCAP1 hello cap=%lu", static_cast<unsigned long>(s.observationCap));

    setPhase(4);
    s.hooks = createHookEngine();
    if (s.hooks == nullptr || !succeeded(s.hooks->initialize())) {
        s.hooks.reset();
        earlyLogRaw("RLCAP1 fail engine_init");
        return RL_E_ENGINE_INIT;
    }

    setPhase(5);
    Vtables vt;
    if (!resolveVtables(vt)) {
        earlyLogRaw("RLCAP1 fail vtables");
        return RL_E_VTABLES;
    }

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
        {vt.draw, reinterpret_cast<void*>(&Draw_Hook),
         reinterpret_cast<void**>(&s.originalDraw)},
        {vt.omSetRenderTargets, reinterpret_cast<void*>(&OMSetRenderTargets_Hook),
         reinterpret_cast<void**>(&s.originalOMSetRenderTargets)},
        {vt.rsSetViewports, reinterpret_cast<void*>(&RSSetViewports_Hook),
         reinterpret_cast<void**>(&s.originalRSSetViewports)},
    };
    for (std::size_t i = 0; i < sizeof installs / sizeof installs[0]; ++i) {
        setPhase(60 + static_cast<LONG>(i));
        if (installs[i].target == nullptr) continue;
        if (!succeeded(createHook(s, installs[i].target, installs[i].detour,
                                  installs[i].original))) {
            earlyLogf("RLCAP1 fail hook slot=%zu target=%p", i, installs[i].target);
            return RL_E_HOOK_CREATE_0 + static_cast<HRESULT>(i);
        }
    }

    setPhase(70);
    if (!succeeded(s.hooks->enableAll())) {
        earlyLogRaw("RLCAP1 fail enableAll");
        return RL_E_HOOK_ENABLE;
    }

    setPhase(80);
    s.observing = true;
    s.installed = true;
    earlyLogRaw("RLCAP1 armed hooks=9 mode=observe");
    return S_OK;
}

// SEH evidence filter: records phase + exception code + the EXACT faulting
// address (inside our DLL or elsewhere) to the durable log, then lets the
// handler return a coded HRESULT. This is diagnosis, never masking: the
// returned value still encodes the failure phase and exception completely.
LONG sehFilter(EXCEPTION_POINTERS* ep, HRESULT* hrOut) {
    const DWORD code =
        (ep != nullptr && ep->ExceptionRecord != nullptr) ? ep->ExceptionRecord->ExceptionCode : 0;
    const void* addr = (ep != nullptr && ep->ExceptionRecord != nullptr)
                           ? ep->ExceptionRecord->ExceptionAddress
                           : nullptr;
    const LONG phase = InterlockedCompareExchange(&gInstallPhase, 0, 0);
    earlyLogf("RLCAP1 seh phase=%ld code=0x%08lx addr=%p", phase,
              static_cast<unsigned long>(code), addr);
    if (hrOut != nullptr) {
        *hrOut = static_cast<HRESULT>(0xE0000000L | ((static_cast<DWORD>(phase) & 0xF) << 24) |
                                      (code & 0x00FFFFFFL));
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

// } inside SEH ╌ note: the guard frames below must stay POD-only (C2712), so
// all C++ work lives in helper steps.
HRESULT uninstallSteps() {
    if (gState == nullptr) return S_OK;  // never installed
    ModuleState& s = *gState;
    std::lock_guard<std::mutex> lock(s.mutex);
    s.log.writeRaw("RLCAP1 bye frames=" + std::to_string(s.frame));
    s.log.flush();
    return uninstallLocked(s);
}

}  // namespace
}  // namespace rl::backend

// ── Injection contract ──────────────────────────────────────────────────────
//
// Signatures follow the thread-proc shape (LPVOID arg, WINAPI/stdcall) so the
// loader's CreateRemoteThread call is ABI-exact — not merely compatible.
// The argument is unused; the loader passes nullptr.

extern "C" {

RENDERLIFT_D3D11_API HRESULT WINAPI RenderLiftInstall(LPVOID /*unused*/) {
    // v3.1: the ENTIRE observable entry lives inside the SEH guard — the
    // binary entry mark, cp=1 and every later step. An AV anywhere (even in
    // the evidence path itself) is caught by the filter and reported with
    // phase + ExceptionAddress + a coded HRESULT.
    HRESULT hr = E_UNEXPECTED;
    __try {
        // 1) Binary witness FIRST — kernel32 only: if this file exists, the
        //    entry reached the first observable byte. Answers the retest
        //    question without any dependence on text-log plumbing.
        rl::backend::writeEntryMark();
        // 2) First text evidence + checkpoint 1 on the same k32 transport.
        rl::backend::earlyLogRaw("RLCAP1 entered proto=k32mark");
        rl::backend::setPhase(1);
        // 3) Armed install path (checkpoints 2..80 inside, each on k32).
        hr = rl::backend::installSteps();
    } __except (rl::backend::sehFilter(GetExceptionInformation(), &hr)) {
        // Evidence written by the filter; hr carries phase + exception code.
    }
    return hr;
}

RENDERLIFT_D3D11_API HRESULT WINAPI RenderLiftUninstall(LPVOID /*unused*/) {
    rl::backend::earlyLogRaw("RLCAP1 uninstall requested");
    HRESULT hr = E_UNEXPECTED;
    __try {
        hr = rl::backend::uninstallSteps();
    } __except (rl::backend::sehFilter(GetExceptionInformation(), &hr)) {
    }
    return hr;
}

RENDERLIFT_D3D11_API std::uint64_t WINAPI RenderLiftFrameCount(LPVOID /*unused*/) {
    const rl::backend::ModuleState* s = rl::backend::gState;
    return s != nullptr ? s->presentCount : 0;
}

// ── CreateRemoteThread frontier probe (lab experiment, v3.2) ────────────────
//
// Answers ONE question: "does a minimal remote thread entry survive at all?"
// Discipline: absolute minimum dependencies — no CRT (no stdio/printf/
// malloc), no STL, no writable globals, no D3D11, no MinHook, no SEH. Only:
//   · the LPVOID parameter (ANSI path written remotely by the loader),
//   · one read-only literal in .rdata,
//   · kernel32 imports via the IAT.
// Behavior: write "RLPROBE1 ok" to %param% (or .\RenderLift.probe if null),
// ALWAYS return 0x12345678. Two witnesses, two outcomes:
//   file exists + 0x12345678 → the frontier (remote thread → entry) works;
//   no file + 0xC0000005     → the failure is in remote-thread DELIVERY,
//                              before any minimal body — not D3D11/MinHook.
RENDERLIFT_D3D11_API DWORD WINAPI RenderLiftEntryProbe(LPVOID param) {
    static const char kMagic[] = "RLPROBE1 ok\r\n";  // .rdata, no init
    const char* path =
        (param != nullptr) ? static_cast<const char*>(param) : "RenderLift.probe";
    const HANDLE h = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                                 CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
        DWORD wrote = 0;
        WriteFile(h, kMagic, static_cast<DWORD>(sizeof(kMagic) - 1), &wrote, nullptr);
        CloseHandle(h);
    }
    return 0x12345678ul;
}

}  // extern "C"

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        // Cache the module directory for the evidence path. Only kernel32
        // services and POD C-string ops are used below — allowed by the
        // loader-lock rules; RenderLiftInstall does the real work later.
        wchar_t wpath[MAX_PATH]{};
        if (GetModuleFileNameW(module, wpath, MAX_PATH) > 0) {
            wchar_t* last = wcsrchr(wpath, L'\\');
            if (last != nullptr) *last = L'\0';
            WideCharToMultiByte(CP_ACP, 0, wpath, -1, rl::backend::gModuleDir,
                                MAX_PATH, nullptr, nullptr);
            // Precompute the binary entry-mark path so the very first
            // observable byte needs zero path work.
            if (rl::backend::gModuleDir[0] != '\0') {
                std::snprintf(rl::backend::gMarkPath, MAX_PATH,
                              "%s\\RenderLift.entry", rl::backend::gModuleDir);
            }
        }
    } else if (reason == DLL_PROCESS_DETACH) {
        rl::backend::ModuleState* s = rl::backend::gState;
        if (s != nullptr && s->installed) (void)rl::backend::uninstallLocked(*s);
    }
    return TRUE;
}
