add_executable(exchange_sim exchange_sim.cpp)
target_include_directories(exchange_sim PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/backends/${ABT_SIM_BACKEND})
target_link_libraries(exchange_sim PRIVATE abt::config abt::flags fmt::fmt)
target_compile_definitions(exchange_sim PRIVATE
    ABT_CONFIG_PATH="${CMAKE_SOURCE_DIR}/config/exchange_sim.toml")
if(NOT ABT_SIM_BACKEND STREQUAL "socket")
    target_link_libraries(exchange_sim PRIVATE abtrda3)
endif()

add_executable(dut dut.cpp)
target_link_libraries(dut PRIVATE abt::config abt::dut abt::flags fmt::fmt)
target_compile_definitions(dut PRIVATE
    ABT_DUT_CONFIG_PATH="${CMAKE_SOURCE_DIR}/config/dut.toml")
