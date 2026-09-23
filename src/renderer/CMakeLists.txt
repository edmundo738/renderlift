# ALRR Core — renderer: frame pipeline contract (header-only for now).
add_library(rl-renderer INTERFACE)
add_library(RenderLift::Core.Renderer ALIAS rl-renderer)

target_include_directories(rl-renderer INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_link_libraries(rl-renderer INTERFACE rl-core)
target_compile_features(rl-renderer INTERFACE cxx_std_20)
