// ─────────────────────────────────────────────────────────────────────────────
// RenderLift — D3D11 module (Windows-only translation unit). RESEARCH LAYER.
//
// Current integration mode: OBSERVE (ADR 0003 — never alter rendering before
// being able to observe it). Every detour below is a measured passthrough:
// it records what the game does into an RLCAP1 capture log and returns
// control to the original function untouched.
//
// Observed surface (v3.6 — 16 hooks, still measured passthrough only):
//   ID3D11Device:        CreateTexture2D(5) · CreateRenderTargetView(9)
//                        CreateDepthStencilView(10)
//   ID3D11DeviceContext: DrawIndexed(12) · Draw(13) · DrawIndexedInstanced(20)
//                        DrawInstanced(21) · OMSetRenderTargets(33)
//                        DrawAuto(38) · DrawIndexedInstancedIndirect(39)
//                        DrawInstancedIndirect(40) · RSSetViewports(44)
//   IDXGISwapChain:      Present(8) · ResizeBuffers(13)
//   REAL-path (v3.6, read dynamically from the game's real ctx vtable at
//   first Present — never hardcoded): Draw(13) · DrawIndexed(12) — 2 hooks,
//   own rd=/rdi= counters + `first REAL slot=`/`realhook target=` lines.
//
// v3.4 draw-path evidence (OBSERVE-only — zero visual/rendering change):
//   per-frame `RLCAP1 draws …` per-slot counters, first-fire lines
//   (`RLCAP1 first slot=<name> ctx=…`), distinct-context tracking
//   (`RLCAP1 context first=/new=…`), a window summary at the cap
//   (`RLCAP1 summary … verdict=…`), and a re-arm path: calling Install
//   again while installed reopens a fresh observation window.
// The legacy `drawstat frame=N calls= maxidx= maxvtx=` and `cap frames=`
// lines are untouched (wire format and consumers preserved).
//
// v3.5 real-object discovery (v3.4 result: 14 armed hooks, 2000 Presents,
// zero context calls — are our probe-derived anchors even the game's code?):
//   one-time, SEH-guarded walk real swapchain → GetDevice(IID_ID3D11Device)
//   → GetImmediateContext → GetType/feature-level → real vtables; then a
//   word-for-word PROBE × REAL address compare (`RLCAP1 probe/real/cmp …`
//   lines, verdict PROBE_EQ_REAL | PROBE_NE_REAL | NO_ID3D11DEVICE |
//   SEH_FAIL). Installs NOTHING at this stage. Focus is logged as a measured
//   variable, never as an assumed cause.
//
// v3.5 RESULT (GTA V Legacy 1.0.3889.0, sealed): verdict=PROBE_NE_REAL,
// diffs=8/15 — the game's real immediate context is a RAGE heap-resident
// class whose Draw(13)/DrawIndexed(12) (+ the 5 other draw-family slots)
// point at DIFFERENT code than our probe vtable, while OMSetRenderTargets/
// RSSetViewports/ExecuteCommandList match. PROVEN: the difference exists.
// NOT proven: that it CAUSES DRAWPATH_ZERO — Present is the standing
// counter-example (its REAL entry also differs from the probe's, yet the
// probe hook captured 2000/2000 frames — whoever calls Present resolves the
// entry on the REAL instance, and the game's documented call pattern makes
// DIFFERENT non-decisive in general).
//
// v3.6 real-draw-path microtest (16 hooks total = 14 PROBE + 2 REAL):
//   from the first-Present discovery, read the REAL context vtable slots
//   12 (DrawIndexed) and 13 (Draw) DYNAMICALLY — never hardcode addresses —
//   and install exactly 2 new hooks through the existing IHookEngine
//   (create ×2 + enableAll). Probe and real paths stay strictly separated:
//   own counters (realDraw/realDrawIndexed → rd=/rdi=), own first-fire
//   lines (`RLCAP1 first REAL slot=…`), own install lines
//   (`RLCAP1 realhook target=…`). Plus: module-owner attribution of the 4
//   draw addresses (GetModuleHandleEx FROM_ADDRESS|UNCHANGED_REFCOUNT —
//   cheap, safe, off hot path) and focus telemetry v2 (W: foreground HWND
//   == swapchain OutputWindow from GetDesc; P: foreground PID == our PID —
//   measurement only, never a gate).
//
// Output: "RenderLift.D3D11.log" (RLCAP1 lines, next to this DLL by default)
//   — inspect offline with:
//   RenderLift.CLI inspect RenderLift.D3D11.log
// Entry witness: "RenderLift.entry" (32-byte binary mark, CREATE_ALWAYS at
//   the top of RenderLiftInstall, kernel32-only — see the v3.1 evidence
//   transport block below).
//
// Env overrides: RENDERLIFT_LOG (path) · RENDERLIFT_OBSERVE_FRAMES (cap,
// default 2000; read from the TARGET process environment — set it before
// the game launches, or leave the default for the official lab window).
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
constexpr std::size_t DrawIndexedInstanced = 20;
constexpr std::size_t DrawInstanced = 21;
constexpr std::size_t OMSetRenderTargets = 33;
constexpr std::size_t DrawAuto = 38;
constexpr std::size_t DrawIndexedInstancedIndirect = 39;
constexpr std::size_t DrawInstancedIndirect = 40;
constexpr std::size_t RSSetViewports = 44;
// Context execution surfaces — v3.5 diagnostics only (address compare, NO hooks)
constexpr std::size_t ExecuteCommandList = 58;
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
using DrawIndexedInstancedFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT,
                                                        UINT, INT, UINT);
using DrawInstancedFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT, UINT,
                                                 UINT);
using DrawAutoFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*);
using DrawIndexedInstancedIndirectFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,
                                                                ID3D11Buffer*, UINT);
using DrawInstancedIndirectFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,
                                                         ID3D11Buffer*, UINT);
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

// Probe-vtable capture (slot function addresses + object identity). Defined
// BEFORE ModuleState — v3.5 stores a snapshot member for the PROBE × REAL
// address comparison (`s.probe`).
struct Vtables {
    void* present = nullptr;
    void* resizeBuffers = nullptr;
    void* createTexture2D = nullptr;
    void* createRtv = nullptr;
    void* createDsv = nullptr;
    void* drawIndexed = nullptr;
    void* draw = nullptr;
    void* drawIndexedInstanced = nullptr;
    void* drawInstanced = nullptr;
    void* drawAuto = nullptr;
    void* drawIndexedInstancedIndirect = nullptr;
    void* drawInstancedIndirect = nullptr;
    void* omSetRenderTargets = nullptr;
    void* rsSetViewports = nullptr;
    // v3.5: probe-object identity (addresses stay valid as log evidence even
    // after the probe objects are released)
    void* executeCommandList = nullptr;  // probe ctx slot 58 (diagnostic only)
    void* probeDevice = nullptr;
    void* probeDevVtable = nullptr;
    void* probeCtx = nullptr;
    void* probeCtxVtable = nullptr;
};

