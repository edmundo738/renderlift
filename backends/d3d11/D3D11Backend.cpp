#include "renderlift/backend/IBackend.hpp"

namespace renderlift::backend {
class D3D11Backend final : public IBackend {
  BackendStatus status_{};
public:
  GraphicsApi api() const override { return GraphicsApi::D3D11; }
  std::string_view name() const override { return "RenderLift.D3D11"; }
  BackendStatus status() const override { return status_; }
  bool initialize() override {
    status_={true,false,"scaffold; DXGI/D3D11 render-target integration pending"};
    return true;
  }
  void shutdown() override { status_={false,false,"shutdown"}; }
};
}
