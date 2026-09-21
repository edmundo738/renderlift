add_library(renderlift-backend-d3d10 STATIC D3D10Backend.cpp)
target_link_libraries(renderlift-backend-d3d10 PUBLIC RenderLift::Core renderlift-backend-common)
