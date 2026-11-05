find_package(GTest QUIET)

if(NOT GTest_FOUND)
  include(FetchContent)
  FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.15.2
    GIT_SHALLOW    TRUE
  )
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googletest)
endif()

enable_testing()

add_executable(fragmentx_tests
  unit/test_size_classes.cpp
  unit/test_free_list.cpp
  unit/test_region_manager.cpp
  unit/test_arena_manager.cpp
  unit/test_allocator.cpp
  unit/test_trace_writer.cpp
  unit/test_event_recorder.cpp
  unit/test_capture_session.cpp
  unit/test_workload_model.cpp
  unit/test_compatibility.cpp
  unit/test_policy_serializer.cpp
)

target_include_directories(fragmentx_tests PRIVATE
  ${PROJECT_SOURCE_DIR}/include
  ${PROJECT_BINARY_DIR}/include
  ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(fragmentx_tests PRIVATE
  GTest::gtest_main
  fragmentx_optimizer
  fragmentx_analysis
  fragmentx_capture
  fragmentx_runtime
  fragmentx_compile_options
  Threads::Threads
)

include(GoogleTest)
gtest_discover_tests(fragmentx_tests)

