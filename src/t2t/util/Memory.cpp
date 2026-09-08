#include "t2t/util/Memory.hpp"

#include <cerrno>
#include <cstddef>
#include <fstream>
#include <sstream>
#include <string>

#include <malloc.h>
#include <sys/mman.h>
#include <sys/resource.h>

namespace abt::util {

namespace {

constexpr std::size_t kHugePage = std::size_t{2} << 20;

std::size_t roundUp(std::size_t bytes) noexcept {
    return (bytes + kHugePage - 1) / kHugePage * kHugePage;
}

}   // namespace

HugePageArena::Unmap::Unmap() noexcept
    : bytes(0) {
}

HugePageArena::Unmap::Unmap(std::size_t mapped) noexcept
    : bytes(mapped) {
}

void HugePageArena::Unmap::operator()(void* p) const noexcept {
    (void)::munmap(p, bytes);
}

HugePageArena::HugePageArena(std::size_t bytes) {
    if (bytes == 0) {
        return;
    }
    const std::size_t size = roundUp(bytes);
    void*             p    = ::mmap(nullptr, size, PROT_READ | PROT_WRITE,
                                    MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB | MAP_POPULATE, -1, 0);
    if (p != MAP_FAILED) {
        m_huge = true;
    } else {
        p = ::mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE, -1, 0);
        if (p == MAP_FAILED) {
            return;
        }
    }
    m_map  = std::unique_ptr<void, Unmap>(p, Unmap{size});
    m_pool = std::make_unique<std::pmr::monotonic_buffer_resource>(p, size, std::pmr::new_delete_resource());
}

std::pmr::memory_resource* HugePageArena::resource() noexcept {
    return m_pool ? m_pool.get() : std::pmr::get_default_resource();
}

bool HugePageArena::huge() const noexcept {
    return m_huge;
}

std::size_t HugePageArena::capacity() const noexcept {
    return m_map ? m_map.get_deleter().bytes : 0;
}

namespace {

constexpr std::size_t kStackPrefaultBytes = 1u << 20;
constexpr std::size_t kPageBytes          = 4096;

[[gnu::noinline]] void prefaultStack() noexcept {
    unsigned char buf[kStackPrefaultBytes];
    for (std::size_t i = 0; i < kStackPrefaultBytes; i += kPageBytes) {
        buf[i] = 0;
    }
    asm volatile("" : : "r"(buf) : "memory");
}

}   // namespace

MemLockResult lockAndPrefaultMemory() noexcept {
    (void)mallopt(M_TRIM_THRESHOLD, -1);
    (void)mallopt(M_MMAP_MAX, 0);
    prefaultStack();
    if (::mlockall(MCL_CURRENT | MCL_FUTURE) != 0) {
        return {.locked = false, .error = errno};
    }
    return {.locked = true, .error = 0};
}

ProcessMemory processMemory() {
    ProcessMemory m{};
    std::ifstream status("/proc/self/status");
    std::string   key;
    while (status >> key) {
        std::size_t kb = 0;
        if (key == "VmRSS:") {
            status >> kb;
            m.rssMb = kb / 1024;
        } else if (key == "VmHWM:") {
            status >> kb;
            m.peakRssMb = kb / 1024;
        } else if (key == "HugetlbPages:") {
            status >> kb;
            m.hugetlbMb = kb / 1024;
        }
        status.ignore(4096, '\n');
    }
    return m;
}

CoreInterrupts coreInterrupts(int cpu) {
    CoreInterrupts c{};
    std::ifstream  in("/proc/interrupts");
    std::string    line;
    int            column = -1;
    if (std::getline(in, line)) {
        std::istringstream head(line);
        std::string        cell;
        for (int i = 0; head >> cell; ++i) {
            if (cell == "CPU" + std::to_string(cpu)) {
                column = i;
            }
        }
    }
    if (column < 0) {
        return c;
    }
    while (std::getline(in, line)) {
        std::istringstream row(line);
        std::string        label;
        row >> label;
        long value = 0;
        for (int i = 0; i <= column; ++i) {
            if (!(row >> value)) {
                value = 0;
                break;
            }
        }
        if (label == "TLB:") {
            c.tlbShootdowns = value;
        } else if (label == "CAL:") {
            c.functionCalls = value;
        } else if (label == "RES:") {
            c.reschedules = value;
        } else if (label == "LOC:") {
            c.timerTicks = value;
        }
    }
    return c;
}

ThreadCounters threadCounters() noexcept {
    rusage ru{};
    if (::getrusage(RUSAGE_THREAD, &ru) != 0) {
        return {};
    }
    return {.minorFaults         = ru.ru_minflt,
            .majorFaults         = ru.ru_majflt,
            .involuntarySwitches = ru.ru_nivcsw,
            .voluntarySwitches   = ru.ru_nvcsw};
}

}   // namespace abt::util
