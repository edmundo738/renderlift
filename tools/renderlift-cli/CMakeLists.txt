# RenderLift.CLI — profile inspector + dynamic-resolution simulator.
add_executable(renderlift-cli main.cpp)

target_link_libraries(renderlift-cli PRIVATE RenderLift::Core RenderLift::Backends)
target_compile_features(renderlift-cli PRIVATE cxx_std_20)

# Ships as RenderLift.CLI(.exe) per the product naming scheme.
set_target_properties(renderlift-cli PROPERTIES OUTPUT_NAME "RenderLift.CLI")
