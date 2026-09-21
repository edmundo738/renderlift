#include "renderlift/ResolutionManager.hpp"
#include <algorithm>
#include <cmath>

namespace renderlift::resolution {
namespace {
std::uint32_t even(double value) {
  auto n = static_cast<std::uint32_t>(std::lround(value));
  if (n & 1u) ++n;
  return std::max<std::uint32_t>(2, n);
}
}
Resolution ResolutionManager::fromScale(Resolution display, double scale) {
  scale = std::clamp(scale, 0.10, 1.0);
  return {even(display.width * scale), even(display.height * scale)};
}
std::vector<Resolution> ResolutionManager::defaultLadder(Resolution display) {
  constexpr double scales[] = {0.25,0.3125,0.375,0.50,0.625,0.75,1.0};
  std::vector<Resolution> result;
  for (double scale : scales) result.push_back(fromScale(display, scale));
  return result;
}
std::vector<Resolution> ResolutionManager::gtaVLadder() {
  return {{426,240},{480,270},{640,360},{854,480},{1024,576},{1366,768}};
}
}
