add_library(renderlift-backend-d3d12 STATIC D3D12Backend.cpp)
target_link_libraries(renderlift-backend-d3d12 PUBLIC RenderLift::Core renderlift-backend-common)
