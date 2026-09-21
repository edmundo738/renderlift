// RenderLift — ALRR Core: fundamental shared types.
//
// These types are API-agnostic: they are used by every engine module
// (resolution, reconstruction, renderer, detection, profiling) and by
// all backends. Nothing in this header may depend on a graphics API.
#pragma once

#include <compare>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace rl {

// ── Versioning ────────────────────────────────────────────────────────────

struct Version {
    std::uint32_t major = 0;
    std::uint32_t minor = 0;
    std::uint32_t patch = 0;

    [[nodiscard]] std::string toString() const {
        return std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(patch);
    }

    friend bool operator==(const Version&, const Version&) = default;
    friend auto operator<=>(const Version&, const Version&) = default;
};

// Product version (RenderLift) and engine version (ALRR Core) evolve on
// separate tracks: RenderLift 0.1 ships ALRR Spatial 0.1, RenderLift 0.5 adds
// ALRR Edge, RenderLift 1.0 adds ALRR Temporal.
inline constexpr Version kAlrrSpatialVersion{0, 1, 0};
inline constexpr Version kAlrrEdgeVersion{0, 5, 0};
inline constexpr Version kAlrrTemporalVersion{1, 0, 0};

// ── Resolution ────────────────────────────────────────────────────────────

struct Resolution {
    std::uint32_t width = 0;
    std::uint32_t height = 0;

    [[nodiscard]] constexpr std::uint64_t pixelCount() const {
        return static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height);
    }

    [[nodiscard]] constexpr double aspectRatio() const {
        return height == 0 ? 0.0 : static_cast<double>(width) / static_cast<double>(height);
    }

    [[nodiscard]] constexpr bool valid() const { return width > 0 && height > 0; }

    friend bool operator==(const Resolution&, const Resolution&) = default;
};

// ── Graphics API identification ───────────────────────────────────────────

enum class GraphicsApi : std::uint8_t {
    Unknown,  // also used as "auto-detect" in profiles
    D3D9,
    D3D10,
    D3D11,
    D3D12,
    Vulkan,
};

[[nodiscard]] inline std::string_view toString(GraphicsApi api) noexcept {
    switch (api) {
        case GraphicsApi::D3D9:   return "d3d9";
        case GraphicsApi::D3D10:  return "d3d10";
        case GraphicsApi::D3D11:  return "d3d11";
        case GraphicsApi::D3D12:  return "d3d12";
        case GraphicsApi::Vulkan: return "vulkan";
        default:                  return "auto";
    }
}

[[nodiscard]] inline std::optional<GraphicsApi> graphicsApiFromString(std::string_view name) noexcept {
    if (name == "d3d9")   return GraphicsApi::D3D9;
    if (name == "d3d10")  return GraphicsApi::D3D10;
    if (name == "d3d11")  return GraphicsApi::D3D11;
    if (name == "d3d12")  return GraphicsApi::D3D12;
    if (name == "vulkan") return GraphicsApi::Vulkan;
    if (name == "auto")   return GraphicsApi::Unknown;
    return std::nullopt;
}

// ── Reconstruction ────────────────────────────────────────────────────────

// ALRR reconstruction tiers. Spatial and Edge are ALRR-native; NIS is an
// interchangeable fallback backend (never the product identity); Temporal is
// the long-term quality tier.
enum class ReconstructionMode : std::uint8_t {
    Spatial,      // ALRR Spatial  — optimized resampling + controlled sharpening
    Edge,         // ALRR Edge     — edge/contrast-aware reconstruction
    NisFallback,  // NVIDIA Image Scaling shader as an interchangeable backend
    Temporal,     // ALRR Temporal — frame history + jitter (+ motion info later)
};

[[nodiscard]] inline std::string_view toString(ReconstructionMode mode) noexcept {
    switch (mode) {
        case ReconstructionMode::Spatial:     return "spatial";
        case ReconstructionMode::Edge:        return "edge";
        case ReconstructionMode::NisFallback: return "nis";
        case ReconstructionMode::Temporal:    return "temporal";
        default:                              return "unknown";
    }
}

[[nodiscard]] inline std::optional<ReconstructionMode> reconstructionModeFromString(std::string_view name) noexcept {
    if (name == "spatial")  return ReconstructionMode::Spatial;
    if (name == "edge")     return ReconstructionMode::Edge;
    if (name == "nis")      return ReconstructionMode::NisFallback;
    if (name == "temporal") return ReconstructionMode::Temporal;
    return std::nullopt;
}

// ── Bottleneck classification ─────────────────────────────────────────────
//
// Lowering internal resolution only helps when the GPU is the bottleneck.
// When the CPU is saturated a smaller render target just wastes quality.

enum class Bottleneck : std::uint8_t {
    Unknown,
    Gpu,
    Cpu,
    Balanced,
};

[[nodiscard]] inline std::string_view toString(Bottleneck b) noexcept {
    switch (b) {
        case Bottleneck::Gpu:      return "gpu";
        case Bottleneck::Cpu:      return "cpu";
        case Bottleneck::Balanced: return "balanced";
        default:                   return "unknown";
    }
}

}  // namespace rl
