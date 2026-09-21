#include "renderlift/DynamicResolutionController.hpp"
#include <algorithm>

namespace renderlift::resolution {
DynamicResolutionController::DynamicResolutionController(
    std::size_t levels,std::size_t initialLevel,
    std::size_t warmupFrames,std::size_t cooldownFrames)
  : levels_(std::max<std::size_t>(1,levels)),
    level_(std::min(initialLevel,levels_-1)),
    warmup_(warmupFrames),cooldown_(cooldownFrames) {}

DecisionResult DynamicResolutionController::update(const FrameStats& stats) {
  ++frame_;
  const bool cpuHot=stats.cpuUtilization>=0.95;
  const bool gpuHot=stats.gpuUtilization>=0.97;
  const bool gpuCool=stats.gpuUtilization<=0.70;
  const bool tooSlow=stats.frameMs>16.67*1.15;
  const bool plentyFast=stats.frameMs<16.67*0.85;

  Bottleneck b=Bottleneck::Balanced;
  if(cpuHot) b=Bottleneck::CPU; else if(gpuHot) b=Bottleneck::GPU;
  if(frame_<=warmup_) return {Decision::Hold,b,level_,"warmup"};
  if(frame_<lastChange_+cooldown_) return {Decision::Hold,b,level_,"cooldown"};
  if(cpuHot) return {Decision::Hold,b,level_,"cpu-bound"};

  if(gpuHot&&tooSlow&&level_>0) {
    --level_; lastChange_=frame_;
    return {Decision::StepDown,b,level_,"gpu-bound"};
  }
  if(gpuCool&&plentyFast&&level_+1<levels_) {
    ++level_; lastChange_=frame_;
    return {Decision::StepUp,b,level_,"gpu-headroom"};
  }
  return {Decision::Hold,b,level_,"stable"};
}
}
