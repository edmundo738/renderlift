# RenderLift test suite — zero external dependencies, wired to CTest.
add_executable(renderlift-tests
    test_main.cpp
    test_types.cpp
    test_json.cpp
    test_profiles.cpp
    test_resolution_manager.cpp
    test_dynamic_resolution.cpp
    test_reconstruction.cpp
    test_render_path.cpp
    test_backends.cpp
    test_vtable.cpp
    test_steering.cpp
)

target_include_directories(renderlift-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(renderlift-tests PRIVATE RenderLift::Core RenderLift::Backends)
target_compile_features(renderlift-tests PRIVATE cxx_std_20)

# Tests load the real profiles/ directory.
target_compile_definitions(renderlift-tests PRIVATE
    RENDERLIFT_PROFILES_DIR="${CMAKE_SOURCE_DIR}/profiles"
)

add_test(NAME renderlift-unit COMMAND renderlift-tests)
