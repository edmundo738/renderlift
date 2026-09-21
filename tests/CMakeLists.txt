add_executable(renderlift-tests resolution_tests.cpp)
target_link_libraries(renderlift-tests PRIVATE RenderLift::Core)
add_test(NAME RenderLiftTests COMMAND renderlift-tests)
