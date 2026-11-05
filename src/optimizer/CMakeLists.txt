add_library(fragmentx_optimizer STATIC
  feature_extractor.cpp
  compatibility.cpp
  candidate_generator.cpp
  policy_evaluator.cpp
  policy_search.cpp
  workload_shift_detector.cpp
)

target_include_directories(fragmentx_optimizer
  PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/include>
    $<INSTALL_INTERFACE:include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(fragmentx_optimizer
  PUBLIC  fragmentx_compile_options
  PRIVATE fragmentx_analysis fragmentx_capture fragmentx_runtime
)

set_target_properties(fragmentx_optimizer PROPERTIES
  POSITION_INDEPENDENT_CODE ON
)

