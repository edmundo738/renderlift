add_library(renderlift-core
  core/Types.cpp
  resolution/ResolutionManager.cpp
  resolution/DynamicResolutionController.cpp
)

target_include_directories(renderlift-core
  PUBLIC
    core/include
    resolution/include
)

target_compile_features(renderlift-core PUBLIC cxx_std_20)
add_library(RenderLift::Core ALIAS renderlift-core)
