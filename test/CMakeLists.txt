# Unit tests. Each executable is one translation unit using the in-tree TestHarness.hpp.
# All link abt::sim (which transitively exposes protocol/lob/util headers) + fmt.

foreach(t IN ITEMS itch ouch lob venue framing session gen socketio net dpdk)
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

# dutbook_test exercises the DUT-side feed builder; dutsession_test drives the whole t2t loop.
add_executable(dutbook_test dutbook_test.cpp)
target_include_directories(dutbook_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(dutbook_test PRIVATE abt::dut abt::flags fmt::fmt)
add_test(NAME dutbook_test COMMAND dutbook_test)

add_executable(dutsession_test dutsession_test.cpp)
target_include_directories(dutsession_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(dutsession_test PRIVATE abt::dut abt::flags fmt::fmt)
add_test(NAME dutsession_test COMMAND dutsession_test)

# t2t_test exercises the tick-to-trade latency recorder.
add_executable(t2t_test t2t_test.cpp)
target_include_directories(t2t_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(t2t_test PRIVATE abt::dut abt::flags fmt::fmt)
add_test(NAME t2t_test COMMAND t2t_test)

# dut_socket_test drives the sim and the DUT against each other over real kernel sockets.
add_executable(dut_socket_test dut_socket_test.cpp)
target_include_directories(dut_socket_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(dut_socket_test PRIVATE abt::dut abt::sim abt::flags fmt::fmt)
add_test(NAME dut_socket_test COMMAND dut_socket_test)
