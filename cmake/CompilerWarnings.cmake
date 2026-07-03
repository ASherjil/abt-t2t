# Shared warning / architecture / optimisation flags, exposed as an INTERFACE
# target so every binary opts in with `target_link_libraries(<t> PRIVATE abt_flags)`.
# Mirrors the flag set used in the sibling abtrda3 transport benchmark repo.

add_library(abt_flags INTERFACE)
add_library(abt::flags ALIAS abt_flags)

target_compile_features(abt_flags INTERFACE cxx_std_20)

target_compile_options(abt_flags INTERFACE
    -Wall -Wextra -Wpedantic
    -Werror=return-type
    -Wconversion -Wsign-conversion
    -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Woverloaded-virtual
    -Wnull-dereference -Wdouble-promotion
    # Release: tuned for the i9-11900K rig (AVX2 / x86-64-v3 baseline).
    $<$<CONFIG:Release>:-O3>
    $<$<CONFIG:Release>:-march=x86-64-v3>
    $<$<CONFIG:RelWithDebInfo>:-O2>
    $<$<CONFIG:RelWithDebInfo>:-march=x86-64-v3>
    $<$<CONFIG:RelWithDebInfo>:-g>
    # Debug: sanitizers on, like abtrda3's debug preset.
    $<$<CONFIG:Debug>:-O0>
    $<$<CONFIG:Debug>:-g3>
    $<$<CONFIG:Debug>:-fsanitize=address,undefined>
)
target_link_options(abt_flags INTERFACE
    $<$<CONFIG:Debug>:-fsanitize=address,undefined>
)