// v3.4: per-slot counters for the draw/dispatch surface. `frameSlots` is
// reset on every Present (feeds the per-frame `RLCAP1 draws …` line);
// `windowSlots` accumulates across the observation window (feeds the
// `RLCAP1 summary …` line written when the frame cap is reached).
struct SlotCounters {
    std::uint64_t draw = 0;                       // Draw(13)
    std::uint64_t drawIndexed = 0;                // DrawIndexed(12)
    std::uint64_t drawIndexedInstanced = 0;       // DrawIndexedInstanced(20)
    std::uint64_t drawInstanced = 0;              // DrawInstanced(21)
    std::uint64_t drawAuto = 0;                   // DrawAuto(38)
    std::uint64_t drawIndexedInstancedIndirect = 0;  // …InstancedIndirect(39)
    std::uint64_t drawInstancedIndirect = 0;      // DrawInstancedIndirect(40)
    std::uint64_t omSetRenderTargets = 0;         // OMSetRenderTargets(33)
    std::uint64_t rsSetViewports = 0;             // RSSetViewports(44)
    // v3.6: REAL-address hooks (installed on the game's own context vtable
    // entries — PROBE path left untouched, never mixed):
    std::uint64_t realDraw = 0;                   // REAL Draw(13)
    std::uint64_t realDrawIndexed = 0;            // REAL DrawIndexed(12)

    void add(const SlotCounters& o) {
        draw += o.draw;
        drawIndexed += o.drawIndexed;
        drawIndexedInstanced += o.drawIndexedInstanced;
        drawInstanced += o.drawInstanced;
        drawAuto += o.drawAuto;
        drawIndexedInstancedIndirect += o.drawIndexedInstancedIndirect;
        drawInstancedIndirect += o.drawInstancedIndirect;
        omSetRenderTargets += o.omSetRenderTargets;
        rsSetViewports += o.rsSetViewports;
        realDraw += o.realDraw;
        realDrawIndexed += o.realDrawIndexed;
    }

    std::uint64_t drawsTotal() const {
        return draw + drawIndexed + drawIndexedInstanced + drawInstanced + drawAuto +
               drawIndexedInstancedIndirect + drawInstancedIndirect;
    }

    std::uint64_t realDrawsTotal() const { return realDraw + realDrawIndexed; }
};

// Distinct contexts tracked per window (enough for immediate + deferred sets).
constexpr std::uint32_t kMaxTrackedContexts = 32;

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
    DrawIndexedInstancedFn originalDrawIndexedInstanced = nullptr;
    DrawInstancedFn originalDrawInstanced = nullptr;
    DrawAutoFn originalDrawAuto = nullptr;
    DrawIndexedInstancedIndirectFn originalDrawIndexedInstancedIndirect = nullptr;
    DrawInstancedIndirectFn originalDrawInstancedIndirect = nullptr;
    OMSetRenderTargetsFn originalOMSetRenderTargets = nullptr;
    RSSetViewportsFn originalRSSetViewports = nullptr;
    // v3.6: trampolines for the 2 REAL-address hooks (filled at first
    // Present; completely separate from the PROBE originals above):
    DrawIndexedFn originalRealDrawIndexed = nullptr;
    DrawFn originalRealDraw = nullptr;

    bool installed = false;
    std::atomic<bool> observing{false};  // read on the hot path without the mutex
    std::uint64_t frame = 0;
    std::uint32_t observationCap = 2000;  // frames; 0 = unlimited
    std::uint64_t presentCount = 0;
    FrameDrawCounters draw;

    // v3.4 window diagnostics (reset on re-arm, all under mutex):
    SlotCounters frameSlots;
    SlotCounters windowSlots;
    std::uint32_t firstLoggedMask = 0;  // one "first slot=" line per bit
    std::uint64_t seenCtxs[kMaxTrackedContexts]{};
    std::uint32_t seenCtxCount = 0;     // distinct contexts observed this window
    std::uint32_t seenCtxOverflow = 0;  // distinct contexts beyond tracking capacity

    // v3.5 real-object discovery (one-time per module load; NOT re-armed —
    // object identities/method addresses do not change between windows):
    Vtables probe;            // snapshot of everything resolveVtables captured
    bool realDone = false;    // real swapchain→device→context dump happened
    // v3.6:
    bool realHooksInstalled = false;  // the 2 REAL hooks armed (one-time)
    HWND scOutHwnd = nullptr;         // real swapchain OutputWindow (GetDesc)
    bool focusInit = false;  // initial focus state line written
    bool focusW = false;     // W: last fg HWND == scOutHwnd
    bool focusP = false;     // P: last fg PID == our PID
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
    return 2000;  // v3.4 main lab window (6 s → 33+ s of gameplay, fps-dependent)
}

