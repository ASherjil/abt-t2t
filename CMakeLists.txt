cmake_minimum_required(VERSION 3.21)
project(abt_t2t
    VERSION 0.1.0
    DESCRIPTION "Ultra-low-latency ITCH/OUCH order book with hardware-timestamped tick-to-trade"
    LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Interface target carrying warning/arch/opt flags (mirrors abtrda3's abtrda3_flags).
include(cmake/CompilerWarnings.cmake)

# ---------------------------------------------------------------------------
# abt_protocol : header-only wire protocol library (ITCH 5.0 / OUCH 5.0).
# Everything is zero-cost overlay structs, so this is an INTERFACE target.
# ---------------------------------------------------------------------------
add_library(abt_protocol INTERFACE)
add_library(abt::protocol ALIAS abt_protocol)
target_include_directories(abt_protocol INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(abt_protocol INTERFACE cxx_std_20)

enable_testing()
add_subdirectory(test)
