function(abt_add_exchange_sim backend)
    if(backend STREQUAL "socket")
        set(target exchange_sim)
    else()
        set(target exchange_sim_${backend})
    endif()
    add_executable(${target} exchange_sim.cpp)
    target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/backends/${backend})
    target_link_libraries(${target} PRIVATE abt::config abt::flags fmt::fmt)
    target_compile_definitions(${target} PRIVATE
        ABT_CONFIG_PATH="${CMAKE_SOURCE_DIR}/config/exchange_sim.toml")
    if(NOT backend STREQUAL "socket")
        target_link_libraries(${target} PRIVATE abtrda3)
    endif()
endfunction()

foreach(backend IN LISTS ABT_SIM_BACKENDS)
    abt_add_exchange_sim(${backend})
endforeach()

add_executable(dut dut.cpp)
target_link_libraries(dut PRIVATE abt::config abt::dut abt::flags fmt::fmt)
target_compile_definitions(dut PRIVATE
    ABT_DUT_CONFIG_PATH="${CMAKE_SOURCE_DIR}/config/dut.toml")