// ── Vtable bootstrap ────────────────────────────────────────────────────────

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
        out.drawIndexedInstanced = ctx.functionAt(slot::DrawIndexedInstanced);
        out.drawInstanced = ctx.functionAt(slot::DrawInstanced);
        out.drawAuto = ctx.functionAt(slot::DrawAuto);
        out.drawIndexedInstancedIndirect = ctx.functionAt(slot::DrawIndexedInstancedIndirect);
        out.drawInstancedIndirect = ctx.functionAt(slot::DrawInstancedIndirect);
        out.omSetRenderTargets = ctx.functionAt(slot::OMSetRenderTargets);
        out.rsSetViewports = ctx.functionAt(slot::RSSetViewports);
        out.executeCommandList = ctx.functionAt(slot::ExecuteCommandList);
        out.probeDevice = device;
        out.probeDevVtable = *reinterpret_cast<void* const*>(device);
        out.probeCtx = context;
        out.probeCtxVtable = *reinterpret_cast<void* const*>(context);
        ok = out.present && out.resizeBuffers && out.createTexture2D && out.createRtv &&
             out.omSetRenderTargets && out.rsSetViewports;
        earlyLogf("RLCAP1 vtables present=%p draw=%p diinst=%p dauto=%p om=%p ok=%d",
                  out.present, out.draw, out.drawIndexedInstanced, out.drawAuto,
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

// ── v3.4 first-fire / distinct-context evidence (call sites hold s.mutex) ───

enum FirstSlotBit : std::uint32_t {
    kFirstBit_DrawIndexed = 1u << 0,
    kFirstBit_Draw = 1u << 1,
    kFirstBit_DrawIndexedInstanced = 1u << 2,
    kFirstBit_DrawInstanced = 1u << 3,
    kFirstBit_DrawAuto = 1u << 4,
    kFirstBit_DrawIndexedInstancedIndirect = 1u << 5,
    kFirstBit_DrawInstancedIndirect = 1u << 6,
    kFirstBit_OMSetRenderTargets = 1u << 7,
    kFirstBit_RSSetViewports = 1u << 8,
    kFirstBit_RealDraw = 1u << 9,
    kFirstBit_RealDrawIndexed = 1u << 10,
};

void noteFirstSlot(ModuleState& s, std::uint32_t bit, const char* name, const void* ctx,
                   bool realTag) {
    if ((s.firstLoggedMask & bit) != 0) return;
    s.firstLoggedMask |= bit;
    char line[128];
    std::snprintf(line, sizeof line,
                  realTag ? "RLCAP1 first REAL slot=%s ctx=0x%llx"
                          : "RLCAP1 first slot=%s ctx=0x%llx",
                  name,
                  static_cast<unsigned long long>(
                      reinterpret_cast<std::uintptr_t>(ctx)));
    s.log.writeRaw(line);
}

// Bounded (32-slot) distinct-context set. The first context ever seen gets a
// `context first=` line; every later distinct one gets `context new=`. Beyond
// the capacity we keep counting (seenCtxOverflow) without logging per call —
// the window summary carries the totals.
void noteContext(ModuleState& s, const void* ctx) {
    const unsigned long long key =
        static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(ctx));
    const std::uint32_t stored =
        (s.seenCtxCount < kMaxTrackedContexts) ? s.seenCtxCount : kMaxTrackedContexts;
    for (std::uint32_t i = 0; i < stored; ++i) {
        if (s.seenCtxs[i] == key) return;  // already known
    }
    if (s.seenCtxCount < kMaxTrackedContexts) {
        char line[96];
        std::snprintf(line, sizeof line,
                      s.seenCtxCount == 0 ? "RLCAP1 context first=0x%llx"
                                          : "RLCAP1 context new=0x%llx",
                      key);
        s.log.writeRaw(line);
        s.seenCtxs[s.seenCtxCount] = key;
    } else {
        ++s.seenCtxOverflow;
    }
    ++s.seenCtxCount;
}

// Draw* are the hottest hooks in the game — counting is all they may do.
void STDMETHODCALLTYPE DrawIndexed_Hook(ID3D11DeviceContext* ctx, UINT indexCount,
                                        UINT startIndexLocation, INT baseVertexLocation) {
    ModuleState& s = state();
    if (observing()) {
        std::lock_guard<std::mutex> lock(s.mutex);
        ++s.draw.calls;
        if (indexCount > s.draw.maxIndices) s.draw.maxIndices = indexCount;
        ++s.frameSlots.drawIndexed;
        noteContext(s, ctx);
        noteFirstSlot(s, kFirstBit_DrawIndexed, "DrawIndexed", ctx, /*realTag=*/false);
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
        ++s.frameSlots.draw;
        noteContext(s, ctx);
        noteFirstSlot(s, kFirstBit_Draw, "Draw", ctx, /*realTag=*/false);
    }
    s.originalDraw(ctx, vertexCount, startVertexLocation);
}

void STDMETHODCALLTYPE DrawIndexedInstanced_Hook(ID3D11DeviceContext* ctx,
                                                 UINT indexCountPerInstance,
                                                 UINT instanceCount,
                                                 UINT startIndexLocation,
                                                 INT baseVertexLocation,
                                                 UINT startInstanceLocation) {
    ModuleState& s = state();
    if (observing()) {
        std::lock_guard<std::mutex> lock(s.mutex);
        ++s.frameSlots.drawIndexedInstanced;
        noteContext(s, ctx);
        noteFirstSlot(s, kFirstBit_DrawIndexedInstanced, "DrawIndexedInstanced", ctx, /*realTag=*/false);
    }
    s.originalDrawIndexedInstanced(ctx, indexCountPerInstance, instanceCount,
                                   startIndexLocation, baseVertexLocation,
                                   startInstanceLocation);
}

void STDMETHODCALLTYPE DrawInstanced_Hook(ID3D11DeviceContext* ctx,
                                          UINT vertexCountPerInstance, UINT instanceCount,
                                          UINT startVertexLocation,
                                          UINT startInstanceLocation) {
    ModuleState& s = state();
    if (observing()) {
        std::lock_guard<std::mutex> lock(s.mutex);
        ++s.frameSlots.drawInstanced;
        noteContext(s, ctx);
        noteFirstSlot(s, kFirstBit_DrawInstanced, "DrawInstanced", ctx, /*realTag=*/false);
    }
    s.originalDrawInstanced(ctx, vertexCountPerInstance, instanceCount,
                            startVertexLocation, startInstanceLocation);
}

void STDMETHODCALLTYPE DrawAuto_Hook(ID3D11DeviceContext* ctx) {
    ModuleState& s = state();
    if (observing()) {
        std::lock_guard<std::mutex> lock(s.mutex);
        ++s.frameSlots.drawAuto;
        noteContext(s, ctx);
        noteFirstSlot(s, kFirstBit_DrawAuto, "DrawAuto", ctx, /*realTag=*/false);
    }
    s.originalDrawAuto(ctx);
}

void STDMETHODCALLTYPE DrawIndexedInstancedIndirect_Hook(
    ID3D11DeviceContext* ctx, ID3D11Buffer* pBufferForArgs, UINT alignedByteOffsetForArgs) {
    ModuleState& s = state();
    if (observing()) {
        std::lock_guard<std::mutex> lock(s.mutex);
        ++s.frameSlots.drawIndexedInstancedIndirect;
        noteContext(s, ctx);
        noteFirstSlot(s, kFirstBit_DrawIndexedInstancedIndirect,
                      "DrawIndexedInstancedIndirect", ctx, /*realTag=*/false);
    }
    s.originalDrawIndexedInstancedIndirect(ctx, pBufferForArgs, alignedByteOffsetForArgs);
}

void STDMETHODCALLTYPE DrawInstancedIndirect_Hook(ID3D11DeviceContext* ctx,
                                                  ID3D11Buffer* pBufferForArgs,
                                                  UINT alignedByteOffsetForArgs) {
    ModuleState& s = state();
    if (observing()) {
        std::lock_guard<std::mutex> lock(s.mutex);
        ++s.frameSlots.drawInstancedIndirect;
        noteContext(s, ctx);
        noteFirstSlot(s, kFirstBit_DrawInstancedIndirect, "DrawInstancedIndirect", ctx, /*realTag=*/false);
    }
    s.originalDrawInstancedIndirect(ctx, pBufferForArgs, alignedByteOffsetForArgs);
}

