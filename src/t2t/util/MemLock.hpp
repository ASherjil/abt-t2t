#pragma once

#include <cstddef>

namespace abt::util {

struct MemLockResult {
    bool locked = false;
    int  error  = 0;
};

struct PageFaults {
    long minor = 0;
    long major = 0;
};

[[nodiscard]] MemLockResult lockAndPrefaultMemory() noexcept;
[[nodiscard]] PageFaults    threadPageFaults() noexcept;

}   // namespace abt::util
