// RenderLift — D3D11 backend module (RenderLift.D3D11).  ◀ FIRST TARGET
//
// This is the backend for the first lab (GTA V @ Intel UHD 620, 1366×768).
// See docs/apis/d3d11.md for the full integration plan.
//
// The two rules that define this backend:
//   1. We do NOT settle for a Present-time resize. Interception must steer
//      texture/render-target/viewport creation so the game's 3D pipeline
//      genuinely runs at the internal resolution.
//   2. HUD/UI continues through to native-resolution targets; ALRR only
//      touches the 3D scene image between post-process and UI composition.
//
// Status: registry stub (hook engine lands in 0.2 with MinHook).
#include "renderlift/backend/StubBackend.hpp"

#include <memory>

namespace rl::backend {

class D3D11Backend final : public StubBackend {
public:
    D3D11Backend()
        : StubBackend(GraphicsApi::D3D11, "RenderLift.D3D11",
                      {// presentation
                       "IDXGISwapChain::Present",
                       "IDXGISwapChain::ResizeBuffers",
                       // internal-resolution steering
                       "ID3D11Device::CreateTexture2D",
                       "ID3D11Device::CreateRenderTargetView",
                       "ID3D11DeviceContext::RSSetViewports",
                       "ID3D11DeviceContext::OMSetRenderTargets",
                       // frame classification (3D pass vs UI pass)
                       "ID3D11DeviceContext::DrawIndexed",
                       "ID3D11DeviceContext::Draw"}) {}
};

std::unique_ptr<IBackend> createD3D11Backend() { return std::make_unique<D3D11Backend>(); }

}  // namespace rl::backend
