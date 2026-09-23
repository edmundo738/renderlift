// RenderLift — ALRR Core: hardware/OS probing.
//
// Profiles can be selected automatically once we know the machine. The probe
// interface is deliberately tiny; platform implementations (DXGI adapter
// enumeration + WMI/SetupAPI on Windows, lspci/sysfs elsewhere) land with the
// runtime they serve. Until then, NullProbe keeps the pipeline honest and
// testable.
#pragma once

#include "renderlift/core/Types.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace rl::detect {

struct GpuInfo {
    std::string name;
    std::uint32_t vendorId = 0;
    std::uint32_t deviceId = 0;
    std::uint64_t dedicatedVramBytes = 0;
    bool integrated = false;
    // Best API this GPU can realistically drive (e.g. D3D11 on an UHD 620).
    GraphicsApi bestApi = GraphicsApi::Unknown;
};

struct CpuInfo {
    std::string brand;
    std::uint32_t physicalCores = 0;
    std::uint32_t logicalCores = 0;
};

struct SystemInfo {
    CpuInfo cpu;
    std::vector<GpuInfo> gpus;
    std::string osName;
};

class ISystemProbe {
public:
    virtual ~ISystemProbe() = default;
    [[nodiscard]] virtual SystemInfo probe() = 0;
};

// Returned on platforms without a real implementation yet.
class NullProbe final : public ISystemProbe {
public:
    [[nodiscard]] SystemInfo probe() override {
        SystemInfo info;
        info.cpu.brand = "unknown (platform probe not implemented)";
        info.osName = "unknown";
        return info;
    }
};

// Factory for the platform's real probe. Today it always yields NullProbe;
// the Windows DXGI-based implementation arrives with the D3D11 backend (0.2).
[[nodiscard]] inline std::unique_ptr<ISystemProbe> createPlatformProbe() {
    return std::make_unique<NullProbe>();
}

}  // namespace rl::detect
