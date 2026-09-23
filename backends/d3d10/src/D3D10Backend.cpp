// RenderLift — D3D10 backend module (RenderLift.D3D10).
//
// Scope (docs/apis/d3d11.md — D3D10 shares the DXGI family): first API with
// the DXGI swap chain (Present/ResizeBuffers). Structurally identical to the
// D3D11 backend, lower priority — most D3D10-era games also ship D3D11 or
// D3D9 executables.
//
// Status: registry stub (hook engine lands in 0.2).
#include "renderlift/backend/StubBackend.hpp"

#include <memory>

namespace rl::backend {

class D3D10Backend final : public StubBackend {
public:
    D3D10Backend()
        : StubBackend(GraphicsApi::D3D10, "RenderLift.D3D10",
                      {"IDXGISwapChain::Present",
                       "IDXGISwapChain::ResizeBuffers",
                       "ID3D10Device::CreateRenderTargetView",
                       "ID3D10Device::RSSetViewports",
                       "ID3D10Device::OMSetRenderTargets"}) {}
};

std::unique_ptr<IBackend> createD3D10Backend() { return std::make_unique<D3D10Backend>(); }

}  // namespace rl::backend
