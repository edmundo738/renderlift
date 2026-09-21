// RenderLift — Vulkan backend module (RenderLift.Vulkan).
//
// Scope (docs/apis/vulkan.md): its own swapchain + explicit synchronization
// model. Reconstruction is a compute dispatch on the acquired swapchain
// image; presentation stays on vkQueuePresentKHR. Function loading will go
// through volk (planned third_party dep, MIT).
//
// Status: registry stub.
#include "renderlift/backend/StubBackend.hpp"

#include <memory>

namespace rl::backend {

class VulkanBackend final : public StubBackend {
public:
    VulkanBackend()
        : StubBackend(GraphicsApi::Vulkan, "RenderLift.Vulkan",
                      {"vkCreateSwapchainKHR",
                       "vkAcquireNextImageKHR",
                       "vkQueuePresentKHR",
                       "vkCreateImageView",
                       "vkCmdSetViewport",
                       "vkCmdBeginRendering / vkCmdBeginRenderPass"}) {}
};

std::unique_ptr<IBackend> createVulkanBackend() { return std::make_unique<VulkanBackend>(); }

}  // namespace rl::backend
