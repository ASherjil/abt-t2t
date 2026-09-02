# Library targets. Headers and sources live side by side per module; the include root is src/, so
# every include is spelled "t2t/<module>/<Name>.hpp" (vendored code as "third_party/<lib>/...").
set(T2T_INCLUDE_ROOT ${PROJECT_SOURCE_DIR}/src ${PROJECT_BINARY_DIR}/generated)

# ---------------------------------------------------------------------------
# abt_util : clock/TSC, affinity, flat hash map, HdrHistogram binding.
# ---------------------------------------------------------------------------
add_library(abt_util STATIC
    util/Histogram.cpp
    util/MemLock.cpp
    util/UniqueFd.cpp)
add_library(abt::util ALIAS abt_util)
target_include_directories(abt_util PUBLIC ${T2T_INCLUDE_ROOT})
target_compile_features(abt_util PUBLIC cxx_std_20)
target_link_libraries(abt_util
    PUBLIC Threads::Threads
    PRIVATE abt_flags hdr_histogram_static)

# ---------------------------------------------------------------------------
# abt_protocol : ITCH 5.0 / OUCH 5.0 / MoldUDP64 / SoupBinTCP wire structs and framers.
# ---------------------------------------------------------------------------
add_library(abt_protocol STATIC
    protocol/MoldUdp64.cpp
    protocol/UdpFramer.cpp)
add_library(abt::protocol ALIAS abt_protocol)
target_include_directories(abt_protocol PUBLIC ${T2T_INCLUDE_ROOT})
target_compile_features(abt_protocol PUBLIC cxx_std_20)
target_link_libraries(abt_protocol PRIVATE abt_flags)

# ---------------------------------------------------------------------------
# abt_lob : header-only limit order book / matching engine.
# ---------------------------------------------------------------------------
add_library(abt_lob INTERFACE)
add_library(abt::lob ALIAS abt_lob)
target_include_directories(abt_lob INTERFACE ${T2T_INCLUDE_ROOT})
target_compile_features(abt_lob INTERFACE cxx_std_20)

# ---------------------------------------------------------------------------
# abt_dut : DUT-side feed builder, OMS, quoter, latency recorder.
# ---------------------------------------------------------------------------
add_library(abt_dut STATIC
    dut/BookBuilder.cpp
    dut/LatencyRecorder.cpp
    dut/OrderManager.cpp
    dut/QuoterStrategy.cpp
    dut/SequenceTracker.cpp)
add_library(abt::dut ALIAS abt_dut)
target_link_libraries(abt_dut
    PUBLIC abt_protocol abt_lob abt_util SPSCQueue::SPSCQueue Threads::Threads
    PRIVATE abt_flags fmt::fmt)

# ---------------------------------------------------------------------------
# abt_replay : NASDAQ ITCH file replay (BinaryFILE format, gz-transparent via zlib).
# ---------------------------------------------------------------------------
add_library(abt_replay STATIC
    replay/BookReplay.cpp
    replay/ItchFile.cpp
    replay/SymbolFilter.cpp)
add_library(abt::replay ALIAS abt_replay)
target_link_libraries(abt_replay
    PUBLIC abt_dut abt_lob
    PRIVATE abt_flags ZLIB::ZLIB)

# ---------------------------------------------------------------------------
# abt_sim : header-only venue glue (OUCH <-> matching engine <-> ITCH/OUCH) + market replay.
# ---------------------------------------------------------------------------
add_library(abt_sim INTERFACE)
add_library(abt::sim ALIAS abt_sim)
target_link_libraries(abt_sim INTERFACE abt_protocol abt_lob abt_replay fmt::fmt)

# ---------------------------------------------------------------------------
# abt_config : TOML-driven runtime configuration loaders (toml++) for sim and DUT.
# ---------------------------------------------------------------------------
add_library(abt_config STATIC
    config/NetParse.cpp
    dut/DutConfigLoad.cpp
    sim/ConfigLoad.cpp)
add_library(abt::config ALIAS abt_config)
target_link_libraries(abt_config
    PUBLIC abt_sim abt_dut
    PRIVATE tomlplusplus::tomlplusplus abt_flags fmt::fmt)
