// RenderLift.CLI — profile inspector + dynamic-resolution simulator.
//
// The CLI is the offline toolbox of the project: inspect game profiles, print
// the internal-resolution ladder they resolve to, and simulate how the dynamic
// controller would react to GPU-bound / CPU-bound / headroom phases — without
// needing the game, a backend, or even a GPU.
//
//   RenderLift.CLI profile  <file.json>
//   RenderLift.CLI ladder   <WxH>
//   RenderLift.CLI simulate <file.json> [--frames N]
//   RenderLift.CLI backends
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "renderlift/RenderLift.hpp"
#include "renderlift/backend/Registry.hpp"
#include "renderlift/core/GameProfile.hpp"
#include "renderlift/resolution/ProfilePlan.hpp"

namespace {

void printBanner() {
    std::printf("RenderLift %s — %s %s\n", rl::productVersion(), rl::engineName(),
                rl::engineVersion());
    std::printf("Render less. Reconstruct more.\n\n");
}

void printUsage() {
    printBanner();
    std::printf(
        "usage:\n"
        "  RenderLift.CLI profile  <profile.json>   inspect a game profile\n"
        "  RenderLift.CLI ladder   <WxH>            generic ladder for a display (e.g. 1366x768)\n"
        "  RenderLift.CLI simulate <profile.json> [--frames N]\n"
        "                                           run the dynamic-resolution simulator\n"
        "  RenderLift.CLI backends                  list backend modules\n"
        "  RenderLift.CLI --help\n");
}

const char* decisionName(rl::resolution::Decision d) {
    switch (d) {
        case rl::resolution::Decision::StepDown: return "STEP DOWN";
        case rl::resolution::Decision::StepUp:   return "STEP UP  ";
        default:                                 return "hold     ";
    }
}

void printLadderEntry(std::size_t i, rl::Resolution display, rl::Resolution r, bool isStart) {
    const double load = static_cast<double>(r.pixelCount()) /
                        static_cast<double>(display.pixelCount());
    std::printf("  [%zu] %5ux%-5u  (%3.0f%% of native pixels)%s\n", i, r.width, r.height,
                load * 100.0, isStart ? "  ← default start" : "");
}

int cmdProfile(const char* path) {
    const rl::core::GameProfile p = rl::core::GameProfile::loadFile(path);
    printBanner();

    std::printf("profile ............. %s (%s)\n", p.id.c_str(), p.title.c_str());
    if (!p.executable.empty()) std::printf("executable .......... %s\n", p.executable.c_str());
    std::printf("api ................. %s\n", std::string(rl::toString(p.api)).c_str());
    const rl::Resolution display = p.display.valid() ? p.display : rl::Resolution{1366, 768};
    std::printf("display ............. %ux%u\n", display.width, display.height);
    std::printf("reconstruction ...... %s (sharpen %.2f)\n",
                std::string(rl::toString(p.reconstruction.mode)).c_str(),
                static_cast<double>(p.reconstruction.sharpening));
    std::printf("ui native ........... %s\n", p.uiNative ? "yes" : "no");
    std::printf("dynamic resolution .. %s @ %.0f fps target (warmup %u, cooldown %u)\n\n",
                p.dynamicResolution.enabled ? "on" : "off", p.dynamicResolution.targetFps,
                p.dynamicResolution.warmupFrames, p.dynamicResolution.cooldownFrames);

    const std::vector<rl::Resolution> ladder = rl::resolution::resolveLadder(p, display);
    const std::size_t start = rl::resolution::resolveStartLevel(p, ladder.size());
    std::printf("internal-resolution ladder (%zu rungs):\n", ladder.size());
    for (std::size_t i = 0; i < ladder.size(); ++i) printLadderEntry(i, display, ladder[i], i == start);
    return 0;
}

int cmdLadder(const char* displayArg) {
    unsigned w = 0, h = 0;
    if (std::sscanf(displayArg, "%ux%u", &w, &h) != 2 || w == 0 || h == 0) {
        std::fprintf(stderr, "error: expected display as <WxH>, got '%s'\n", displayArg);
        return 1;
    }
    printBanner();
    const rl::Resolution display{w, h};
    const auto ladder = rl::resolution::ResolutionManager::ladderForDisplay(display);
    std::printf("generic ladder for %ux%u (factor-based):\n", w, h);
    for (std::size_t i = 0; i < ladder.size(); ++i) printLadderEntry(i, display, ladder[i], false);
    return 0;
}

struct Phase {
    const char* name;
    const char* flavor;
    rl::resolution::FrameSample sample;
};

int cmdSimulate(const char* path, int framesPerPhase) {
    const rl::core::GameProfile p = rl::core::GameProfile::loadFile(path);
    printBanner();

    const rl::Resolution display = p.display.valid() ? p.display : rl::Resolution{1366, 768};
    const std::vector<rl::Resolution> ladder = rl::resolution::resolveLadder(p, display);
    const std::size_t start = rl::resolution::resolveStartLevel(p, ladder.size());

    rl::resolution::DynamicResolutionController controller(
        ladder.size(), start, rl::resolution::controllerConfigFrom(p));

    const double targetMs = 1000.0 / p.dynamicResolution.targetFps;
    std::printf("scenario ............ %s on %s @ %ux%u\n", p.title.c_str(), p.id.c_str(),
                display.width, display.height);
    std::printf("target .............. %.0f fps (%.1f ms frame budget)\n",
                p.dynamicResolution.targetFps, targetMs);
    std::printf("start ............... rung %zu → %ux%u\n\n", start, ladder[start].width,
                ladder[start].height);

    const std::vector<Phase> phases = {
        {"frame heavy fight", "GPU-bound: explosions, 99% GPU, slow frames",
         {targetMs * 1.4, 0.99, 0.35}},
        {"downtown traffic jam", "CPU-bound: 99% CPU, GPU has room, frames still slow",
         {targetMs * 1.4, 0.55, 0.99}},
        {"quiet apartment", "headroom: fast frames, GPU half idle",
         {targetMs * 0.6, 0.50, 0.40}},
    };

    std::size_t level = start;
    std::uint64_t frame = 0;
    for (const Phase& phase : phases) {
        std::printf("── %s\n   %s\n", phase.name, phase.flavor);
        int changes = 0;
        for (int i = 0; i < framesPerPhase; ++i) {
            ++frame;
            const rl::resolution::DecisionInfo d = controller.update(phase.sample);
            if (d.decision != rl::resolution::Decision::Hold) {
                ++changes;
                std::printf("   [frame %3llu] %s → %ux%-5u (%s)\n",
                            static_cast<unsigned long long>(frame), decisionName(d.decision),
                            ladder[d.level].width, ladder[d.level].height,
                            std::string(rl::toString(d.bottleneck)).c_str());
                level = d.level;
            }
        }
        if (changes == 0) {
            const rl::resolution::DecisionInfo d = controller.update(phase.sample);
            std::printf("   no ladder changes (%s)\n", d.reason);
        }
        std::printf("\n");
    }

    std::printf("summary\n");
    std::printf("  final internal resolution ... %ux%u\n", ladder[level].width,
                ladder[level].height);
    const double load = static_cast<double>(ladder[level].pixelCount()) /
                        static_cast<double>(display.pixelCount());
    std::printf("  3D render load .............. %.0f%% of native pixels\n", load * 100.0);
    return 0;
}

int cmdBackends() {
    printBanner();
    const auto all = rl::backend::createAllBackends();
    std::printf("backend modules (%zu):\n\n", all.size());
    for (const auto& b : all) {
        std::printf("  %-24s api=%-6s %s\n", b->info().moduleName.c_str(),
                    std::string(rl::toString(b->info().api)).c_str(),
                    b->info().hooksImplemented ? "hooks ✓" : "stub (hooks planned 0.2)");
        for (const auto target : b->plannedHookTargets()) {
            std::printf("      • %s\n", std::string(target).c_str());
        }
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2 || std::strcmp(argv[1], "--help") == 0 || std::strcmp(argv[1], "-h") == 0) {
        printUsage();
        return argc < 2 ? 1 : 0;
    }

    const std::string command = argv[1];
    try {
        if (command == "profile" && argc >= 3) return cmdProfile(argv[2]);
        if (command == "ladder" && argc >= 3) return cmdLadder(argv[2]);
        if (command == "backends") return cmdBackends();
        if (command == "simulate" && argc >= 3) {
            int frames = 240;
            for (int i = 3; i + 1 < argc; ++i) {
                if (std::strcmp(argv[i], "--frames") == 0) frames = std::atoi(argv[i + 1]);
            }
            if (frames < 60) frames = 60;  // below warmup+cooldown nothing happens
            return cmdSimulate(argv[2], frames);
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }

    std::fprintf(stderr, "error: unknown or malformed command '%s'\n\n", command.c_str());
    printUsage();
    return 1;
}
