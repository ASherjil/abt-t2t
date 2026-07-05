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

find_package(fmt REQUIRED)

# TOML config (toml++) — the exchange-sim binary reads config/exchange_sim.toml at runtime.
include(FetchContent)
FetchContent_Declare(tomlplusplus
    GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
    GIT_TAG v3.4.0)
FetchContent_MakeAvailable(tomlplusplus)

# Kernel-bypass transports (DPDK / Verbs / AF_XDP / ef_vi) live in the sibling ABTRDA3
# repo, which owns all NIC/EAL/hugepage init. OFF by default: enabling it runs ABTRDA3's
# CMake, which pkg-config-requires DPDK/rdma-core/libxdp. Turn on for the DPDK datapath.
option(ABT_WITH_TRANSPORT "Fetch ABTRDA3 kernel-bypass transports" OFF)
if(ABT_WITH_TRANSPORT)
    include(FetchContent)
    FetchContent_Declare(abtrda3
        GIT_REPOSITORY https://github.com/ASherjil/ABTRDA3.git
        GIT_TAG main)
    FetchContent_MakeAvailable(abtrda3)
endif()

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
add_library(abt_config STATIC src/sim/ConfigLoad.cpp)
add_library(abt::config ALIAS abt_config)
target_link_libraries(abt_config PUBLIC abt_sim PRIVATE tomlplusplus::tomlplusplus abt_flags)

add_subdirectory(apps)

enable_testing()
add_subdirectory(test)
