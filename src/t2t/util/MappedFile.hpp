#pragma once

#include <cstddef>
#include <memory>
#include <span>
#include <string>

namespace abt::util {

class MappedFile {
public:
    MappedFile() = default;
    explicit MappedFile(const std::string& path, bool populate = false);

    [[nodiscard]] bool                       ok() const noexcept;
    [[nodiscard]] std::span<const std::byte> bytes() const noexcept;

private:
    struct Unmap {
        Unmap() noexcept;
        explicit Unmap(std::size_t mapped) noexcept;
        void operator()(void* p) const noexcept;

        std::size_t bytes;
    };

    std::unique_ptr<void, Unmap> m_map;
};

}   // namespace abt::util
