add_library(renderlift-backend-vulkan STATIC VulkanBackend.cpp)
target_link_libraries(renderlift-backend-vulkan PUBLIC RenderLift::Core renderlift-backend-common)
