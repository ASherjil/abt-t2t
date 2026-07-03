# Unit tests. Each executable is one translation unit using the tiny in-tree
# TestHarness.hpp (no external test-framework dependency yet).

add_executable(itch_test itch_test.cpp)
target_include_directories(itch_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(itch_test PRIVATE abt::protocol abt::flags)
add_test(NAME itch_test COMMAND itch_test)
