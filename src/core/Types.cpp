#include "renderlift/Types.hpp"

namespace renderlift {
std::string_view toString(GraphicsApi api) {
  switch (api) {
    case GraphicsApi::D3D9: return "D3D9";
    case GraphicsApi::D3D10: return "D3D10";
    case GraphicsApi::D3D11: return "D3D11";
    case GraphicsApi::D3D12: return "D3D12";
    case GraphicsApi::Vulkan: return "Vulkan";
    default: return "Unknown";
  }
}
std::string_view toString(ReconstructionMode mode) {
  switch (mode) {
    case ReconstructionMode::Spatial: return "Spatial";
    case ReconstructionMode::Edge: return "Edge";
    case ReconstructionMode::Temporal: return "Temporal";
  }
  return "Unknown";
}
std::string_view toString(Bottleneck bottleneck) {
  switch (bottleneck) {
    case Bottleneck::GPU: return "GPU";
    case Bottleneck::CPU: return "CPU";
    case Bottleneck::Balanced: return "Balanced";
    default: return "Unknown";
  }
}
}
