#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <span>
#include <string>
#include <vector>

struct gzFile_s;

namespace abt::replay {

class ItchFileReader {
public:
    explicit ItchFileReader(const std::string& path, std::size_t bufferBytes = 1u << 22);

    [[nodiscard]] bool ok() const noexcept;
    [[nodiscard]] bool next(std::span<const std::byte>& msg);

    [[nodiscard]] std::uint64_t messages() const noexcept;
    [[nodiscard]] std::uint64_t bytes() const noexcept;
    [[nodiscard]] bool          truncated() const noexcept;

private:
    [[nodiscard]] bool fill(std::size_t need);

    struct GzClose {
        void operator()(gzFile_s* file) const noexcept;
    };

    std::unique_ptr<gzFile_s, GzClose> m_file;
    std::vector<std::byte> m_buf;
    std::size_t            m_pos       = 0;
    std::size_t            m_len       = 0;
    std::uint64_t          m_messages  = 0;
    std::uint64_t          m_bytes     = 0;
    bool                   m_eof       = false;
    bool                   m_truncated = false;
};

class ItchFileWriter {
public:
    explicit ItchFileWriter(const std::string& path);

    [[nodiscard]] bool          ok() const noexcept;
    void                        write(std::span<const std::byte> msg);
    [[nodiscard]] std::uint64_t messages() const noexcept;

private:
    struct FClose {
        void operator()(std::FILE* file) const noexcept;
    };

    std::unique_ptr<std::FILE, FClose> m_file;
    std::uint64_t m_messages = 0;
};

}   // namespace abt::replay
