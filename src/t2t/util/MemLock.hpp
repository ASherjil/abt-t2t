#pragma once

#include <cstddef>

namespace abt::util {

struct MemLockResult {
    bool locked = false;
    int  error  = 0;
};

struct ThreadCounters {
    long minorFaults         = 0;
    long majorFaults         = 0;
    long involuntarySwitches = 0;
    long voluntarySwitches   = 0;
};

struct ProcessMemory {
    std::size_t rssMb     = 0;
    std::size_t peakRssMb = 0;
    std::size_t hugetlbMb = 0;
};

[[nodiscard]] MemLockResult  lockAndPrefaultMemory() noexcept;
[[nodiscard]] ThreadCounters threadCounters() noexcept;
[[nodiscard]] ProcessMemory  processMemory();

}   // namespace abt::util
