#include "renderlift/backend/IBackend.hpp"
namespace renderlift::backend {
class D3D12Backend final:public IBackend {
  BackendStatus status_{};
public:
  GraphicsApi api()const override{return GraphicsApi::D3D12;}
  std::string_view name()const override{return "RenderLift.D3D12";}
  BackendStatus status()const override{return status_;}
  bool initialize()override{status_={true,false,"scaffold; resource-state tracking pending"};return true;}
  void shutdown()override{status_={false,false,"shutdown"};}
};
}