void STDMETHODCALLTYPE OMSetRenderTargets_Hook(ID3D11DeviceContext* ctx, UINT numViews,
                                               ID3D11RenderTargetView* const* ppRtv,
                                               ID3D11DepthStencilView* pDsv) {
    ModuleState& s = state();
    if (observing()) {
        std::lock_guard<std::mutex> lock(s.mutex);
        ++s.frameSlots.omSetRenderTargets;
        noteContext(s, ctx);
        noteFirstSlot(s, kFirstBit_OMSetRenderTargets, "OMSetRenderTargets", ctx, /*realTag=*/false);
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
    if (observing()) {
        std::lock_guard<std::mutex> lock(s.mutex);
        ++s.frameSlots.rsSetViewports;
        noteContext(s, ctx);
        noteFirstSlot(s, kFirstBit_RSSetViewports, "RSSetViewports", ctx, /*realTag=*/false);
        if (pViewports != nullptr && numViewports > 0) {
            obs::ViewportEvent e{};
            e.context = reinterpret_cast<std::uint64_t>(ctx);
            e.count = numViewports;
            e.x = pViewports[0].TopLeftX;
            e.y = pViewports[0].TopLeftY;
            e.w = pViewports[0].Width;
            e.h = pViewports[0].Height;
            s.log.write(e);
        }
    }
    s.originalRSSetViewports(ctx, numViewports, pViewports);
}

// ── v3.6 REAL-path detours (microtest) ─────────────────────────────────────
// Targets: the code the GAME'S OWN immediate-context vtable points at, slots
// 12/13, read dynamically at first Present (v3.5 proved they differ from the
// probe vtable's). Strictly separate evidence: real_* counters +
// `first REAL slot=` lines — probe counters are never touched here.
void STDMETHODCALLTYPE RealDrawIndexed_Hook(ID3D11DeviceContext* ctx, UINT indexCount,
                                            UINT startIndexLocation,
                                            INT baseVertexLocation) {
    ModuleState& s = state();
    if (observing()) {
        std::lock_guard<std::mutex> lock(s.mutex);
        ++s.frameSlots.realDrawIndexed;
        noteContext(s, ctx);
        noteFirstSlot(s, kFirstBit_RealDrawIndexed, "DrawIndexed", ctx, /*realTag=*/true);
    }
    s.originalRealDrawIndexed(ctx, indexCount, startIndexLocation, baseVertexLocation);
}

void STDMETHODCALLTYPE RealDraw_Hook(ID3D11DeviceContext* ctx, UINT vertexCount,
                                     UINT startVertexLocation) {
    ModuleState& s = state();
    if (observing()) {
        std::lock_guard<std::mutex> lock(s.mutex);
        ++s.frameSlots.realDraw;
        noteContext(s, ctx);
        noteFirstSlot(s, kFirstBit_RealDraw, "Draw", ctx, /*realTag=*/true);
    }
    s.originalRealDraw(ctx, vertexCount, startVertexLocation);
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

// ── v3.5 real-object discovery (H2 investigation — diagnostics, NO hooks) ───
//
// PROBLEM the v3.4 run posed: 14 probe-anchored hooks armed, Present fired
// 2000 times, yet every context counter stayed at zero with zero contexts
// observed. Two competing explanations: (H2) the method addresses our probe
// vtable yielded are NOT the functions the game's real device/context use;
// (H3) the game submits through deferred contexts / command lists so the
// hooked immediate-context functions never run. This block answers H2 with
// raw addresses and nothing else: on the game's real swapchain (the pointer
// arriving at our Present hook — PROVEN real), walk
//   swapchain → GetDevice(IID_ID3D11Device) → GetImmediateContext →
//   GetType/feature-level → real vtables → slot addresses,
// then compare each address with the probe-derived value word for word.
// Reference pattern (external): gta5-extended-video-export walks the same
// real swapchain → GetDevice → GetImmediateContext chain from its Present.

// All-POD result bag (trivially copyable: legal inside an SEH frame).
struct RealProbeResult {
    HRESULT deviceHr = E_FAIL;   // GetDevice(IID_ID3D11Device) result
    const void* dxgiDevice = nullptr;  // IID_IDXGIDevice fallback identity
    const void* device = nullptr;
    const void* devVtable = nullptr;
    unsigned long featureLevel = 0;
    const void* context = nullptr;
    const void* ctxVtable = nullptr;
    unsigned long ctxType = 0xFFFFFFFFul;  // D3D11_DEVICE_CONTEXT_TYPE
    // Real slot addresses (nullptr when not reachable):
    const void* scPresent = nullptr;
    const void* scResize = nullptr;
    const void* devTex2D = nullptr;
    const void* devRtv = nullptr;
    const void* devDsv = nullptr;
    const void* ctxDrawIndexed = nullptr;
    const void* ctxDraw = nullptr;
    const void* ctxDrawIndexedInstanced = nullptr;
    const void* ctxDrawInstanced = nullptr;
    const void* ctxDrawAuto = nullptr;
    const void* ctxDrawIndexedInstancedIndirect = nullptr;
    const void* ctxDrawInstancedIndirect = nullptr;
    const void* ctxOMSetRenderTargets = nullptr;
    const void* ctxRSSetViewports = nullptr;
    const void* ctxExecuteCommandList = nullptr;
    HWND scOutHwnd = nullptr;  // v3.6: swapchain OutputWindow (GetDesc) — focus W metric
    bool sehHit = false;
};

// SEH filter on the k32 transport (same discipline as sehFilter): a fault in
// OUR diag calls must never be able to crash the game's render thread. The
// exception is recorded with code + address and the discovery reports
// verdict=SEH_FAIL — nothing is masked: the failure itself becomes evidence.
LONG realDiscoverSeh(EXCEPTION_POINTERS* ep) {
    const DWORD code =
        (ep != nullptr && ep->ExceptionRecord != nullptr) ? ep->ExceptionRecord->ExceptionCode : 0;
    const void* addr = (ep != nullptr && ep->ExceptionRecord != nullptr)
                           ? ep->ExceptionRecord->ExceptionAddress
                           : nullptr;
    earlyLogf("RLCAP1 discover seh code=0x%08lx addr=%p", static_cast<unsigned long>(code),
              addr);
    return EXCEPTION_EXECUTE_HANDLER;
}

RealProbeResult runRealDiscovery(IDXGISwapChain* swapchain) {
    RealProbeResult r;
    __try {
        // v3.6: capture the REAL output window for the focus W metric —
        // GetDesc on the game's own swapchain, POD struct, SEH-guarded.
        DXGI_SWAP_CHAIN_DESC scDesc{};
        if (SUCCEEDED(swapchain->GetDesc(&scDesc))) r.scOutHwnd = scDesc.OutputWindow;

        ID3D11Device* dev = nullptr;
        r.deviceHr =
            swapchain->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void**>(&dev));
        if (FAILED(r.deviceHr) || dev == nullptr) {
            // No ID3D11Device behind the real swapchain — itself decisive
            // (p.ex. DX10/DX10.1 game mode). Still identify the DXGI device.
            IDXGIDevice* dxgi = nullptr;
            if (SUCCEEDED(swapchain->GetDevice(__uuidof(IDXGIDevice),
                                               reinterpret_cast<void**>(&dxgi))) &&
                dxgi != nullptr) {
                r.dxgiDevice = dxgi;
                dxgi->Release();
            }
            return r;
        }
        r.device = dev;
        r.featureLevel = static_cast<unsigned long>(dev->GetFeatureLevel());
        void* const* devVt = *reinterpret_cast<void* const**>(dev);
        r.devVtable = static_cast<const void*>(devVt);
        r.devTex2D = devVt[slot::CreateTexture2D];
        r.devRtv = devVt[slot::CreateRenderTargetView];
        r.devDsv = devVt[slot::CreateDepthStencilView];

        void* const* scVt = *reinterpret_cast<void* const**>(swapchain);
        r.scPresent = scVt[slot::Present];
        r.scResize = scVt[slot::ResizeBuffers];

        ID3D11DeviceContext* ctx = nullptr;
        dev->GetImmediateContext(&ctx);  // always populated for a real device
        if (ctx != nullptr) {
            r.context = ctx;
            r.ctxType = static_cast<unsigned long>(ctx->GetType());
            void* const* ctxVt = *reinterpret_cast<void* const**>(ctx);
            r.ctxVtable = static_cast<const void*>(ctxVt);
            r.ctxDrawIndexed = ctxVt[slot::DrawIndexed];
            r.ctxDraw = ctxVt[slot::Draw];
            r.ctxDrawIndexedInstanced = ctxVt[slot::DrawIndexedInstanced];
            r.ctxDrawInstanced = ctxVt[slot::DrawInstanced];
            r.ctxDrawAuto = ctxVt[slot::DrawAuto];
            r.ctxDrawIndexedInstancedIndirect = ctxVt[slot::DrawIndexedInstancedIndirect];
            r.ctxDrawInstancedIndirect = ctxVt[slot::DrawInstancedIndirect];
            r.ctxOMSetRenderTargets = ctxVt[slot::OMSetRenderTargets];
            r.ctxRSSetViewports = ctxVt[slot::RSSetViewports];
            r.ctxExecuteCommandList = ctxVt[slot::ExecuteCommandList];
            ctx->Release();
        }
        dev->Release();
    } __except (realDiscoverSeh(GetExceptionInformation())) {
        r.sehHit = true;
        r.deviceHr = E_UNEXPECTED;
    }
    return r;
}

bool cmpRow(ModuleState& s, const char* name, std::size_t slotIdx, const void* probeFn,
            const void* realFn) {
    const bool same =
        (probeFn != nullptr) && (probeFn == realFn);
    char line[224];
    std::snprintf(line, sizeof line, "RLCAP1 cmp name=%s slot=%zu probe=0x%llx real=0x%llx %s",
                  name, slotIdx,
                  static_cast<unsigned long long>(
                      reinterpret_cast<std::uintptr_t>(probeFn)),
                  static_cast<unsigned long long>(
                      reinterpret_cast<std::uintptr_t>(realFn)),
                  same ? "SAME" : "DIFFERENT");
    s.log.writeRaw(line);
    return same;
}

// v3.6: cheap/safe module attribution for a code address (approved route —
// FROM_ADDRESS | UNCHANGED_REFCOUNT: lookup only, no refcount bump, no
// module load; basename only). Runs once per discovery, never on hot paths.
void moduleOf(const void* address, char* out, std::size_t outSize) {
    HMODULE mod = nullptr;
    out[0] = '\0';
    if (address != nullptr &&
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCSTR>(address), &mod) &&
        mod != nullptr) {
        char path[MAX_PATH];
        if (GetModuleFileNameA(mod, path, MAX_PATH) != 0) {
            const char* base = path;
            for (const char* p = path; *p != '\0'; ++p) {
                if (*p == '\\' || *p == '/') base = p + 1;
            }
            std::snprintf(out, outSize, "%s", base);
        }
    }
    if (out[0] == '\0') std::snprintf(out, outSize, "%s", "?nomodule");
}

