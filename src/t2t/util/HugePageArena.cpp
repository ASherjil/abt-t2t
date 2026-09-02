#include "t2t/util/HugePageArena.hpp"

#include <sys/mman.h>

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
    m_map = std::unique_ptr<void, Unmap>(p, Unmap{size});
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

}   // namespace abt::util
