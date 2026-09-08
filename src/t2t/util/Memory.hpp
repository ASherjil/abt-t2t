#pragma once

#include <cstddef>
#include <memory>
#include <memory_resource>

namespace abt::util {

class HugePageArena {
public:
    HugePageArena() = default;
    explicit HugePageArena(std::size_t bytes);

    [[nodiscard]] std::pmr::memory_resource* resource() noexcept;
    [[nodiscard]] bool                       huge() const noexcept;
    [[nodiscard]] std::size_t                capacity() const noexcept;

private:
    struct Unmap {
        Unmap() noexcept;
        explicit Unmap(std::size_t mapped) noexcept;
        void operator()(void* p) const noexcept;

        std::size_t bytes;
    };

    std::unique_ptr<void, Unmap>                         m_map;
    std::unique_ptr<std::pmr::monotonic_buffer_resource> m_pool;
    bool                                                 m_huge = false;
};

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

struct CoreInterrupts {
    long tlbShootdowns = 0;
    long functionCalls = 0;
    long reschedules   = 0;
    long timerTicks    = 0;
};

struct ProcessMemory {
    std::size_t rssMb     = 0;
    std::size_t peakRssMb = 0;
    std::size_t hugetlbMb = 0;
};

[[nodiscard]] MemLockResult  lockAndPrefaultMemory() noexcept;
[[nodiscard]] ThreadCounters threadCounters() noexcept;
[[nodiscard]] CoreInterrupts coreInterrupts(int cpu);
[[nodiscard]] ProcessMemory  processMemory();

}   // namespace abt::util