// Formats the whole discovery report (caller holds s.mutex). ~24 lines once.
void logRealDiscovery(ModuleState& s, IDXGISwapChain* swapchain, const RealProbeResult& r) {
    char line[288];
    s.scOutHwnd = r.scOutHwnd;  // focus W metric baseline (nullptr on SEH)
    if (r.sehHit) {
        s.log.writeRaw("RLCAP1 real verdict=SEH_FAIL (see 'RLCAP1 discover seh' above)");
        return;
    }
    if (r.device == nullptr) {
        std::snprintf(line, sizeof line,
                      "RLCAP1 real swap=0x%llx hr=0x%08lx dxgi_device=0x%llx",
                      static_cast<unsigned long long>(
                          reinterpret_cast<std::uintptr_t>(swapchain)),
                      static_cast<unsigned long>(r.deviceHr),
                      static_cast<unsigned long long>(
                          reinterpret_cast<std::uintptr_t>(r.dxgiDevice)));
        s.log.writeRaw(line);
        s.log.writeRaw(
            "RLCAP1 real verdict=NO_ID3D11DEVICE (real swapchain has no D3D11 device —"
            " H2 non-D3D11 rendering mode, p.ex. DX10/DX10.1)");
        return;
    }
    std::snprintf(line, sizeof line,
                  "RLCAP1 real swap=0x%llx device=0x%llx devvt=0x%llx context=0x%llx"
                  " ctxvt=0x%llx type=%s(%lu) fl=0x%lx",
                  static_cast<unsigned long long>(
                      reinterpret_cast<std::uintptr_t>(swapchain)),
                  static_cast<unsigned long long>(
                      reinterpret_cast<std::uintptr_t>(r.device)),
                  static_cast<unsigned long long>(
                      reinterpret_cast<std::uintptr_t>(r.devVtable)),
                  static_cast<unsigned long long>(
                      reinterpret_cast<std::uintptr_t>(r.context)),
                  static_cast<unsigned long long>(
                      reinterpret_cast<std::uintptr_t>(r.ctxVtable)),
                  r.ctxType == 0 ? "IMMEDIATE" : (r.ctxType == 1 ? "DEFERRED" : "?TYPE"),
                  r.ctxType, r.featureLevel);
    s.log.writeRaw(line);

    const Vtables& p = s.probe;
    std::uint32_t rows = 0;
    std::uint32_t diffs = 0;
    const bool noCtx = (r.context == nullptr);
    rows += 1; diffs += cmpRow(s, "Present", slot::Present, p.present, r.scPresent) ? 0u : 1u;
    rows += 1; diffs += cmpRow(s, "ResizeBuffers", slot::ResizeBuffers, p.resizeBuffers, r.scResize) ? 0u : 1u;
    rows += 1; diffs += cmpRow(s, "CreateTexture2D", slot::CreateTexture2D, p.createTexture2D, r.devTex2D) ? 0u : 1u;
    rows += 1; diffs += cmpRow(s, "CreateRenderTargetView", slot::CreateRenderTargetView, p.createRtv, r.devRtv) ? 0u : 1u;
    rows += 1; diffs += cmpRow(s, "CreateDepthStencilView", slot::CreateDepthStencilView, p.createDsv, r.devDsv) ? 0u : 1u;
    if (!noCtx) {
        rows += 1; diffs += cmpRow(s, "DrawIndexed", slot::DrawIndexed, p.drawIndexed, r.ctxDrawIndexed) ? 0u : 1u;
        rows += 1; diffs += cmpRow(s, "Draw", slot::Draw, p.draw, r.ctxDraw) ? 0u : 1u;
        rows += 1; diffs += cmpRow(s, "DrawIndexedInstanced", slot::DrawIndexedInstanced, p.drawIndexedInstanced, r.ctxDrawIndexedInstanced) ? 0u : 1u;
        rows += 1; diffs += cmpRow(s, "DrawInstanced", slot::DrawInstanced, p.drawInstanced, r.ctxDrawInstanced) ? 0u : 1u;
        rows += 1; diffs += cmpRow(s, "DrawAuto", slot::DrawAuto, p.drawAuto, r.ctxDrawAuto) ? 0u : 1u;
        rows += 1; diffs += cmpRow(s, "DrawIndexedInstancedIndirect", slot::DrawIndexedInstancedIndirect, p.drawIndexedInstancedIndirect, r.ctxDrawIndexedInstancedIndirect) ? 0u : 1u;
        rows += 1; diffs += cmpRow(s, "DrawInstancedIndirect", slot::DrawInstancedIndirect, p.drawInstancedIndirect, r.ctxDrawInstancedIndirect) ? 0u : 1u;
        rows += 1; diffs += cmpRow(s, "OMSetRenderTargets", slot::OMSetRenderTargets, p.omSetRenderTargets, r.ctxOMSetRenderTargets) ? 0u : 1u;
        rows += 1; diffs += cmpRow(s, "RSSetViewports", slot::RSSetViewports, p.rsSetViewports, r.ctxRSSetViewports) ? 0u : 1u;
        rows += 1; diffs += cmpRow(s, "ExecuteCommandList", slot::ExecuteCommandList, p.executeCommandList, r.ctxExecuteCommandList) ? 0u : 1u;
    } else {
        s.log.writeRaw("RLCAP1 real context=nullptr (GetImmediateContext empty)");
    }
    std::snprintf(line, sizeof line, "RLCAP1 real verdict=%s rows=%u diffs=%u",
                  diffs == 0 ? "PROBE_EQ_REAL" : "PROBE_NE_REAL",
                  static_cast<unsigned>(rows), static_cast<unsigned>(diffs));
    s.log.writeRaw(line);

    // v3.6 evidence addenda (same one-time block):
    std::snprintf(line, sizeof line, "RLCAP1 scwnd out=0x%llx",
                  static_cast<unsigned long long>(
                      reinterpret_cast<std::uintptr_t>(r.scOutHwnd)));
    s.log.writeRaw(line);
    if (!noCtx) {
        // Who owns the 4 differing draw addresses? (probe Draw/DrawIndexed
        // vs real Draw/DrawIndexed). Cheap lookup, off hot path.
        const struct OwnerRow {
            const char* name;
            const char* target;
            const void* fn;
        } owners[] = {
            {"Draw", "PROBE", p.draw},
            {"Draw", "REAL", r.ctxDraw},
            {"DrawIndexed", "PROBE", p.drawIndexed},
            {"DrawIndexed", "REAL", r.ctxDrawIndexed},
        };
        for (std::size_t i = 0; i < sizeof owners / sizeof owners[0]; ++i) {
            char mod[96];
            moduleOf(owners[i].fn, mod, sizeof mod);
            std::snprintf(line, sizeof line,
                          "RLCAP1 owner name=%s target=%s addr=0x%llx module=%s",
                          owners[i].name, owners[i].target,
                          static_cast<unsigned long long>(
                              reinterpret_cast<std::uintptr_t>(owners[i].fn)),
                          mod);
            s.log.writeRaw(line);
        }
    }
}

