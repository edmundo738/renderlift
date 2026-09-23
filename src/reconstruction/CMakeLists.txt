# ALRR Core — reconstruction: the engine's tiers (Spatial / Edge / …).
add_library(rl-reconstruction STATIC
    src/Reconstruction.cpp
    src/SpatialUpscaler.cpp
    src/EdgeUpscaler.cpp
)
add_library(RenderLift::Core.Reconstruction ALIAS rl-reconstruction)

target_include_directories(rl-reconstruction PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_link_libraries(rl-reconstruction PUBLIC rl-core)
target_compile_features(rl-reconstruction PUBLIC cxx_std_20)
