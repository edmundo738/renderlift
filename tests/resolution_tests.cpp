#include "renderlift/ResolutionManager.hpp"
#include "renderlift/DynamicResolutionController.hpp"
#include <cassert>
#include <iostream>

using namespace renderlift;
using namespace renderlift::resolution;

int main() {
  const auto ladder=ResolutionManager::gtaVLadder();
  assert(ladder.size()==6);
  assert(ladder[0]==Resolution{426,240});
  assert(ladder.back()==Resolution{1366,768});

  DynamicResolutionController c(ladder.size(),2,0,0);
  auto down=c.update({25.0,0.99,0.50});
  assert(down.decision==Decision::StepDown);
  assert(down.level==1);

  auto cpu=c.update({25.0,0.99,0.99});
  assert(cpu.decision==Decision::Hold);

  std::cout<<"RenderLift core tests passed.\n";
}
