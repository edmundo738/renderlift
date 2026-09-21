// RenderLift — observation layer: capture wire format "RLCAP1".
//
// The Research Layer rule (ADR 0003): never alter rendering before we can
// observe it. Backends in "observe" integration mode write a line-based text
// log that is both human-readable and strictly parseable, so the frame
// inspector (RenderLift.CLI inspect) can run offline on any OS.
//
// Format v1 — one event per line, space-separated key=value tokens:
//
//   RLCAP1 frame=129
//   RLCAP1 tex dev=0x1f2a id=0x8a1f w=1366 h=768 fmt=28 bind=0x28 mips=1 array=1 samples=1
//   RLCAP1 rtv dev=0x1f2a id=0x77aa res=0x8a1f
//   RLCAP1 dsv dev=0x1f2a id=0x77bb res=0x8a20
//   RLCAP1 vp  ctx=0x30 n=1 x=0 y=0 w=1366 h=768
//   RLCAP1 rt  ctx=0x30 n=2 rtvs=0x77aa,0x77ac dsv=0x77bb
//   RLCAP1 draw ctx=0x30 indexed=1 count=12500
//   RLCAP1 present sc=0x9d40 frame=129
//
// Pointers are identity tokens only (never dereferenced offline). `fmt` is a
// DXGI_FORMAT number; `bind` is a hex bitmask of D3D11_BIND_FLAG bits.
// Version token RLCAP1 allows future evolution.
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

namespace rl::backend::obs {

inline constexpr std::string_view kCapturePrefix = "RLCAP1";
inline constexpr std::size_t kMaxRtvs = 8;  // D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT

struct FrameMarkerEvent {
    std::uint64_t frame = 0;
};

struct TextureCreatedEvent {
    std::uint64_t device = 0;
    std::uint64_t id = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t dxgiFormat = 0;
    std::uint32_t bindFlags = 0;
    std::uint32_t mipLevels = 0;
    std::uint32_t arraySize = 0;
    std::uint32_t samples = 1;
};

struct ViewCreatedEvent {
    std::uint64_t device = 0;
    std::uint64_t viewId = 0;      // the RTV/DSV pointer
    std::uint64_t resourceId = 0;  // texture it views
};

struct ViewportEvent {
    std::uint64_t context = 0;
    std::uint32_t count = 0;
    float x = 0, y = 0, w = 0, h = 0;  // first viewport (leading signal)
};

struct RenderTargetsEvent {
    std::uint64_t context = 0;
    std::uint32_t count = 0;
    std::array<std::uint64_t, kMaxRtvs> rtvs{};  // zeros beyond count
    std::uint64_t dsv = 0;
};

struct DrawEvent {
    std::uint64_t context = 0;
    bool indexed = false;
    std::uint32_t count = 0;  // index or vertex count
};

struct PresentEvent {
    std::uint64_t swapchain = 0;
    std::uint64_t frame = 0;
};

// Per-frame draw aggregate. A game issues thousands of draws per frame, so
// live backends MUST NOT log each one; they emit one drawstat per frame
// instead. DrawEvent stays for small, hand-crafted captures.
struct DrawStatEvent {
    std::uint64_t frame = 0;
    std::uint32_t calls = 0;        // total Draw*/DrawIndexed* calls in the frame
    std::uint32_t maxIndices = 0;   // largest single indexed draw
    std::uint32_t maxVertices = 0;  // largest single non-indexed draw
};

using Event = std::variant<FrameMarkerEvent, TextureCreatedEvent, ViewCreatedEvent,
                           RenderTargetsEvent, DrawEvent, DrawStatEvent, PresentEvent,
                           ViewportEvent>;

// "RLCAP1 ..." single line, without trailing newline.
[[nodiscard]] std::string formatEvent(const Event& event);

// RTV creation → tag "rtv"; DSV creation → tag "dsv" (same payload shape).
[[nodiscard]] std::string formatRtvCreated(const ViewCreatedEvent& e);
[[nodiscard]] std::string formatDsvCreated(const ViewCreatedEvent& e);

[[nodiscard]] std::optional<ViewCreatedEvent> parseViewCreated(std::string_view tag,
                                                               std::string_view rest);

// Strict parse: returns nullopt for lines that are not RLCAP1 or malformed.
[[nodiscard]] std::optional<Event> parseEvent(std::string_view line);

// Display helper: DXGI_FORMAT number → short name for reports
// ("R8G8B8A8_UNORM", "R16G16B16A16_FLOAT", "D24_UNORM_S8_UINT", …).
// Unknown formats render as "fmt=NN".
[[nodiscard]] std::string dxgiFormatName(std::uint32_t format);

// Display helper: D3D11_BIND_* bitmask → "RT|SRV|DS" compact string.
[[nodiscard]] std::string bindFlagsName(std::uint32_t bindFlags);

}  // namespace rl::backend::obs
