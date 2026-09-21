# ── RenderLift backends (per-API capture/injection modules) ─────
#
# common is a STATIC library with the platform-neutral interception
# machinery (hook engine interface + steering policy) — testable everywhere.
# The D3D11 module becomes the real SHARED RenderLift.D3D11.dll on Windows
# (0.2: hook install + measured passthrough; 0.3: full steering); other
# backends remain registry stubs until their wave.

add_library(rl-backend-common STATIC
    common/src/HookEngine.cpp
    common/src/Steering.cpp
)
add_library(RenderLift::Backend.Common ALIAS rl-backend-common)

target_include_directories(rl-backend-common PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/common/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(rl-backend-common PUBLIC rl-core)
target_compile_features(rl-backend-common PUBLIC cxx_std_20)

if(WIN32)
    target_link_libraries(rl-backend-common PRIVATE RenderLift::MinHook)
endif()

# ── Stub backends (uniform, platform-neutral) ───────────────────
function(renderlift_backend name source)
    add_library(rl-backend-${name} STATIC ${source})
    target_link_libraries(rl-backend-${name} PUBLIC rl-backend-common)
endfunction()

renderlift_backend(d3d9   d3d9/src/D3D9Backend.cpp)
renderlift_backend(d3d10  d3d10/src/D3D10Backend.cpp)
renderlift_backend(d3d12  d3d12/src/D3D12Backend.cpp)
renderlift_backend(vulkan vulkan/src/VulkanBackend.cpp)

# ── D3D11: first real backend ───────────────────────────────────
if(WIN32)
    add_library(rl-backend-d3d11 SHARED
        d3d11/src/D3D11Backend.cpp       # registry identity (createD3D11Backend)
        d3d11/src/win/D3D11Module.cpp    # the interception module
    )
    set_target_properties(rl-backend-d3d11 PROPERTIES OUTPUT_NAME "RenderLift.D3D11")
    target_link_libraries(rl-backend-d3d11 PRIVATE
        rl-backend-common RenderLift::MinHook d3d11 dxgi)
    message(STATUS "RenderLift.D3D11 will build as a SHARED module (real backend)")
else()
    renderlift_backend(d3d11 d3d11/src/D3D11Backend.cpp)
endif()

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
