#pragma once
//
// TestHarness.hpp -- a ~40-line zero-dependency assertion harness.
//
// Deliberately tiny so slice-1 builds and runs with no network/FetchContent. Each test
// executable owns one translation unit; CHECK/CHECK_EQ record into TU-local counters and
// main() returns summary(). Swappable for GoogleTest later without touching test logic.
//
#include <cstdio>
#include <cstdint>

namespace abt::test {

inline int g_checks = 0;
inline int g_failures = 0;

inline void record(bool ok, const char* expr, const char* file, int line) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::fprintf(stderr, "  FAIL  %s:%d:  %s\n", file, line, expr);
    }
}

inline void record_eq(std::uint64_t a, std::uint64_t b, const char* expr,
                       const char* file, int line) {
    ++g_checks;
    if (a != b) {
        ++g_failures;
        std::fprintf(stderr, "  FAIL  %s:%d:  %s   (lhs=%llu rhs=%llu)\n", file, line, expr,
                     static_cast<unsigned long long>(a), static_cast<unsigned long long>(b));
    }
}

inline int summary(const char* name) {
    std::fprintf(stderr, "[%s] %d checks, %d failure%s\n", name, g_checks, g_failures,
                 g_failures == 1 ? "" : "s");
    return g_failures == 0 ? 0 : 1;
}

}  // namespace abt::test

#define CHECK(cond)      ::abt::test::record((cond), #cond, __FILE__, __LINE__)
#define CHECK_EQ(a, b)   ::abt::test::record_eq((std::uint64_t)(a), (std::uint64_t)(b), \
                                                #a " == " #b, __FILE__, __LINE__)
