function(abt_add_backend_apps backend)
    if(backend STREQUAL "socket")
        set(sim exchange_sim)
        set(dut dut)
    else()
        set(sim exchange_sim_${backend})
        set(dut dut_${backend})
    endif()

    add_executable(${sim} exchange_sim.cpp)
    target_include_directories(${sim} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/backends/${backend})
    target_link_libraries(${sim} PRIVATE abt::config abt::flags fmt::fmt)
    target_compile_definitions(${sim} PRIVATE
        ABT_CONFIG_PATH="${CMAKE_SOURCE_DIR}/config/exchange_sim.toml")

    add_executable(${dut} dut.cpp)
    target_include_directories(${dut} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/backends/${backend})
    target_link_libraries(${dut} PRIVATE abt::config abt::dut abt::flags fmt::fmt)
    target_compile_definitions(${dut} PRIVATE
        ABT_DUT_CONFIG_PATH="${CMAKE_SOURCE_DIR}/config/dut.toml")

    if(NOT backend STREQUAL "socket")
        target_link_libraries(${sim} PRIVATE abtrda3)
        target_link_libraries(${dut} PRIVATE abtrda3)
    endif()
endfunction()

foreach(backend IN LISTS ABT_BACKENDS)
    abt_add_backend_apps(${backend})
endforeach()

add_executable(itch_replay itch_replay.cpp)
target_link_libraries(itch_replay PRIVATE abt::replay abt::flags fmt::fmt)
