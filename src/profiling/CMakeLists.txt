# ALRR Core — profiling: frame timers and rolling stats (header-only).
add_library(rl-profiling INTERFACE)
add_library(RenderLift::Core.Profiling ALIAS rl-profiling)

target_include_directories(rl-profiling INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_link_libraries(rl-profiling INTERFACE rl-core)
target_compile_features(rl-profiling INTERFACE cxx_std_20)
