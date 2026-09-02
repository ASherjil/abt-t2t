#pragma once

#include <cstddef>
#include <memory>
#include <memory_resource>

namespace abt::util {

class HugePageArena {
public:
    HugePageArena() = default;
    explicit HugePageArena(std::size_t bytes);

    [[nodiscard]] std::pmr::memory_resource* resource() noexcept;
    [[nodiscard]] bool                       huge() const noexcept;
    [[nodiscard]] std::size_t                capacity() const noexcept;

private:
    struct Unmap {
        Unmap() noexcept;
        explicit Unmap(std::size_t mapped) noexcept;
        void operator()(void* p) const noexcept;

        std::size_t bytes;
    };

    std::unique_ptr<void, Unmap>                         m_map;
    std::unique_ptr<std::pmr::monotonic_buffer_resource> m_pool;
    bool                                                 m_huge = false;
};

}   // namespace abt::util
