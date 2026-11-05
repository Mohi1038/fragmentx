cmake_minimum_required(VERSION 3.21)

project(
  FragmentX
  VERSION 0.1.0
  DESCRIPTION "Adaptive Memory Allocation and Heap Optimization for C/C++"
  HOMEPAGE_URL "https://github.com/fragmentx/fragmentx"
  LANGUAGES CXX
)

# ─── C++ Standard ────────────────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ─── Build type default ──────────────────────────────────────────────────────
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  message(STATUS "FragmentX: defaulting build type to RelWithDebInfo")
  set(CMAKE_BUILD_TYPE "RelWithDebInfo" CACHE STRING "Build type" FORCE)
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
    "Debug" "Release" "RelWithDebInfo" "MinSizeRel")
endif()

# ─── Options ─────────────────────────────────────────────────────────────────
option(FRAGMENTX_BUILD_TESTS       "Build the test suite"            OFF)
option(FRAGMENTX_BUILD_BENCHMARKS  "Build the benchmark suite"       OFF)
option(FRAGMENTX_BUILD_EXAMPLES    "Build example programs"          ON)
option(FRAGMENTX_ENABLE_ASAN       "Enable AddressSanitizer"         OFF)
option(FRAGMENTX_ENABLE_TSAN       "Enable ThreadSanitizer"          OFF)
option(FRAGMENTX_ENABLE_UBSAN      "Enable UndefinedBehaviorSanitizer" OFF)
option(FRAGMENTX_INSTALL           "Generate install targets"        ON)
option(FRAGMENTX_INTERPOSITION     "Build the native process-capture shim for supported platforms" ON)

# ─── Compiler & linker flags ─────────────────────────────────────────────────
add_library(fragmentx_compile_options INTERFACE)

target_compile_options(fragmentx_compile_options INTERFACE
  $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:
    -Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor
    -Wold-style-cast -Wcast-align -Wunused -Woverloaded-virtual
    -Wconversion -Wsign-conversion -Wnull-dereference
    -Wformat=2 -Wimplicit-fallthrough
  >
  $<$<CXX_COMPILER_ID:MSVC>:
    /W4 /WX /permissive-
  >
  $<$<CONFIG:Release>:$<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-O3 -DNDEBUG>>
  $<$<CONFIG:RelWithDebInfo>:$<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-O2 -g -DNDEBUG>>
  $<$<CONFIG:Debug>:$<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-O0 -g -DFRAGMENTX_DEBUG>>
)

# ─── Sanitizers ──────────────────────────────────────────────────────────────
if(FRAGMENTX_ENABLE_ASAN AND FRAGMENTX_ENABLE_TSAN)
  message(FATAL_ERROR "ASan and TSan cannot be enabled simultaneously")
endif()

if(FRAGMENTX_ENABLE_ASAN)
  target_compile_options(fragmentx_compile_options INTERFACE
    -fsanitize=address,undefined -fno-omit-frame-pointer -fno-optimize-sibling-calls)
  target_link_options(fragmentx_compile_options INTERFACE
    -fsanitize=address,undefined)
elseif(FRAGMENTX_ENABLE_UBSAN)
  target_compile_options(fragmentx_compile_options INTERFACE
    -fsanitize=undefined -fno-omit-frame-pointer)
  target_link_options(fragmentx_compile_options INTERFACE
    -fsanitize=undefined)
endif()

if(FRAGMENTX_ENABLE_TSAN)
  target_compile_options(fragmentx_compile_options INTERFACE
    -fsanitize=thread -fno-omit-frame-pointer)
  target_link_options(fragmentx_compile_options INTERFACE
    -fsanitize=thread)
endif()

# ─── Platform detection ───────────────────────────────────────────────────────
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  set(FRAGMENTX_PLATFORM_LINUX ON)
  target_compile_definitions(fragmentx_compile_options INTERFACE FRAGMENTX_PLATFORM_LINUX)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
  set(FRAGMENTX_PLATFORM_MACOS ON)
  target_compile_definitions(fragmentx_compile_options INTERFACE FRAGMENTX_PLATFORM_MACOS)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
  set(FRAGMENTX_PLATFORM_WINDOWS ON)
  target_compile_definitions(fragmentx_compile_options INTERFACE FRAGMENTX_PLATFORM_WINDOWS)
