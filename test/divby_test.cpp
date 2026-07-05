//
// Unit test for fast constant division (abt::util::DivBy). Cross-checks the magic-multiply result
// against real division exhaustively over a large dividend range for a spread of divisors.
//

#include <cstdint>

#include "TestHarness.hpp"

#include "abt/util/DivBy.hpp"

using namespace abt;

namespace {

void checkDivisor(std::uint32_t d) {
    const util::DivBy div(d);
    bool ok = true;
    for (std::uint32_t n = 0; n <= (1u << 20); ++n) {
        if (div(n) != n / d) {
            ok = false;
            break;
        }
    }
    CHECK(ok);

    // A few large dividends near the 32-bit ceiling.
    const std::uint32_t bigs[] = {0xFFFFFFFFu, 0xFFFFFFFEu, 0x80000000u, 0x7FFFFFFFu, 123456789u};
    bool okBig = true;
    for (std::uint32_t n : bigs) {
        if (div(n) != n / d) {
            okBig = false;
        }
    }
    CHECK(okBig);
}

void test_divby() {
    const std::uint32_t divisors[] = {1u, 2u, 3u, 4u, 5u, 7u, 8u, 10u, 100u, 128u,
                                      256u, 1000u, 1024u, 12345u, 65535u, 65536u};
    for (std::uint32_t d : divisors) {
        checkDivisor(d);
    }
}

}

int main() {
    test_divby();
    return abt::test::summary("divby");
}
