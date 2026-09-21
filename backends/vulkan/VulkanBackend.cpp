#include "renderlift/backend/IBackend.hpp"
namespace renderlift::backend {
class VulkanBackend final:public IBackend {
  BackendStatus status_{};
public:
  GraphicsApi api()const override{return GraphicsApi::Vulkan;}
  std::string_view name()const override{return "RenderLift.Vulkan";}
  BackendStatus status()const override{return status_;}
  bool initialize()override{status_={true,false,"scaffold; swapchain/present integration pending"};return true;}
  void shutdown()override{status_={false,false,"shutdown"};}
};
}
