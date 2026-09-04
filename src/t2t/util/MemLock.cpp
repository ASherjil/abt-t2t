#include "t2t/util/MemLock.hpp"

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
