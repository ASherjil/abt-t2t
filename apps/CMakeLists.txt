# Runnable binaries.

add_executable(exchange_sim exchange_sim.cpp)
target_link_libraries(exchange_sim PRIVATE abt::config abt::flags fmt::fmt)
target_compile_definitions(exchange_sim PRIVATE
    ABT_CONFIG_PATH="${CMAKE_SOURCE_DIR}/config/exchange_sim.toml")
