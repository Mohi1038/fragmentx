add_library(fragmentx_runtime STATIC
  config.cpp
  policy_serializer.cpp
  region_manager.cpp
  arena_manager.cpp
  size_classes.cpp
  free_list.cpp
  allocation_router.cpp
  metadata_store.cpp
  thread_cache.cpp
  allocator.cpp
)

target_include_directories(fragmentx_runtime
  PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/include>
    $<INSTALL_INTERFACE:include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(fragmentx_runtime
  PUBLIC  fragmentx_compile_options Threads::Threads
)

set_target_properties(fragmentx_runtime PROPERTIES
  POSITION_INDEPENDENT_CODE ON
)
