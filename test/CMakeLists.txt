# Unit tests. Each executable is one translation unit using the in-tree TestHarness.hpp.
# All link abt::sim (which transitively exposes protocol/lob/util headers) + fmt.

foreach(t IN ITEMS itch ouch lob venue mirror framing session gen socketio net transport flathashmap divby scan)
    add_executable(${t}_test ${t}_test.cpp)
    target_include_directories(${t}_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(${t}_test PRIVATE abt::sim abt::flags fmt::fmt)
    add_test(NAME ${t}_test COMMAND ${t}_test)
endforeach()

# config_test additionally links the toml++ loader library.
add_executable(config_test config_test.cpp)
target_include_directories(config_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(config_test PRIVATE abt::config abt::flags fmt::fmt)
add_test(NAME config_test COMMAND config_test)

add_executable(dutconfig_test dutconfig_test.cpp)
target_include_directories(dutconfig_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(dutconfig_test PRIVATE abt::config abt::flags fmt::fmt)
add_test(NAME dutconfig_test COMMAND dutconfig_test)

# quoter_test exercises the market-making quoter strategy.
add_executable(quoter_test quoter_test.cpp)
target_include_directories(quoter_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(quoter_test PRIVATE abt::dut abt::flags fmt::fmt)
add_test(NAME quoter_test COMMAND quoter_test)

add_executable(arena_test arena_test.cpp)
target_include_directories(arena_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(arena_test PRIVATE abt::util abt::flags fmt::fmt)
add_test(NAME arena_test COMMAND arena_test)

add_executable(uniquefd_test uniquefd_test.cpp)
target_include_directories(uniquefd_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(uniquefd_test PRIVATE abt::util abt::flags fmt::fmt)
add_test(NAME uniquefd_test COMMAND uniquefd_test)

# histogram_test smoke-tests the vendored HdrHistogram binding.
add_executable(histogram_test histogram_test.cpp)
target_include_directories(histogram_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(histogram_test PRIVATE abt::dut abt::flags fmt::fmt)
add_test(NAME histogram_test COMMAND histogram_test)

# dutbook_test exercises the DUT-side feed builder; dutsession_test drives the whole t2t loop.
add_executable(dutbook_test dutbook_test.cpp)
target_include_directories(dutbook_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(dutbook_test PRIVATE abt::dut abt::flags fmt::fmt)
add_test(NAME dutbook_test COMMAND dutbook_test)

add_executable(dutsession_test dutsession_test.cpp)
target_include_directories(dutsession_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(dutsession_test PRIVATE abt::dut abt::flags fmt::fmt)
add_test(NAME dutsession_test COMMAND dutsession_test)

foreach(t IN ITEMS latency sequence oms)
    add_executable(${t}_test ${t}_test.cpp)
    target_include_directories(${t}_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(${t}_test PRIVATE abt::dut abt::flags fmt::fmt)
    add_test(NAME ${t}_test COMMAND ${t}_test)
endforeach()

# dut_socket_test drives the sim and the DUT against each other over real kernel sockets.
add_executable(dut_socket_test dut_socket_test.cpp)
target_include_directories(dut_socket_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(dut_socket_test PRIVATE abt::dut abt::sim abt::flags fmt::fmt)
add_test(NAME dut_socket_test COMMAND dut_socket_test)

# dutproc_test measures the in-process RX->decision compute latency (no hardware timestamps).
if(ABT_SW_TIMING)
    add_executable(dutproc_test dutproc_test.cpp)
    target_include_directories(dutproc_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(dutproc_test PRIVATE abt::dut abt::flags fmt::fmt)
    add_test(NAME dutproc_test COMMAND dutproc_test)
endif()

add_executable(itchfile_test itchfile_test.cpp)
target_include_directories(itchfile_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(itchfile_test PRIVATE abt::replay abt::sim abt::flags fmt::fmt ZLIB::ZLIB)
add_test(NAME itchfile_test COMMAND itchfile_test)

add_executable(replay_test replay_test.cpp)
target_include_directories(replay_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(replay_test PRIVATE abt::replay abt::sim abt::flags fmt::fmt)
add_test(NAME replay_test COMMAND replay_test)