// v3.6: install exactly 2 hooks on the REAL Draw/DrawIndexed entry points
// (addresses come from runRealDiscovery — read from the game's real context
// vtable at runtime, NEVER hardcoded). Uses the existing engine; enableAll()
// is its only enable primitive — re-invoking it is idempotent for hooks that
// are already enabled (MinHook skips enabled entries under MH_ALL_HOOKS), so
// the 14 armed probe hooks are untouched. One-time (realHooksInstalled);
// re-arm reopens the counting window without any reinstall.
void installRealDrawHooks(ModuleState& s, const RealProbeResult& r) {
    if (s.realHooksInstalled || s.hooks == nullptr) return;
    if (r.sehHit || r.context == nullptr || r.ctxDraw == nullptr ||
        r.ctxDrawIndexed == nullptr) {
        s.log.writeRaw("RLCAP1 realhook skip=missing-real-addrs");
        return;
    }
    const struct RealInstall {
        const char* name;
        const void* target;
        void* detour;
        void** original;
    } installs[] = {
        {"Draw", r.ctxDraw, reinterpret_cast<void*>(&RealDraw_Hook),
         reinterpret_cast<void**>(&s.originalRealDraw)},
        {"DrawIndexed", r.ctxDrawIndexed, reinterpret_cast<void*>(&RealDrawIndexed_Hook),
         reinterpret_cast<void**>(&s.originalRealDrawIndexed)},
    };
    std::uint32_t ok = 0;
    for (std::size_t i = 0; i < 2; ++i) {
        const HookStatus st =
            s.hooks->create(const_cast<void*>(installs[i].target), installs[i].detour,
                            installs[i].original);
        char line[160];
        std::snprintf(line, sizeof line, "RLCAP1 realhook target=%s addr=0x%llx ok=%d",
                      installs[i].name,
                      static_cast<unsigned long long>(
                          reinterpret_cast<std::uintptr_t>(installs[i].target)),
                      succeeded(st) ? 1 : 0);
        s.log.writeRaw(line);
        if (succeeded(st)) ++ok;
    }
    if (ok == 2 && succeeded(s.hooks->enableAll())) {
        s.realHooksInstalled = true;
        s.log.writeRaw("RLCAP1 real verdict2=REALHOOKS_ARMED count=2 total=16");
        return;
    }
    // Failure is evidence, never masked: the run degrades to v3.5 behavior.
    s.log.writeRaw("RLCAP1 real verdict2=REALHOOKS_FAIL");
}

