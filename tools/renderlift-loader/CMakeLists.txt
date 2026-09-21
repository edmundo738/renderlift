# RenderLift.Loader — Windows-only injection helper for authorized labs.
if(WIN32)
    add_executable(renderlift-loader main.cpp)
    target_compile_features(renderlift-loader PRIVATE cxx_std_20)
    set_target_properties(renderlift-loader PROPERTIES OUTPUT_NAME "RenderLift.Loader")
    message(STATUS "RenderLift.Loader will build (Windows injection helper)")
else()
    message(STATUS "RenderLift.Loader skipped — Windows-only tool")
endif()
