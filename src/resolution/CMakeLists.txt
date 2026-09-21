# ALRR Core — resolution: ladders + dynamic resolution controller.
add_library(rl-resolution STATIC
    src/ResolutionManager.cpp
    src/DynamicResolutionController.cpp
)
add_library(RenderLift::Core.Resolution ALIAS rl-resolution)

target_include_directories(rl-resolution PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_link_libraries(rl-resolution PUBLIC rl-core)
target_compile_features(rl-resolution PUBLIC cxx_std_20)