endif()

# ─── Threads ─────────────────────────────────────────────────────────────────
find_package(Threads REQUIRED)

# ─── GoogleTest (via FetchContent) ────────────────────────────────────────────
if(FRAGMENTX_BUILD_TESTS)
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
  enable_testing()
  include(GoogleTest)
endif()

# ─── Google Benchmark (via FetchContent) ─────────────────────────────────────
if(FRAGMENTX_BUILD_BENCHMARKS)
  include(FetchContent)
  FetchContent_Declare(
    benchmark
    GIT_REPOSITORY https://github.com/google/benchmark.git
    GIT_TAG        v1.9.1
    GIT_SHALLOW    TRUE
  )
  set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
  set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(benchmark)
endif()

# ─── Version header generation ────────────────────────────────────────────────
configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake/fragmentx_version.hpp.in"
  "${CMAKE_CURRENT_BINARY_DIR}/include/fragmentx/version.hpp"
  @ONLY
)

# ─── Subdirectories ───────────────────────────────────────────────────────────
add_subdirectory(src)

if(FRAGMENTX_BUILD_TESTS)
  add_subdirectory(tests)
endif()

if(FRAGMENTX_BUILD_BENCHMARKS)
  add_subdirectory(benchmarks)
endif()

if(FRAGMENTX_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()

# ─── Install ─────────────────────────────────────────────────────────────────
if(FRAGMENTX_INSTALL)
  include(GNUInstallDirs)
  include(CMakePackageConfigHelpers)

  install(DIRECTORY include/fragmentx
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING PATTERN "*.hpp"
  )

  install(FILES "${CMAKE_CURRENT_BINARY_DIR}/include/fragmentx/version.hpp"
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/fragmentx
  )

  install(TARGETS
      fragmentx_runtime
      fragmentx_capture
      fragmentx_analysis
      fragmentx_optimizer
      fragmentx_compile_options
    EXPORT FragmentXTargets
    LIBRARY  DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE  DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME  DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  )

  install(TARGETS fragmentx
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  )

  write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/FragmentXConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
  )

  configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/FragmentXConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/FragmentXConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/FragmentX
  )

  install(EXPORT FragmentXTargets
    FILE FragmentXTargets.cmake
    NAMESPACE FragmentX::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/FragmentX
  )

  install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/FragmentXConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/FragmentXConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/FragmentX
  )
endif()

# ─── Summary ─────────────────────────────────────────────────────────────────
message(STATUS "")
message(STATUS "  ╔═══════════════════════════════════════╗")
message(STATUS "  ║          FragmentX v${PROJECT_VERSION}             ║")
message(STATUS "  ╚═══════════════════════════════════════╝")
message(STATUS "")
message(STATUS "  Build type        : ${CMAKE_BUILD_TYPE}")
message(STATUS "  Platform          : ${CMAKE_SYSTEM_NAME}")
message(STATUS "  Compiler          : ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "  C++ standard      : ${CMAKE_CXX_STANDARD}")
message(STATUS "  Build tests       : ${FRAGMENTX_BUILD_TESTS}")
message(STATUS "  Build benchmarks  : ${FRAGMENTX_BUILD_BENCHMARKS}")
message(STATUS "  Build examples    : ${FRAGMENTX_BUILD_EXAMPLES}")
message(STATUS "  ASan              : ${FRAGMENTX_ENABLE_ASAN}")
message(STATUS "  TSan              : ${FRAGMENTX_ENABLE_TSAN}")
message(STATUS "  UBSan             : ${FRAGMENTX_ENABLE_UBSAN}")
message(STATUS "  Install           : ${FRAGMENTX_INSTALL}")
message(STATUS "")

