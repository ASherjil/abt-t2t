#include "TestHarness.hpp"

#include <cstddef>
#include <cstdint>
#include <memory_resource>

#include "t2t/util/FlatHashMap.hpp"
#include "t2t/util/HugePageArena.hpp"

using namespace abt;

namespace {

void test_arena_serves_pmr_containers() {
    util::HugePageArena arena(4u << 20);
    CHECK(arena.capacity() >= (4u << 20));
    std::pmr::memory_resource* mr = arena.resource();
    CHECK(mr != nullptr);

    util::FlatHashMap<std::uint64_t, std::uint32_t> map(1024, mr);
    for (std::uint64_t k = 1; k <= 5000; ++k) {
        map.insertOrAssign(k, static_cast<std::uint32_t>(k * 3));
    }
    CHECK_EQ(map.size(), 5000u);
    CHECK(map.capacity() >= 8192u);
    const std::uint32_t* v = map.find(4321);
    CHECK(v != nullptr && *v == 4321u * 3u);

    std::pmr::vector<std::uint32_t> levels(mr);
    levels.assign(1u << 16, 0u);
    levels[12345] = 7;
    CHECK_EQ(levels[12345], 7u);
}

void test_empty_arena_falls_back_to_default_resource() {
    util::HugePageArena none;
    CHECK_EQ(none.capacity(), 0u);
    CHECK(!none.huge());
    CHECK(none.resource() == std::pmr::get_default_resource());
}

}   // namespace

int main() {
    test_arena_serves_pmr_containers();
    test_empty_arena_falls_back_to_default_resource();
    return abt::test::summary("arena");
}
