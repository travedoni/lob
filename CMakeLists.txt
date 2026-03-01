cmake_minimum_required(VERSION 3.15)
project(LimitOrderBook CXX)

# After project(LimitOrderBook CXX)

# Find and set the correct SDK path
execute_process(COMMAND xcrun --show-sdk-path OUTPUT_VARIABLE CMAKE_OSX_SYSROOT OUTPUT_STRIP_TRAILING_WHITESPACE)
set(CMAKE_OSX_SYSROOT ${CMAKE_OSX_SYSROOT} CACHE PATH "macOS SDK path" FORCE)
message(STATUS "Using macOS SDK: ${CMAKE_OSX_SYSROOT}")

# Ensure CMake uses the correct compiler and SDK
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -isysroot ${CMAKE_OSX_SYSROOT}")

# Explicitly add libc++ include path
include_directories(${CMAKE_OSX_SYSROOT}/usr/include/c++/v1)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# ── Compiler flags ────────────────────────────────────────────────────────────
set(CMAKE_CXX_FLAGS_DEBUG   "-g -O0 -Wall -Wextra -Wpedantic")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG -march=native")

# Uncomment for memory debugging:
# set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -fsanitize=address,undefined")

include_directories(include)

# ── Phase 1: Interactive CLI ──────────────────────────────────────────────────
add_executable(lob src/main.cpp)

# ── Phase 1: Test suite ───────────────────────────────────────────────────────
add_executable(test_lob tests/test_lob.cpp)

# ── Phase 2: Benchmark ────────────────────────────────────────────────────────
# Always built with -O3 for accurate perf numbers regardless of build type
add_executable(benchmark src/benchmark.cpp)
target_compile_options(benchmark PRIVATE -O3 -march=native -DNDEBUG)

# ── CTest ─────────────────────────────────────────────────────────────────────
enable_testing()
add_test(NAME LOBTests COMMAND test_lob)
