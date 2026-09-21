# ALRR Core — detection: hardware/OS probing (header-only for now).
add_library(rl-detection INTERFACE)
add_library(RenderLift::Core.Detection ALIAS rl-detection)

target_include_directories(rl-detection INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_link_libraries(rl-detection INTERFACE rl-core)
target_compile_features(rl-detection INTERFACE cxx_std_20)
