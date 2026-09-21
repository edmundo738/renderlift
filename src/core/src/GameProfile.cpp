#include "renderlift/core/GameProfile.hpp"

#include <fstream>
#include <sstream>
#include <stdexcept>

namespace rl::core {
namespace {

[[noreturn]] void profileError(const std::string& id, const std::string& message) {
    throw std::runtime_error("game profile '" + id + "': " + message);
}

Resolution resolutionFromJson(const json::Value& v, const std::string& id, const char* field) {
    if (!v.isObject()) {
        profileError(id, std::string("field '") + field + "' must be an object {width, height}");
    }
    const double w = v.numberOr("width", 0.0);
    const double h = v.numberOr("height", 0.0);
    if (w <= 0.0 || h <= 0.0 || w > 65535.0 || h > 65535.0) {
        profileError(id, std::string("field '") + field + "' has invalid dimensions");
    }
    return Resolution{static_cast<std::uint32_t>(w), static_cast<std::uint32_t>(h)};
}

}  // namespace

GameProfile GameProfile::fromJson(const json::Value& root) {
    if (!root.isObject()) {
        profileError("<unknown>", "root must be a JSON object");
    }

    GameProfile p;
    p.schema = root.stringOr("schema", "");
    p.id = root.stringOr("id", "");
    p.title = root.stringOr("title", "");
    if (p.id.empty()) {
        profileError("<unknown>", "missing required field 'id'");
    }
    if (p.title.empty()) {
        profileError(p.id, "missing required field 'title'");
    }

    p.executable = root.stringOr("executable", "");

    const std::string api = root.stringOr("api", "auto");
    const auto parsedApi = graphicsApiFromString(api);
    if (!parsedApi.has_value()) {
        profileError(p.id, "unknown api '" + api + "'");
    }
    p.api = *parsedApi;

    if (const json::Value* display = root.find("display")) {
        p.display = resolutionFromJson(*display, p.id, "display");
    }

    if (const json::Value* ladder = root.find("internalLadder")) {
        if (!ladder->isArray()) {
            profileError(p.id, "field 'internalLadder' must be an array of {width, height}");
        }
        for (const json::Value& entry : ladder->asArray()) {
            p.internalLadder.push_back(resolutionFromJson(entry, p.id, "internalLadder[]"));
        }
    }

    p.defaultLevelIndex = static_cast<std::size_t>(root.numberOr("defaultLevelIndex", 0.0));
    p.uiNative = root.boolOr("uiNative", true);

    if (const json::Value* reconstruction = root.find("reconstruction")) {
        const std::string mode = reconstruction->stringOr("mode", "edge");
        const auto parsedMode = reconstructionModeFromString(mode);
        if (!parsedMode.has_value()) {
            profileError(p.id, "unknown reconstruction mode '" + mode + "'");
        }
        p.reconstruction.mode = *parsedMode;
        const double s = reconstruction->numberOr("sharpening", 0.35);
        if (s < 0.0 || s > 1.0) {
            profileError(p.id, "reconstruction.sharpening must be within [0, 1]");
        }
        p.reconstruction.sharpening = static_cast<float>(s);
    }

    if (const json::Value* dynamic = root.find("dynamicResolution")) {
        p.dynamicResolution.enabled = dynamic->boolOr("enabled", true);
        p.dynamicResolution.targetFps = dynamic->numberOr("targetFps", 30.0);
        p.dynamicResolution.warmupFrames =
            static_cast<std::uint32_t>(dynamic->numberOr("warmupFrames", 20.0));
        p.dynamicResolution.cooldownFrames =
            static_cast<std::uint32_t>(dynamic->numberOr("cooldownFrames", 45.0));
        if (p.dynamicResolution.targetFps <= 1.0) {
            profileError(p.id, "dynamicResolution.targetFps must be greater than 1");
        }
    }

    return p;
}

GameProfile GameProfile::loadFile(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        throw std::runtime_error("cannot open game profile '" + path + "'");
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    try {
        return fromJson(json::parse(buffer.str()));
    } catch (const json::ParseError& e) {
        throw std::runtime_error("game profile '" + path + "': " + e.what());
    }
}

}  // namespace rl::core
