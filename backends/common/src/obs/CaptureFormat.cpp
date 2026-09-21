#include "renderlift/backend/obs/CaptureFormat.hpp"

#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <type_traits>
#include <vector>

namespace rl::backend::obs {
namespace {

// ── Formatting ──────────────────────────────────────────────────────────────

std::string hex(std::uint64_t v) {
    char buf[24];
    std::snprintf(buf, sizeof buf, "0x%llx", static_cast<unsigned long long>(v));
    return buf;
}

std::string num(std::uint64_t v) { return std::to_string(v); }

std::string fnum(float v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%g", static_cast<double>(v));
    return buf;
}

// ── Parsing helpers ─────────────────────────────────────────────────────────

using Tokens = std::map<std::string, std::string>;

Tokens tokenize(std::string_view rest) {
    Tokens out;
    std::size_t pos = 0;
    while (pos < rest.size()) {
        const std::size_t space = rest.find(' ', pos);
        const std::string_view token =
            rest.substr(pos, space == std::string_view::npos ? rest.size() - pos : space - pos);
        pos = space == std::string_view::npos ? rest.size() : space + 1;
        if (token.empty()) continue;
        const std::size_t eq = token.find('=');
        if (eq == std::string_view::npos) return {};  // strict: every token is key=value
        out.emplace(std::string(token.substr(0, eq)), std::string(token.substr(eq + 1)));
    }
    return out;
}

bool getU64(const Tokens& t, const char* key, std::uint64_t& out) {
    const auto it = t.find(key);
    if (it == t.end()) return false;
    try {
        const std::string& s = it->second;
        out = (s.rfind("0x", 0) == 0) ? std::stoull(s, nullptr, 16) : std::stoull(s);
        return true;
    } catch (...) {
        return false;
    }
}

bool getU32(const Tokens& t, const char* key, std::uint32_t& out) {
    std::uint64_t v = 0;
    if (!getU64(t, key, v) || v > 0xFFFFFFFFull) return false;
    out = static_cast<std::uint32_t>(v);
    return true;
}

bool getF32(const Tokens& t, const char* key, float& out) {
    const auto it = t.find(key);
    if (it == t.end()) return false;
    try {
        out = std::stof(it->second);
        return true;
    } catch (...) {
        return false;
    }
}

template <typename E>
bool fillViewCreatedEvent(const Tokens& t, E& e) {
    return getU64(t, "dev", e.device) && getU64(t, "id", e.viewId) &&
           getU64(t, "res", e.resourceId);
}

}  // namespace

// ── formatEvent ─────────────────────────────────────────────────────────────

std::string formatRtvCreated(const ViewCreatedEvent& e) {
    return "RLCAP1 rtv dev=" + hex(e.device) + " id=" + hex(e.viewId) +
           " res=" + hex(e.resourceId);
}

std::string formatDsvCreated(const ViewCreatedEvent& e) {
    return "RLCAP1 dsv dev=" + hex(e.device) + " id=" + hex(e.viewId) +
           " res=" + hex(e.resourceId);
}

std::string formatEvent(const Event& event) {
    const auto visitor = [](const auto& e) -> std::string {
        using E = std::decay_t<decltype(e)>;
        if constexpr (std::is_same_v<E, FrameMarkerEvent>) {
            return "RLCAP1 frame=" + num(e.frame);
        } else if constexpr (std::is_same_v<E, TextureCreatedEvent>) {
            return "RLCAP1 tex dev=" + hex(e.device) + " id=" + hex(e.id) +
                   " w=" + num(e.width) + " h=" + num(e.height) +
                   " fmt=" + num(e.dxgiFormat) + " bind=" + hex(e.bindFlags) +
                   " mips=" + num(e.mipLevels) + " array=" + num(e.arraySize) +
                   " samples=" + num(e.samples);
        } else if constexpr (std::is_same_v<E, ViewCreatedEvent>) {
            return formatRtvCreated(e);
        } else if constexpr (std::is_same_v<E, ViewportEvent>) {
            return "RLCAP1 vp ctx=" + hex(e.context) + " n=" + num(e.count) +
                   " x=" + fnum(e.x) + " y=" + fnum(e.y) + " w=" + fnum(e.w) +
                   " h=" + fnum(e.h);
        } else if constexpr (std::is_same_v<E, RenderTargetsEvent>) {
            std::string s = "RLCAP1 rt ctx=" + hex(e.context) + " n=" + num(e.count) + " rtvs=";
            std::string list;
            for (std::uint32_t i = 0; i < e.count && i < kMaxRtvs; ++i) {
                if (!list.empty()) list += ',';
                list += hex(e.rtvs[i]);
            }
            if (list.empty()) list = "0x0";
            return s + list + " dsv=" + hex(e.dsv);
        } else if constexpr (std::is_same_v<E, DrawEvent>) {
            return std::string("RLCAP1 draw ctx=") + hex(e.context) +
                   " indexed=" + (e.indexed ? "1" : "0") + " count=" + num(e.count);
        } else if constexpr (std::is_same_v<E, DrawStatEvent>) {
            return "RLCAP1 drawstat frame=" + num(e.frame) + " calls=" + num(e.calls) +
                   " maxidx=" + num(e.maxIndices) + " maxvtx=" + num(e.maxVertices);
        } else if constexpr (std::is_same_v<E, PresentEvent>) {
            return "RLCAP1 present sc=" + hex(e.swapchain) + " frame=" + num(e.frame);
        }
        return {};
    };
    return std::visit(visitor, event);
}

// ── parseEvent ──────────────────────────────────────────────────────────────

std::optional<ViewCreatedEvent> parseViewCreated(std::string_view, std::string_view rest) {
    const Tokens t = tokenize(rest);
    ViewCreatedEvent e;
    if (!fillViewCreatedEvent(t, e)) return std::nullopt;
    return e;
}

std::optional<Event> parseEvent(std::string_view line) {
    if (line.rfind(kCapturePrefix, 0) != 0) return std::nullopt;
    const std::string_view rest = line.substr(kCapturePrefix.size() + 1);  // skip "RLCAP1 "

    // "frame=N" carries its value in the tag position — handle it first.
    if (rest.rfind("frame=", 0) == 0) {
        FrameMarkerEvent e;
        const Tokens t = tokenize(rest);
        if (!getU64(t, "frame", e.frame)) return std::nullopt;
        return Event{e};
    }

    const std::size_t space = rest.find(' ');
    const std::string_view tag = rest.substr(0, space);
    const std::string_view body =
        space == std::string_view::npos ? std::string_view{} : rest.substr(space + 1);
    const Tokens t = tokenize(body);

    auto u64 = [&](const char* k, std::uint64_t& v) { return getU64(t, k, v); };
    auto u32 = [&](const char* k, std::uint32_t& v) { return getU32(t, k, v); };

    if (tag == "tex") {
        TextureCreatedEvent e;
        if (!u64("dev", e.device) || !u64("id", e.id) || !u32("w", e.width) ||
            !u32("h", e.height) || !u32("fmt", e.dxgiFormat) || !u32("bind", e.bindFlags) ||
            !u32("mips", e.mipLevels) || !u32("array", e.arraySize) || !u32("samples", e.samples)) {
            return std::nullopt;
        }
        return Event{e};
    }
    if (tag == "rtv" || tag == "dsv") {
        const auto view = parseViewCreated(tag, body);
        if (!view.has_value()) return std::nullopt;
        return Event{*view};
    }
    if (tag == "vp") {
        ViewportEvent e;
        if (!u64("ctx", e.context) || !u32("n", e.count) || !getF32(t, "x", e.x) ||
            !getF32(t, "y", e.y) || !getF32(t, "w", e.w) || !getF32(t, "h", e.h)) {
            return std::nullopt;
        }
        return Event{e};
    }
    if (tag == "rt") {
        RenderTargetsEvent e;
        if (!u64("ctx", e.context) || !u32("n", e.count)) return std::nullopt;
        const auto it = t.find("rtvs");
        if (it == t.end()) return std::nullopt;
        std::string list = it->second;
        std::size_t pos = 0;
        std::uint32_t parsed = 0;
        while (pos <= list.size() && parsed < kMaxRtvs) {
            const std::size_t comma = list.find(',', pos);
            const std::string item = list.substr(
                pos, comma == std::string::npos ? list.size() - pos : comma - pos);
            try {
                e.rtvs[parsed++] =
                    item.rfind("0x", 0) == 0 ? std::stoull(item, nullptr, 16) : std::stoull(item);
            } catch (...) {
                return std::nullopt;
            }
            if (comma == std::string::npos) break;
            pos = comma + 1;
        }
        if (parsed < e.count) e.count = parsed;  // tolerate truncated lists
        if (!u64("dsv", e.dsv)) return std::nullopt;
        return Event{e};
    }
    if (tag == "draw") {
        DrawEvent e;
        std::uint64_t indexed = 0;
        if (!u64("ctx", e.context) || !u64("indexed", indexed) || !u32("count", e.count)) {
            return std::nullopt;
        }
        e.indexed = indexed != 0;
        return Event{e};
    }
    if (tag == "drawstat") {
        DrawStatEvent e;
        std::uint64_t frame = 0;
        if (!u64("frame", frame) || !u32("calls", e.calls) || !u32("maxidx", e.maxIndices) ||
            !u32("maxvtx", e.maxVertices)) {
            return std::nullopt;
        }
        e.frame = frame;
        return Event{e};
    }
    if (tag == "present") {
        PresentEvent e;
        if (!u64("sc", e.swapchain) || !u64("frame", e.frame)) return std::nullopt;
        return Event{e};
    }
    return std::nullopt;
}

// ── Display helpers ─────────────────────────────────────────────────────────

std::string dxgiFormatName(std::uint32_t f) {
    switch (f) {
        case 2:  return "R32G32B32A32_FLOAT";
        case 10: return "R16G16B16A16_FLOAT";
        case 11: return "R16G16B16A16_UNORM";
        case 20: return "D32_FLOAT_S8X24_UINT";
        case 24: return "R10G10B10A2_UNORM";
        case 28: return "R8G8B8A8_UNORM";
        case 29: return "R8G8B8A8_UNORM_SRGB";
        case 40: return "D32_FLOAT";
        case 45: return "D24_UNORM_S8_UINT";
        case 54: return "R16_FLOAT";
        case 56: return "R16_UNORM";
        case 57: return "R16_UINT";
        case 71: return "BC1_UNORM";
        case 74: return "BC2_UNORM";
        case 77: return "BC3_UNORM";
        case 87: return "R11G11B10_FLOAT";
        default: return "fmt=" + std::to_string(f);
    }
}

std::string bindFlagsName(std::uint32_t flags) {
    std::string out;
    const auto add = [&](std::uint32_t bit, const char* name) {
        if (flags & bit) {
            if (!out.empty()) out += '|';
            out += name;
        }
    };
    add(0x01, "VB");
    add(0x02, "IB");
    add(0x04, "CB");
    add(0x08, "SRV");
    add(0x10, "SO");
    add(0x20, "RT");   // D3D11_BIND_RENDER_TARGET
    add(0x40, "DS");   // D3D11_BIND_DEPTH_STENCIL (0x40) — see note below
    add(0x80, "UAV");
    // D3D11 constants: SHADER_RESOURCE=0x8, STREAM_OUTPUT=0x10,
    // RENDER_TARGET=0x20, DEPTH_STENCIL=0x40, UNORDERED_ACCESS=0x80.
    if (out.empty()) out.push_back('-');  // ('-' as lit. trips GCC 12 -Wrestrict FPs)
    return out;
}

}  // namespace rl::backend::obs
