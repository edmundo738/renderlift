#include "renderlift/ResolutionManager.hpp"
#include "renderlift/DynamicResolutionController.hpp"
#include <iostream>

int main() {
  using namespace renderlift;
  using namespace renderlift::resolution;
  const auto ladder=ResolutionManager::gtaVLadder();

  std::cout<<"RenderLift 0.1.0\n";
  std::cout<<"Engine: ALRR Core\n";
  std::cout<<"Laboratory: GTA V / D3D11 / 1366x768\n";
  for(std::size_t i=0;i<ladder.size();++i)
    std::cout<<"  ["<<i<<"] "<<ladder[i].width<<"x"<<ladder[i].height<<"\n";

  DynamicResolutionController c(ladder.size(),2,0,0);
  auto result=c.update({25.0,0.99,0.50});
  std::cout<<"Synthetic decision: "<<static_cast<int>(result.decision)
           <<" -> level "<<result.level<<" ("<<result.reason<<")\n";
}
