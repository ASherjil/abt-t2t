#include "t2t/util/MemLock.hpp"

#include <cerrno>
#include <cstddef>

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

PageFaults threadPageFaults() noexcept {
    rusage ru{};
    if (::getrusage(RUSAGE_THREAD, &ru) != 0) {
        return {};
    }
    return {.minor = ru.ru_minflt, .major = ru.ru_majflt};
}

}   // namespace abt::util
