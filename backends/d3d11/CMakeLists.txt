add_library(renderlift-backend-d3d11 STATIC D3D11Backend.cpp)
target_link_libraries(renderlift-backend-d3d11 PUBLIC RenderLift::Core renderlift-backend-common)
target_include_directories(renderlift-backend-d3d11 PUBLIC .)
