#pragma once
#include <cstdint>
#include <string_view>

namespace renderlift {

struct Resolution {
  std::uint32_t width{};
  std::uint32_t height{};
  constexpr bool operator==(const Resolution&) const = default;
  constexpr bool valid() const { return width > 0 && height > 0; }
};

enum class GraphicsApi { Unknown, D3D9, D3D10, D3D11, D3D12, Vulkan };
enum class ReconstructionMode { Spatial, Edge, Temporal };
enum class Bottleneck { Unknown, GPU, CPU, Balanced };

std::string_view toString(GraphicsApi api);
std::string_view toString(ReconstructionMode mode);
std::string_view toString(Bottleneck bottleneck);

}
