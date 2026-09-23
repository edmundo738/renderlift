# ALRR Core — kernel: shared types, JSON, game profiles.
add_library(rl-core STATIC
    src/Json.cpp
    src/GameProfile.cpp
)
add_library(RenderLift::Core.Kernel ALIAS rl-core)

target_include_directories(rl-core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_compile_features(rl-core PUBLIC cxx_std_20)
