#include "t2t/util/MappedFile.hpp"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include "t2t/util/UniqueFd.hpp"

namespace abt::util {

MappedFile::Unmap::Unmap() noexcept
    : bytes(0) {
}

MappedFile::Unmap::Unmap(std::size_t mapped) noexcept
    : bytes(mapped) {
}

void MappedFile::Unmap::operator()(void* p) const noexcept {
    (void)::munmap(p, bytes);
}

MappedFile::MappedFile(const std::string& path, bool populate) {
    const UniqueFd fd{::open(path.c_str(), O_RDONLY | O_CLOEXEC)};
    if (!fd) {
        return;
    }
    struct stat st{};
    if (::fstat(fd.get(), &st) != 0 || st.st_size <= 0) {
        return;
    }
    const auto size  = static_cast<std::size_t>(st.st_size);
    const int  flags = MAP_PRIVATE | (populate ? MAP_POPULATE : 0);
    void*      p     = ::mmap(nullptr, size, PROT_READ, flags, fd.get(), 0);
    if (p == MAP_FAILED) {
        return;
    }
    m_map = std::unique_ptr<void, Unmap>(p, Unmap{size});
}

bool MappedFile::ok() const noexcept {
    return static_cast<bool>(m_map);
}

std::span<const std::byte> MappedFile::bytes() const noexcept {
    if (!m_map) {
        return {};
    }
    return {static_cast<const std::byte*>(m_map.get()), m_map.get_deleter().bytes};
}

}   // namespace abt::util
