# ── RenderLift backends (per-API capture/injection modules) ─────
#
# Until the hook engine lands (0.2), every backend is a small STATIC stub
# that carries its identity + planned hook targets and builds everywhere.
# On Windows these become SHARED libraries named RenderLift.D3D9.dll …
# RenderLift.Vulkan.dll; that flip happens per module when it grows real
# interception code, without touching the IBackend contract.

add_library(rl-backend-common INTERFACE)
target_include_directories(rl-backend-common INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/common/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(rl-backend-common INTERFACE rl-core)
target_compile_features(rl-backend-common INTERFACE cxx_std_20)

function(renderlift_backend name source)
    add_library(rl-backend-${name} STATIC ${source})
    target_link_libraries(rl-backend-${name} PUBLIC rl-backend-common)
endfunction()

renderlift_backend(d3d9   d3d9/src/D3D9Backend.cpp)
renderlift_backend(d3d10  d3d10/src/D3D10Backend.cpp)
renderlift_backend(d3d11  d3d11/src/D3D11Backend.cpp)
renderlift_backend(d3d12  d3d12/src/D3D12Backend.cpp)
renderlift_backend(vulkan vulkan/src/VulkanBackend.cpp)

# Umbrella target: gives consumers every backend + the registry header.
add_library(renderlift-backends INTERFACE)
add_library(RenderLift::Backends ALIAS renderlift-backends)
target_link_libraries(renderlift-backends INTERFACE
    rl-backend-d3d9
    rl-backend-d3d10
    rl-backend-d3d11
    rl-backend-d3d12
    rl-backend-vulkan
)
