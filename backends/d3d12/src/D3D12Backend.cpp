// RenderLift — D3D12 backend module (RenderLift.D3D12).
//
// D3D12 is NOT "D3D11 with different names" (docs/apis/d3d12.md): the game
// owns command lists and explicit resource states. Our reconstruction pass
// must slot a compute dispatch into the game's command stream and respect
// D3D12_RESOURCE_STATE_* transitions (COMMON → UNORDERED_ACCESS →
// PRESENT_SOURCE). This backend is scheduled after D3D11 proves the model.
//
// Status: registry stub.
#include "renderlift/backend/StubBackend.hpp"

#include <memory>

namespace rl::backend {

class D3D12Backend final : public StubBackend {
public:
    D3D12Backend()
        : StubBackend(GraphicsApi::D3D12, "RenderLift.D3D12",
                      {"ID3D12CommandQueue::ExecuteCommandLists",
                       "IDXGISwapChain3::Present",
                       "IDXGISwapChain::ResizeBuffers",
                       "ID3D12Device::CreateRenderTargetView",
                       "ID3D12GraphicsCommandList::RSSetViewports",
                       "ID3D12GraphicsCommandList::OMSetRenderTargets",
                       "ID3D12GraphicsCommandList::ResourceBarrier"}) {}
};

std::unique_ptr<IBackend> createD3D12Backend() { return std::make_unique<D3D12Backend>(); }

}  // namespace rl::backend
