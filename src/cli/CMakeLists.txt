add_executable(fragmentx
  main.cpp
  html_reporter.cpp
  text_reporter.cpp
  commands.cpp
)

target_include_directories(fragmentx PRIVATE
  ${PROJECT_SOURCE_DIR}/include
  ${PROJECT_BINARY_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(fragmentx PRIVATE
  fragmentx_optimizer
  fragmentx_analysis
  fragmentx_capture
  fragmentx_runtime
  fragmentx_compile_options
  Threads::Threads
)

if(TARGET fragmentx_interpose)
  add_dependencies(fragmentx fragmentx_interpose)
  target_compile_definitions(fragmentx PRIVATE
    FRAGMENTX_INTERPOSE_PATH=\"$<TARGET_FILE:fragmentx_interpose>\"
  )
endif()

# On Linux, link dl for dlsym in interposition and symbol resolution.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  target_link_libraries(fragmentx PRIVATE ${CMAKE_DL_LIBS})
endif()

install(TARGETS fragmentx RUNTIME DESTINATION bin)

