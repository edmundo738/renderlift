// RenderLift — D3D9 backend module (RenderLift.D3D9).
//
// Scope (docs/apis/d3d9.md): legacy but widespread — Source engine, GTA IV,
// Skyrim (2011). Presentation is a IDirect3DDevice9::Present call on the
// device itself; Reset() changes the backbuffer in place and must be
// intercepted to keep our internal-target redirection consistent.
//
// Status: registry stub (hook engine lands in 0.2).
#include "renderlift/backend/StubBackend.hpp"

#include <memory>

namespace rl::backend {

class D3D9Backend final : public StubBackend {
public:
    D3D9Backend()
        : StubBackend(GraphicsApi::D3D9, "RenderLift.D3D9",
                      {"IDirect3DDevice9::Present",
                       "IDirect3DDevice9::Reset",
                       "IDirect3DDevice9::CreateRenderTarget",
                       "IDirect3DDevice9::SetRenderTarget",
                       "IDirect3DDevice9::SetViewport"}) {}
};

std::unique_ptr<IBackend> createD3D9Backend() { return std::make_unique<D3D9Backend>(); }

}  // namespace rl::backend