HRESULT STDMETHODCALLTYPE Present_Hook(IDXGISwapChain* swapchain, UINT syncInterval,
                                       UINT flags) {
    ModuleState& s = state();
    {
        std::lock_guard<std::mutex> lock(s.mutex);
        ++s.frame;
        ++s.presentCount;

        // v3.5 one-time: who REALLY renders — walk the game's own objects.
        // v3.6: the same first-Present block now also arms the 2 REAL hooks.
        if (!s.realDone) {
            s.realDone = true;
            const RealProbeResult r = runRealDiscovery(swapchain);
            logRealDiscovery(s, swapchain, r);
            installRealDrawHooks(s, r);
        }

        if (s.observing) {
            // Focus as a MEASURED experimental variable v2 (measurement
            // only — never a gate, never an assumed cause). Two distinct
            // components, reported separately so PID-vs-HWND disagreements
            // become visible instead of being conflated:
            //   W = foreground HWND == real swapchain OutputWindow (GetDesc)
            //   P = foreground PID == our process id (the v3.5 metric)
            {
                const HWND fg = GetForegroundWindow();
                DWORD fgPid = 0;
                if (fg != nullptr) GetWindowThreadProcessId(fg, &fgPid);
                const bool focusW = (fg != nullptr) && (fg == s.scOutHwnd);
                const bool focusP =
                    (fg != nullptr) && (fgPid == GetCurrentProcessId());
                if (!s.focusInit) {
                    s.focusInit = true;
                    s.focusW = focusW;
                    s.focusP = focusP;
                    char line[224];
                    std::snprintf(line, sizeof line,
                                  "RLCAP1 focus init W=%d P=%d frame=%llu sc=0x%llx"
                                  " fg=0x%llx fgpid=%lu pid=%lu",
                                  focusW ? 1 : 0, focusP ? 1 : 0,
                                  static_cast<unsigned long long>(s.frame),
                                  static_cast<unsigned long long>(
                                      reinterpret_cast<std::uintptr_t>(s.scOutHwnd)),
                                  static_cast<unsigned long long>(
                                      reinterpret_cast<std::uintptr_t>(fg)),
                                  static_cast<unsigned long>(fgPid),
                                  static_cast<unsigned long>(GetCurrentProcessId()));
                    s.log.writeRaw(line);
                } else if (focusW != s.focusW || focusP != s.focusP) {
                    char line[224];
                    std::snprintf(line, sizeof line,
                                  "RLCAP1 focus transition W=%d->%d P=%d->%d frame=%llu"
                                  " sc=0x%llx fg=0x%llx fgpid=%lu",
                                  s.focusW ? 1 : 0, focusW ? 1 : 0,
                                  s.focusP ? 1 : 0, focusP ? 1 : 0,
                                  static_cast<unsigned long long>(s.frame),
                                  static_cast<unsigned long long>(
                                      reinterpret_cast<std::uintptr_t>(s.scOutHwnd)),
                                  static_cast<unsigned long long>(
                                      reinterpret_cast<std::uintptr_t>(fg)),
                                  static_cast<unsigned long>(fgPid));
                    s.focusW = focusW;
                    s.focusP = focusP;
                    s.log.writeRaw(line);
                }
            }

            // Per-frame aggregate first, then the frame marker — legacy
            // `drawstat` wire line preserved verbatim (v3.4: MUST NOT change).
            obs::DrawStatEvent stat{};
            stat.frame = s.frame;
            stat.calls = s.draw.calls;
            stat.maxIndices = s.draw.maxIndices;
            stat.maxVertices = s.draw.maxVertices;
            s.log.write(stat);

            // v3.4: per-slot coverage diagnostic (raw line; the RLCAP1 parser
            // tolerates unknown tags, offline inspector just skips it).
            char line[384];
            // rd=/rdi= (v3.6) are the REAL-path counters — always separated
            // from the probe-path d=/di= above; never summed into draws=.
            std::snprintf(line, sizeof line,
                          "RLCAP1 draws frame=%llu d=%llu di=%llu diinst=%llu dinst=%llu"
                          " dauto=%llu diind=%llu dinstind=%llu om=%llu vp=%llu"
                          " rd=%llu rdi=%llu",
                          static_cast<unsigned long long>(s.frame),
                          static_cast<unsigned long long>(s.frameSlots.draw),
                          static_cast<unsigned long long>(s.frameSlots.drawIndexed),
                          static_cast<unsigned long long>(s.frameSlots.drawIndexedInstanced),
                          static_cast<unsigned long long>(s.frameSlots.drawInstanced),
                          static_cast<unsigned long long>(s.frameSlots.drawAuto),
                          static_cast<unsigned long long>(s.frameSlots.drawIndexedInstancedIndirect),
                          static_cast<unsigned long long>(s.frameSlots.drawInstancedIndirect),
                          static_cast<unsigned long long>(s.frameSlots.omSetRenderTargets),
                          static_cast<unsigned long long>(s.frameSlots.rsSetViewports),
                          static_cast<unsigned long long>(s.frameSlots.realDraw),
                          static_cast<unsigned long long>(s.frameSlots.realDrawIndexed));
            s.log.writeRaw(line);
            s.windowSlots.add(s.frameSlots);

            obs::PresentEvent e{};
            e.swapchain = reinterpret_cast<std::uint64_t>(swapchain);
            e.frame = s.frame;
            s.log.write(e);

            if (s.frame % 30 == 0) s.log.flush();

            if (s.observationCap > 0 && s.frame >= s.observationCap) {
                // Window summary BEFORE the legacy terminator — answers "por
                // onde o jogo passou" without reading 2000 per-frame lines.
                char sum[576];
                std::snprintf(sum, sizeof sum,
                              "RLCAP1 summary frames=%llu d=%llu di=%llu diinst=%llu"
                              " dinst=%llu dauto=%llu diind=%llu dinstind=%llu om=%llu"
                              " vp=%llu rd=%llu rdi=%llu draws=%llu rdraws=%llu"
                              " ctxs=%u ctxovf=%u verdict=%s rverdict=%s",
                              static_cast<unsigned long long>(s.frame),
                              static_cast<unsigned long long>(s.windowSlots.draw),
                              static_cast<unsigned long long>(s.windowSlots.drawIndexed),
                              static_cast<unsigned long long>(s.windowSlots.drawIndexedInstanced),
                              static_cast<unsigned long long>(s.windowSlots.drawInstanced),
                              static_cast<unsigned long long>(s.windowSlots.drawAuto),
                              static_cast<unsigned long long>(s.windowSlots.drawIndexedInstancedIndirect),
                              static_cast<unsigned long long>(s.windowSlots.drawInstancedIndirect),
                              static_cast<unsigned long long>(s.windowSlots.omSetRenderTargets),
                              static_cast<unsigned long long>(s.windowSlots.rsSetViewports),
                              static_cast<unsigned long long>(s.windowSlots.realDraw),
                              static_cast<unsigned long long>(s.windowSlots.realDrawIndexed),
                              static_cast<unsigned long long>(s.windowSlots.drawsTotal()),
                              static_cast<unsigned long long>(s.windowSlots.realDrawsTotal()),
                              s.seenCtxCount, s.seenCtxOverflow,
                              s.windowSlots.drawsTotal() > 0 ? "DRAWPATH_ACTIVE"
                                                             : "DRAWPATH_ZERO",
                              !s.realHooksInstalled ? "REALHOOKS_ABSENT"
                              : s.windowSlots.realDrawsTotal() > 0 ? "REALDRAW_ACTIVE"
                                                                   : "REALDRAW_ZERO");
                s.log.writeRaw(sum);
                s.log.writeRaw("RLCAP1 cap frames=" + std::to_string(s.frame));
                s.log.flush();
                s.observing = false;  // passthrough overhead drops to ~nothing
            }
        }
        s.draw = FrameDrawCounters{};
        s.frameSlots = SlotCounters{};
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
//   5 vtables  · 50..56 probe sub-steps · 60+i per-hook (14 → 60..73)
//   75 enableAll · 85 armed
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
    s.originalDrawIndexedInstanced = nullptr;
    s.originalDrawInstanced = nullptr;
    s.originalDrawAuto = nullptr;
    s.originalDrawIndexedInstancedIndirect = nullptr;
    s.originalDrawInstancedIndirect = nullptr;
    s.originalOMSetRenderTargets = nullptr;
    s.originalRSSetViewports = nullptr;
    s.originalRealDrawIndexed = nullptr;
    s.originalRealDraw = nullptr;
    s.realHooksInstalled = false;
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
    if (s.installed) {
        // ── v3.4 re-arm: hooks stay installed; open a fresh window ─────────
        // A second CreateRemoteThread(RenderLiftInstall) call is now the
        // supported way to start a NEW observation window without unloading
        // or reinjecting the DLL (loader unchanged; GTA stays running).
        std::lock_guard<std::mutex> lock(s.mutex);
        s.frame = 0;
        s.observationCap = observationCapFromEnv();
        s.draw = FrameDrawCounters{};
        s.frameSlots = SlotCounters{};
        s.windowSlots = SlotCounters{};
        s.firstLoggedMask = 0;
        s.seenCtxCount = 0;
        s.seenCtxOverflow = 0;
        for (std::uint32_t i = 0; i < kMaxTrackedContexts; ++i) s.seenCtxs[i] = 0;
        s.observing = true;
        char line[96];
        std::snprintf(line, sizeof line, "RLCAP1 rearm cap=%lu",
                      static_cast<unsigned long>(s.observationCap));
        s.log.writeRaw(line);
        s.log.flush();
        return S_OK;
    }
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
    s.probe = vt;  // v3.5: keep the probe identity for the PROBE×REAL compare
    {
        char pl[288];
        std::snprintf(pl, sizeof pl,
                      "RLCAP1 probe device=0x%llx devvt=0x%llx context=0x%llx ctxvt=0x%llx",
                      static_cast<unsigned long long>(
                          reinterpret_cast<std::uintptr_t>(vt.probeDevice)),
                      static_cast<unsigned long long>(
                          reinterpret_cast<std::uintptr_t>(vt.probeDevVtable)),
                      static_cast<unsigned long long>(
                          reinterpret_cast<std::uintptr_t>(vt.probeCtx)),
                      static_cast<unsigned long long>(
                          reinterpret_cast<std::uintptr_t>(vt.probeCtxVtable)));
        s.log.writeRaw(pl);
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
        {vt.drawIndexedInstanced, reinterpret_cast<void*>(&DrawIndexedInstanced_Hook),
         reinterpret_cast<void**>(&s.originalDrawIndexedInstanced)},
        {vt.drawInstanced, reinterpret_cast<void*>(&DrawInstanced_Hook),
         reinterpret_cast<void**>(&s.originalDrawInstanced)},
        {vt.drawAuto, reinterpret_cast<void*>(&DrawAuto_Hook),
         reinterpret_cast<void**>(&s.originalDrawAuto)},
        {vt.drawIndexedInstancedIndirect,
         reinterpret_cast<void*>(&DrawIndexedInstancedIndirect_Hook),
         reinterpret_cast<void**>(&s.originalDrawIndexedInstancedIndirect)},
        {vt.drawInstancedIndirect, reinterpret_cast<void*>(&DrawInstancedIndirect_Hook),
         reinterpret_cast<void**>(&s.originalDrawInstancedIndirect)},
        {vt.omSetRenderTargets, reinterpret_cast<void*>(&OMSetRenderTargets_Hook),
         reinterpret_cast<void**>(&s.originalOMSetRenderTargets)},
        {vt.rsSetViewports, reinterpret_cast<void*>(&RSSetViewports_Hook),
         reinterpret_cast<void**>(&s.originalRSSetViewports)},
    };
    std::size_t hooked = 0;
    for (std::size_t i = 0; i < sizeof installs / sizeof installs[0]; ++i) {
        setPhase(60 + static_cast<LONG>(i));
        if (installs[i].target == nullptr) continue;
        if (!succeeded(createHook(s, installs[i].target, installs[i].detour,
                                  installs[i].original))) {
            earlyLogf("RLCAP1 fail hook slot=%zu target=%p", i, installs[i].target);
            return RL_E_HOOK_CREATE_0 + static_cast<HRESULT>(i);
        }
        ++hooked;
    }

    setPhase(75);
    if (!succeeded(s.hooks->enableAll())) {
        earlyLogRaw("RLCAP1 fail enableAll");
        return RL_E_HOOK_ENABLE;
    }

    setPhase(85);
    s.observing = true;
    s.installed = true;
    earlyLogf("RLCAP1 armed hooks=%zu mode=observe", hooked);
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
