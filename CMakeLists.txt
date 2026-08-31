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

find_package(Threads REQUIRED)

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

# Exchange-sim backend tier, fixed at configure time. One binary per backend is built from the
# single apps/exchange_sim.cpp (apps/backends/<name>/SimBackend.hpp is the only place a concrete
# transport type is named):
#   socket -> exchange_sim                                   (builds anywhere)
#   dpdk   -> + exchange_sim_dpdk                            (fetches ABTRDA3; needs libdpdk)
#   verbs  -> + exchange_sim_dpdk + exchange_sim_verbs       (needs libdpdk + rdma-core)
# ABTRDA3 always also builds AF_XDP (libxdp/libbpf + clang for the BPF object).
set(ABT_SIM_BACKEND "socket" CACHE STRING "exchange_sim backend tier: socket | dpdk | verbs")
set_property(CACHE ABT_SIM_BACKEND PROPERTY STRINGS socket dpdk verbs)
if(NOT ABT_SIM_BACKEND MATCHES "^(socket|dpdk|verbs)$")
    message(FATAL_ERROR "ABT_SIM_BACKEND must be socket, dpdk or verbs (got '${ABT_SIM_BACKEND}')")
endif()
set(ABT_SIM_BACKENDS socket)
if(ABT_SIM_BACKEND STREQUAL "dpdk" OR ABT_SIM_BACKEND STREQUAL "verbs")
    list(APPEND ABT_SIM_BACKENDS dpdk)
    set(ABTRDA3_WITH_DPDK ON CACHE BOOL "" FORCE)
endif()
if(ABT_SIM_BACKEND STREQUAL "verbs")
    list(APPEND ABT_SIM_BACKENDS verbs)
    set(ABTRDA3_WITH_VERBS ON CACHE BOOL "" FORCE)
endif()
if(NOT ABT_SIM_BACKEND STREQUAL "socket")
    set(ABTRDA3_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    # Override with -DFETCHCONTENT_SOURCE_DIR_ABTRDA3=<local checkout> to build against a working copy.
    FetchContent_Declare(abtrda3
        GIT_REPOSITORY https://github.com/ASherjil/ABTRDA3.git
        GIT_TAG master)
    FetchContent_MakeAvailable(abtrda3)
    get_target_property(_abtrda3_inc abtrda3 INTERFACE_INCLUDE_DIRECTORIES)
    set_target_properties(abtrda3 PROPERTIES INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${_abtrda3_inc}")
endif()
message(STATUS "abt-t2t: exchange_sim backends = ${ABT_SIM_BACKENDS}")

# ---------------------------------------------------------------------------
# abt_protocol : header-only wire protocol library (ITCH 5.0 / OUCH 5.0).
# Everything is zero-cost overlay structs, so this is an INTERFACE target.
# ---------------------------------------------------------------------------
add_library(abt_protocol STATIC
    src/protocol/MoldUdp64.cpp
    src/protocol/UdpFramer.cpp)
add_library(abt::protocol ALIAS abt_protocol)
target_include_directories(abt_protocol PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(abt_protocol PUBLIC cxx_std_20)
target_link_libraries(abt_protocol PRIVATE abt_flags)

# ---------------------------------------------------------------------------
# abt_lob : header-only limit order book / matching engine.
# ---------------------------------------------------------------------------
add_library(abt_lob INTERFACE)
add_library(abt::lob ALIAS abt_lob)
target_include_directories(abt_lob INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(abt_lob INTERFACE cxx_std_20)

# ---------------------------------------------------------------------------
# abt_sim : header-only venue glue (OUCH <-> matching engine <-> ITCH/OUCH).
# ---------------------------------------------------------------------------
add_library(abt_sim INTERFACE)
add_library(abt::sim ALIAS abt_sim)
target_link_libraries(abt_sim INTERFACE abt_protocol abt_lob fmt::fmt)

# ---------------------------------------------------------------------------
# abt_config : TOML-driven runtime configuration loader (toml++).
# ---------------------------------------------------------------------------
add_library(abt_config STATIC
    src/config/NetParse.cpp
    src/dut/DutConfigLoad.cpp
    src/sim/ConfigLoad.cpp)
add_library(abt::config ALIAS abt_config)
target_link_libraries(abt_config
    PUBLIC abt_sim abt_dut
    PRIVATE tomlplusplus::tomlplusplus abt_flags fmt::fmt)

# ---------------------------------------------------------------------------
# abt_dut : DUT-side latency-critical order book (ITCH feed builder; t2t harness to come).
# ---------------------------------------------------------------------------
add_library(abt_dut STATIC
    src/dut/BookBuilder.cpp
    src/dut/LatencyRecorder.cpp
    src/dut/OrderManager.cpp
    src/dut/QuoterStrategy.cpp
    src/dut/SequenceTracker.cpp
    src/util/Histogram.cpp)
add_library(abt::dut ALIAS abt_dut)
target_link_libraries(abt_dut
    PUBLIC abt_protocol abt_lob SPSCQueue::SPSCQueue Threads::Threads
    PRIVATE abt_flags fmt::fmt hdr_histogram_static)

# ---------------------------------------------------------------------------
# abt_replay : NASDAQ ITCH file replay (BinaryFILE format, gz-transparent via zlib).
# ---------------------------------------------------------------------------
find_package(ZLIB REQUIRED)
add_library(abt_replay STATIC
    src/replay/BookReplay.cpp
    src/replay/ItchFile.cpp)
add_library(abt::replay ALIAS abt_replay)
target_link_libraries(abt_replay
    PUBLIC abt_dut abt_lob
    PRIVATE abt_flags ZLIB::ZLIB)

add_subdirectory(apps)

enable_testing()
add_subdirectory(test)
