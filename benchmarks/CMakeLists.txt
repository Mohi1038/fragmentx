add_executable(fragmentx_benchmarks
  allocator_benchmarks.cpp
)

target_link_libraries(fragmentx_benchmarks PRIVATE
  benchmark::benchmark
  fragmentx_optimizer
  fragmentx_runtime
  fragmentx_compile_options
  Threads::Threads
)

target_include_directories(fragmentx_benchmarks PRIVATE
  ${PROJECT_SOURCE_DIR}/include
  ${PROJECT_BINARY_DIR}/include
)
