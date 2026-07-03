# Unit tests. Each executable is one translation unit using the tiny in-tree
# TestHarness.hpp (no external test-framework dependency yet).

add_executable(itch_test itch_test.cpp)
target_include_directories(itch_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(itch_test PRIVATE abt::protocol abt::flags)
add_test(NAME itch_test COMMAND itch_test)

add_executable(ouch_test ouch_test.cpp)
target_include_directories(ouch_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ouch_test PRIVATE abt::protocol abt::flags)
add_test(NAME ouch_test COMMAND ouch_test)

add_executable(lob_test lob_test.cpp)
target_include_directories(lob_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(lob_test PRIVATE abt::lob abt::flags)
add_test(NAME lob_test COMMAND lob_test)

add_executable(venue_test venue_test.cpp)
target_include_directories(venue_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(venue_test PRIVATE abt::sim abt::flags)
add_test(NAME venue_test COMMAND venue_test)

add_executable(framing_test framing_test.cpp)
target_include_directories(framing_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(framing_test PRIVATE abt::protocol abt::flags)
add_test(NAME framing_test COMMAND framing_test)

add_executable(session_test session_test.cpp)
target_include_directories(session_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(session_test PRIVATE abt::sim abt::flags)
add_test(NAME session_test COMMAND session_test)

add_executable(gen_test gen_test.cpp)
target_include_directories(gen_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(gen_test PRIVATE abt::sim abt::flags)
add_test(NAME gen_test COMMAND gen_test)
