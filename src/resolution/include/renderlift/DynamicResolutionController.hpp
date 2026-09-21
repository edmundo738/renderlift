#pragma once
#include "renderlift/Types.hpp"
#include <cstddef>
#include <string_view>

namespace renderlift::resolution {
struct FrameStats {
  double frameMs{};
  double gpuUtilization{};
  double cpuUtilization{};
};
enum class Decision { Hold, StepDown, StepUp };
struct DecisionResult {
  Decision decision{Decision::Hold};
  Bottleneck bottleneck{Bottleneck::Unknown};
  std::size_t level{};
  std::string_view reason{};
};
class DynamicResolutionController {
public:
  DynamicResolutionController(std::size_t levels, std::size_t initialLevel,
                               std::size_t warmupFrames=10,
                               std::size_t cooldownFrames=30);
  DecisionResult update(const FrameStats& stats);
  std::size_t level() const { return level_; }
private:
  std::size_t levels_{};
  std::size_t level_{};
  std::size_t frame_{};
  std::size_t lastChange_{};
  std::size_t warmup_{};
  std::size_t cooldown_{};
};
}
