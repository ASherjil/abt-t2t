# Runnable binaries.

add_executable(exchange_sim exchange_sim.cpp)
target_link_libraries(exchange_sim PRIVATE abt::sim abt::flags)
