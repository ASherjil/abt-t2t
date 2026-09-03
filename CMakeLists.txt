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

# Link-time optimisation for optimised builds: the DUT hot path spans several translation units
# (BookTable, OrderManager, QuoterStrategy, the framer) and without LTO every one of them is a real
# call per packet.
if(CMAKE_BUILD_TYPE STREQUAL "Release" OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
    include(CheckIPOSupported)
    check_ipo_supported(RESULT ABT_IPO_OK OUTPUT ABT_IPO_MSG LANGUAGES CXX)
    if(ABT_IPO_OK)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
        message(STATUS "abt-t2t: LTO enabled")
    else()
        message(STATUS "abt-t2t: LTO not available (${ABT_IPO_MSG})")
    endif()
endif()

find_package(Threads REQUIRED)

# ccache when available: dependency and project objects survive a clean build.
find_program(ABT_CCACHE ccache)
if(ABT_CCACHE AND NOT CMAKE_CXX_COMPILER_LAUNCHER)
    set(CMAKE_CXX_COMPILER_LAUNCHER "${ABT_CCACHE}")
    set(CMAKE_C_COMPILER_LAUNCHER "${ABT_CCACHE}")
    message(STATUS "abt-t2t: using ccache (${ABT_CCACHE})")
endif()

set(FETCHCONTENT_UPDATES_DISCONNECTED ON CACHE BOOL "Never re-fetch already-populated dependencies")
include(FetchContent)

# {fmt} — fetched (not find_package) at the same tag ABTRDA3 declares, so both projects share
# one fmt::fmt target when ABTRDA3 is pulled in as a subproject (FetchContent dedups by name).
set(FMT_INSTALL OFF CACHE BOOL "" FORCE)
set(FMT_TEST    OFF CACHE BOOL "" FORCE)
set(FMT_DOC     OFF CACHE BOOL "" FORCE)
FetchContent_Declare(fmt
    GIT_REPOSITORY https://github.com/fmtlib/fmt.git
    GIT_TAG 12.1.0)
FetchContent_MakeAvailable(fmt)

# TOML config (toml++) — the exchange-sim binary reads config/exchange_sim.toml at runtime.
FetchContent_Declare(tomlplusplus
    GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
    GIT_TAG v3.4.0)
FetchContent_MakeAvailable(tomlplusplus)

# Lock-free SPSC ring (Eric Rigtorp) — decouples the latency-critical thread from the histogram
# consumer thread in the tick-to-trade measurement pipeline. Header-only.
FetchContent_Declare(spscqueue
    GIT_REPOSITORY https://github.com/rigtorp/SPSCQueue.git
    GIT_TAG v1.1)
FetchContent_MakeAvailable(spscqueue)
get_target_property(_spsc_inc SPSCQueue INTERFACE_INCLUDE_DIRECTORIES)
set_target_properties(SPSCQueue PROPERTIES INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${_spsc_inc}")

# HdrHistogram (official C library) — nanosecond-resolution latency histogram, recorded off the
# hot path in the consumer thread. Same choice as ABTRDA3.
set(HDR_HISTOGRAM_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
set(HDR_HISTOGRAM_BUILD_SHARED OFF CACHE BOOL "" FORCE)
FetchContent_Declare(hdrhistogram
    GIT_REPOSITORY https://github.com/HdrHistogram/HdrHistogram_c.git
    GIT_TAG 0.11.10)
FetchContent_MakeAvailable(hdrhistogram)

# Backend tier (sim + DUT), fixed at configure time. One binary per backend is built from the
# single src/apps/exchange_sim.cpp (src/apps/backends/<name>/Backend.hpp is the only place a concrete
# transport type is named):
#   socket -> exchange_sim, dut                              (builds anywhere)
#   dpdk   -> + exchange_sim_dpdk, dut_dpdk                  (fetches ABTRDA3; needs libdpdk)
#   verbs  -> + exchange_sim_verbs, dut_verbs                (needs libdpdk + rdma-core)
# ABTRDA3 always also builds AF_XDP (libxdp/libbpf + clang for the BPF object).
set(ABT_BACKEND "socket" CACHE STRING "sim+dut backend tier: socket | dpdk | verbs")
set_property(CACHE ABT_BACKEND PROPERTY STRINGS socket dpdk verbs)
if(NOT ABT_BACKEND MATCHES "^(socket|dpdk|verbs)$")
    message(FATAL_ERROR "ABT_BACKEND must be socket, dpdk or verbs (got '${ABT_BACKEND}')")
endif()
set(ABT_BACKENDS socket)
if(ABT_BACKEND STREQUAL "dpdk" OR ABT_BACKEND STREQUAL "verbs")
    list(APPEND ABT_BACKENDS dpdk)
    set(ABTRDA3_WITH_DPDK ON CACHE BOOL "" FORCE)
endif()
if(ABT_BACKEND STREQUAL "verbs")
    list(APPEND ABT_BACKENDS verbs)
    set(ABTRDA3_WITH_VERBS ON CACHE BOOL "" FORCE)
endif()
if(NOT ABT_BACKEND STREQUAL "socket")
    set(ABTRDA3_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    # Tracks master: cloned once, never re-fetched (FETCHCONTENT_UPDATES_DISCONNECTED). Clean the build
    # dir to pick up a newer ABTRDA3. -DFETCHCONTENT_SOURCE_DIR_ABTRDA3=<checkout> builds a working copy.
    FetchContent_Declare(abtrda3
        GIT_REPOSITORY https://github.com/ASherjil/ABTRDA3.git
        GIT_TAG master)
    FetchContent_MakeAvailable(abtrda3)
    get_target_property(_abtrda3_inc abtrda3 INTERFACE_INCLUDE_DIRECTORIES)
    set_target_properties(abtrda3 PROPERTIES INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${_abtrda3_inc}")
endif()
message(STATUS "abt-t2t: backends = ${ABT_BACKENDS}")

# Software timing instruments (rdtscp-based proc / t2t_sw recorders in the DUT hot loop). OFF for
# hardware-timestamp campaigns so the hot loop carries zero rdtscp; exposed to C++ as the constexpr
# abt::build::kSwTiming in the generated t2t/BuildConfig.hpp (no #ifdef in code).
option(ABT_SW_TIMING "software timing instruments (rdtscp proc/t2t_sw) in the DUT hot loop" ON)
if(ABT_SW_TIMING)
    set(ABT_SW_TIMING_VALUE true)
else()
    set(ABT_SW_TIMING_VALUE false)
endif()
configure_file(src/t2t/BuildConfig.hpp.in ${PROJECT_BINARY_DIR}/generated/t2t/BuildConfig.hpp @ONLY)
message(STATUS "abt-t2t: sw timing = ${ABT_SW_TIMING}")

find_package(ZLIB REQUIRED)

add_subdirectory(src/t2t)
add_subdirectory(src/apps)

enable_testing()
add_subdirectory(test)
