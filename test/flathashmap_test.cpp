//
// Unit test for the open-addressing hash map (abt::util::FlatHashMap). The oracle test is the real
// guard: 200k randomized insert/erase ops cross-checked against std::unordered_map exercise the
// backward-shift deletion and probe chains far harder than hand-written cases could.
//

#include "TestHarness.hpp"

#include <cstdint>
#include <unordered_map>

#include "t2t/util/FlatHashMap.hpp"

using namespace abt;

namespace {

using Map = util::FlatHashMap<std::uint64_t, std::uint32_t>;

// Guarded value check — keeps the dereference behind an explicit null test the compiler can see.
void expectVal(Map& map, std::uint64_t key, std::uint32_t expected) {
    const std::uint32_t* p = map.find(key);
    CHECK(p != nullptr);
    if (p != nullptr) {
        CHECK_EQ(*p, expected);
    }
}

void test_basic() {
    Map map(16);
    CHECK_EQ(map.size(), 0u);
    CHECK(map.find(1) == nullptr);

    for (std::uint64_t k = 1; k <= 1000; ++k) {
        map.insertOrAssign(k, static_cast<std::uint32_t>(k * 10));
    }
    CHECK_EQ(map.size(), 1000u);
    for (std::uint64_t k = 1; k <= 1000; ++k) {
        expectVal(map, k, static_cast<std::uint32_t>(k * 10));
    }
    CHECK(map.find(2000) == nullptr);

    // Overwrite existing key: value changes, size does not.
    map.insertOrAssign(5, 999u);
    expectVal(map, 5, 999u);
    CHECK_EQ(map.size(), 1000u);

    // In-place modify through the returned pointer.
    std::uint32_t* p = map.find(5);
    CHECK(p != nullptr);
    if (p != nullptr) {
        *p = 123u;
    }
    expectVal(map, 5, 123u);

    // Erase evens; odds survive.
    for (std::uint64_t k = 2; k <= 1000; k += 2) {
        CHECK(map.erase(k));
    }
    CHECK_EQ(map.size(), 500u);
    CHECK(map.erase(2) == false);
    for (std::uint64_t k = 1; k <= 1000; ++k) {
        const std::uint32_t* q = map.find(k);
        if ((k % 2) == 1) {
            CHECK(q != nullptr);
        } else {
            CHECK(q == nullptr);
        }
    }

    map.clear();
    CHECK_EQ(map.size(), 0u);
    CHECK(map.find(1) == nullptr);
}

void test_grow() {
    Map map(16);
    CHECK_EQ(map.capacity(), 16u);
    for (std::uint64_t k = 1; k <= 100; ++k) {
        map.insertOrAssign(k, static_cast<std::uint32_t>(k));
    }
    CHECK(map.capacity() > 16u);
    CHECK_EQ(map.size(), 100u);
    for (std::uint64_t k = 1; k <= 100; ++k) {
        expectVal(map, k, static_cast<std::uint32_t>(k));
    }
}

void test_erase_at() {
    Map map(16);
    for (std::uint64_t k = 1; k <= 9; ++k) {
        map.insertOrAssign(k, static_cast<std::uint32_t>(k * 10));
    }
    for (std::uint64_t k = 1; k <= 9; k += 2) {
        std::uint32_t* v = map.find(k);
        CHECK(v != nullptr);
        if (v != nullptr) {
            map.eraseAt(map.indexOf(v));
        }
    }
    CHECK_EQ(map.size(), 4u);
    for (std::uint64_t k = 1; k <= 9; ++k) {
        if (k % 2 == 0) {
            expectVal(map, k, static_cast<std::uint32_t>(k * 10));
        } else {
            CHECK(map.find(k) == nullptr);
        }
    }
    std::unordered_map<std::uint64_t, std::uint32_t> oracle;
    Map                                              big(64);
    std::uint64_t                                    x = 88172645463325252ull;
    for (int i = 0; i < 20000; ++i) {
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
        const std::uint64_t key = 1 + (x % 700);
        if ((x >> 20) % 3 == 0) {
            std::uint32_t* v = big.find(key);
            if (v != nullptr) {
                big.eraseAt(big.indexOf(v));
            }
            oracle.erase(key);
        } else {
            big.insertOrAssign(key, static_cast<std::uint32_t>(i));
            oracle[key] = static_cast<std::uint32_t>(i);
        }
    }
    CHECK_EQ(big.size(), oracle.size());
    for (const auto& [k, v] : oracle) {
        expectVal(big, k, v);
    }
}

void test_oracle() {
    Map                                              map(16);
    std::unordered_map<std::uint64_t, std::uint32_t> oracle;

    std::uint64_t rng  = 0x123456789ull;
    const auto    next = [&rng]() noexcept -> std::uint64_t {
        rng ^= rng << 13;
        rng ^= rng >> 7;
        rng ^= rng << 17;
        return rng;
    };

    bool eraseMatched = true;
    bool spotMatched  = true;
    for (int i = 0; i < 200000; ++i) {
        const std::uint64_t key = (next() % 4000) + 1;   // keys 1..4000 (never the 0 sentinel)
        if ((next() & 1u) != 0u) {
            const auto v = static_cast<std::uint32_t>(next());
            map.insertOrAssign(key, v);
            oracle[key] = v;
        } else {
            const bool a = map.erase(key);
            const bool b = (oracle.erase(key) != 0);
            if (a != b) {
                eraseMatched = false;
            }
        }
        if ((i & 63) == 0) {
            const std::uint64_t  q  = (next() % 4000) + 1;
            const std::uint32_t* p  = map.find(q);
            const auto           it = oracle.find(q);
            if (it == oracle.end()) {
                if (p != nullptr) {
                    spotMatched = false;
                }
            } else if (p == nullptr || *p != it->second) {
                spotMatched = false;
            }
        }
    }
    CHECK(eraseMatched);
    CHECK(spotMatched);
    CHECK_EQ(map.size(), oracle.size());

    bool fullSweep = true;
    for (const auto& [k, v] : oracle) {
        const std::uint32_t* p = map.find(k);
        if (p == nullptr || *p != v) {
            fullSweep = false;
        }
    }
    CHECK(fullSweep);
}

}   // namespace

int main() {
    test_erase_at();
    test_basic();
    test_grow();
    test_oracle();
    return abt::test::summary("flathashmap");
}
