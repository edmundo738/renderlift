add_library(renderlift-backend-d3d9 STATIC D3D9Backend.cpp)
target_link_libraries(renderlift-backend-d3d9 PUBLIC RenderLift::Core renderlift-backend-common)
