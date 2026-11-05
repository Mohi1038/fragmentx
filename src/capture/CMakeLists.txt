add_library(fragmentx_capture STATIC
  allocation_hooks.cpp
  event_recorder.cpp
  trace_writer.cpp
  trace_reader.cpp
  capture_session.cpp
  site_registry.cpp
)

target_include_directories(fragmentx_capture
  PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/include>
    $<INSTALL_INTERFACE:include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(fragmentx_capture
  PUBLIC  fragmentx_compile_options Threads::Threads
  PRIVATE fragmentx_runtime
)

set_target_properties(fragmentx_capture PROPERTIES
  POSITION_INDEPENDENT_CODE ON
)

# Native process-capture shim. Each OS uses its own loader/interposition API.
if(FRAGMENTX_INTERPOSITION AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
  set(FRAGMENTX_INTERPOSITION_SOURCE interposition.cpp)
elseif(FRAGMENTX_INTERPOSITION AND CMAKE_SYSTEM_NAME STREQUAL "Darwin")
  set(FRAGMENTX_INTERPOSITION_SOURCE interposition_macos.cpp)
endif()

if(FRAGMENTX_INTERPOSITION_SOURCE)
  add_library(fragmentx_interpose SHARED ${FRAGMENTX_INTERPOSITION_SOURCE})
  target_include_directories(fragmentx_interpose PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_BINARY_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}
  )
  target_link_libraries(fragmentx_interpose PRIVATE fragmentx_compile_options Threads::Threads ${CMAKE_DL_LIBS})
  target_compile_features(fragmentx_interpose PRIVATE cxx_std_20)
  set_target_properties(fragmentx_interpose PROPERTIES
    OUTPUT_NAME "fragmentx_interpose"
    VERSION ${PROJECT_VERSION}
    SOVERSION 0
  )
  install(TARGETS fragmentx_interpose
    LIBRARY DESTINATION lib
  )
elseif(FRAGMENTX_INTERPOSITION)
  message(STATUS "FragmentX process capture has no native backend for ${CMAKE_SYSTEM_NAME}; instrumented API capture remains available")
endif()

