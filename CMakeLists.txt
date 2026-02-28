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

# ── Release flags for performance measurement (Phase 2) ──────────────────────
# Switch to Release when benchmarking: cmake -DCMAKE_BUILD_TYPE=Release ..
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()

set(CMAKE_CXX_FLAGS_DEBUG   "-g -O0 -Wall -Wextra -Wpedantic")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG -march=native")

# Phase 2: uncomment to enable AddressSanitizer for memory debugging
# set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -fsanitize=address,undefined")
# set(CMAKE_LINKER_FLAGS_DEBUG "-fsanitize=address,undefined")

include_directories(include)

# ── Main interactive CLI ──────────────────────────────────────────────────────
add_executable(lob src/main.cpp)

# ── Test suite ────────────────────────────────────────────────────────────────
add_executable(test_lob tests/test_lob.cpp)

# ── CTest integration ─────────────────────────────────────────────────────────
enable_testing()
add_test(NAME LOBTests COMMAND test_lob)
