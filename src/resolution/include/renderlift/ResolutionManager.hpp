#pragma once
#include "renderlift/Types.hpp"
#include <vector>

namespace renderlift::resolution {
class ResolutionManager {
public:
  static Resolution fromScale(Resolution display, double scale);
  static std::vector<Resolution> defaultLadder(Resolution display);
  static std::vector<Resolution> gtaVLadder();
};
}
