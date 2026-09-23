# ── ALRR Core (API-agnostic engine) ─────────────────────────────
# Layout mirrors the engine's module map. Everything here must stay free of
# graphics-API includes; interception lives in backends/.
add_subdirectory(core)
add_subdirectory(resolution)
add_subdirectory(reconstruction)
add_subdirectory(renderer)
add_subdirectory(detection)
add_subdirectory(profiling)

# Umbrella target: link this to get the whole engine.
add_library(renderlift-core INTERFACE)
add_library(RenderLift::Core ALIAS renderlift-core)

target_link_libraries(renderlift-core INTERFACE
    rl-core
    rl-resolution
    rl-reconstruction
    rl-renderer
    rl-detection
    rl-profiling
)

# When the runtime becomes a real shared library on Windows it will ship as
# RenderLift.Core.dll — until then it is a static/INTERFACE aggregate.
set_target_properties(renderlift-core PROPERTIES
    EXPORT_NAME Core
)
