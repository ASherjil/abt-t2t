# Unit tests. Each executable is one translation unit using the in-tree TestHarness.hpp.
# All link abt::sim (which transitively exposes protocol/lob/util headers) + fmt.

foreach(t IN ITEMS itch ouch lob venue framing session gen socketio net dpdk)
    add_executable(${t}_test ${t}_test.cpp)
    target_include_directories(${t}_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(${t}_test PRIVATE abt::sim abt::flags fmt::fmt)
    add_test(NAME ${t}_test COMMAND ${t}_test)
endforeach()
