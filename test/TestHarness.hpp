#pragma once
//
// Minimal assertion harness (CHECK / CHECK_EQ) for the unit tests.
//

#include <cstdint>

#include <fmt/core.h>

namespace abt::test {

inline int g_checks = 0;
inline int g_failures = 0;

inline void record(bool ok, const char* expr, const char* file, int line) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        fmt::print(stderr, "  FAIL  {}:{}:  {}\n", file, line, expr);
    }
}

inline void record_eq(std::uint64_t a, std::uint64_t b, const char* expr,
                       const char* file, int line) {
    ++g_checks;
    if (a != b) {
        ++g_failures;
        fmt::print(stderr, "  FAIL  {}:{}:  {}   (lhs={} rhs={})\n", file, line, expr, a, b);
    }
}

inline int summary(const char* name) {
    fmt::print(stderr, "[{}] {} checks, {} failure{}\n", name, g_checks, g_failures,
               g_failures == 1 ? "" : "s");
    return g_failures == 0 ? 0 : 1;
}

}

#define CHECK(cond)      ::abt::test::record((cond), #cond, __FILE__, __LINE__)
#define CHECK_EQ(a, b)   ::abt::test::record_eq(static_cast<std::uint64_t>(a),          \
                                                static_cast<std::uint64_t>(b),          \
                                                #a " == " #b, __FILE__, __LINE__)
