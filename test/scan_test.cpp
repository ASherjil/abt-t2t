//
// Unit test for the nearest-non-empty-level scan (abt::util). Cross-checks the AVX2 path against
// the scalar path for every start position over arrays whose sizes straddle the 8-wide block
// boundary, with all-zero, single-populated, gapped, and random contents.
//

#include <cstdint>
#include <vector>

#include "TestHarness.hpp"

#include "abt/util/Scan.hpp"

using namespace abt;

namespace {

void checkArray(const std::vector<std::uint32_t>& a) {
    const std::size_t n = a.size();
    bool downOk = true;
    bool upOk = true;
    for (std::size_t start = 0; start < n; ++start) {
        if (util::scanDownNonZero(a.data(), start) != util::scanDownNonZeroScalar(a.data(), start)) {
            downOk = false;
        }
        if (util::scanUpNonZero(a.data(), start, n - 1) !=
            util::scanUpNonZeroScalar(a.data(), start, n - 1)) {
            upOk = false;
        }
    }
    CHECK(downOk);
    CHECK(upOk);
}

void test_scan() {
    const std::size_t sizes[] = {1, 2, 7, 8, 9, 15, 16, 17, 31, 64, 100, 257};

    std::uint64_t rng = 0xabcdef123ull;
    const auto next = [&rng]() noexcept -> std::uint64_t {
        rng ^= rng << 13;
        rng ^= rng >> 7;
        rng ^= rng << 17;
        return rng;
    };

    for (std::size_t n : sizes) {
        // All zero.
        std::vector<std::uint32_t> a(n, 0u);
        checkArray(a);

        // Single populated level at each position.
        for (std::size_t p = 0; p < n; ++p) {
            std::vector<std::uint32_t> b(n, 0u);
            b[p] = 42u;
            checkArray(b);
        }

        // Sparse random (mostly zero, occasional non-zero) — a few draws.
        for (int trial = 0; trial < 8; ++trial) {
            std::vector<std::uint32_t> c(n, 0u);
            for (std::size_t k = 0; k < n; ++k) {
                if ((next() % 5u) == 0u) {
                    c[k] = static_cast<std::uint32_t>(next() | 1u);
                }
            }
            checkArray(c);
        }
    }
}

}

int main() {
    test_scan();
    return abt::test::summary("scan");
}
