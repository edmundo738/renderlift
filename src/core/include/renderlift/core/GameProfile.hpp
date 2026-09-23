// RenderLift — ALRR Core: game profile model + loader.
//
// A GameProfile describes how RenderLift should treat one title: preferred
// API, target display resolution, the internal-resolution ladder, the default
// rung, reconstruction settings and dynamic-resolution behavior. Profiles are
// JSON files under profiles/ (schema: renderlift/game-profile@1 — see
// profiles/README.md).
//
// This module only models and loads the data. Turning a profile into an
// actual resolution ladder or a controller configuration happens in
// src/resolution (which depends on this module, never the other way around).
#pragma once

#include "renderlift/core/Json.hpp"
#include "renderlift/core/Types.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace rl::core {

struct ReconstructionSettings {
    ReconstructionMode mode = ReconstructionMode::Edge;
    float sharpening = 0.35f;  // 0..1, kept low for low-resolution sources
};

struct DynamicResolutionSettings {
    bool enabled = true;
    double targetFps = 30.0;
    std::uint32_t warmupFrames = 20;    // measurements ignored at startup
    std::uint32_t cooldownFrames = 45;  // min frames between ladder changes
};

// How far the backend is allowed to integrate for this title (ADR 0003).
struct IntegrationSettings {
    IntegrationMode mode = IntegrationMode::Observe;
    // In observe mode: stop logging after this many frames (0 = unlimited).
    std::uint32_t observationFrames = 600;
};

struct GameProfile {
    // Schema identifier, e.g. "renderlift/game-profile@1".
    std::string schema;
    // Stable unique id, e.g. "gta5", "generic.default".
    std::string id;
    // Human-readable title, e.g. "Grand Theft Auto V".
    std::string title;
    // Process name used for detection, e.g. "GTA5.exe" (may be empty).
    std::string executable;
    // Preferred graphics API; Unknown == auto-detect.
    GraphicsApi api = GraphicsApi::Unknown;
    // Target display resolution the profile was tuned for. When zero-sized,
    // the runtime display resolution is used instead.
    Resolution display{};
    // Explicit internal-resolution ladder (ascending). When empty, the
    // resolution manager derives one from the display resolution.
    std::vector<Resolution> internalLadder;
    // Index into the resolved ladder used at startup.
    std::size_t defaultLevelIndex = 0;
    // Compose HUD/UI at native display resolution.
    bool uiNative = true;

    ReconstructionSettings reconstruction{};
    DynamicResolutionSettings dynamicResolution{};
    IntegrationSettings integration{};

    // Loads a profile from an already-parsed JSON document.
    // Throws std::runtime_error on missing/invalid required fields.
    [[nodiscard]] static GameProfile fromJson(const json::Value& root);

    // Reads and parses a JSON profile file. Throws std::runtime_error when
    // the file cannot be read or parsed.
    [[nodiscard]] static GameProfile loadFile(const std::string& path);
};

}  // namespace rl::core
