add_library(fragmentx_analysis STATIC
  trace_parser.cpp
  lifetime_model.cpp
  workload_model.cpp
  fragmentation.cpp
)

target_include_directories(fragmentx_analysis
  PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/include>
    $<INSTALL_INTERFACE:include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(fragmentx_analysis
  PUBLIC  fragmentx_compile_options
  PRIVATE fragmentx_capture fragmentx_runtime
)

set_target_properties(fragmentx_analysis PROPERTIES
  POSITION_INDEPENDENT_CODE ON
)

