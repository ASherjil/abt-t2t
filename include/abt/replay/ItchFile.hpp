#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <span>
#include <string>
#include <vector>

struct gzFile_s;

namespace abt::replay {

class ItchFileReader {
public:
    explicit ItchFileReader(const std::string& path, std::size_t bufferBytes = 1u << 22);
    ~ItchFileReader();

    ItchFileReader(const ItchFileReader&)            = delete;
    ItchFileReader& operator=(const ItchFileReader&) = delete;

    [[nodiscard]] bool ok() const noexcept;
    [[nodiscard]] bool next(std::span<const std::byte>& msg);

    [[nodiscard]] std::uint64_t messages() const noexcept;
    [[nodiscard]] std::uint64_t bytes() const noexcept;
    [[nodiscard]] bool          truncated() const noexcept;

private:
    [[nodiscard]] bool fill(std::size_t need);

    gzFile_s*              m_file = nullptr;
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
    ~ItchFileWriter();

    ItchFileWriter(const ItchFileWriter&)            = delete;
    ItchFileWriter& operator=(const ItchFileWriter&) = delete;

    [[nodiscard]] bool          ok() const noexcept;
    void                        write(std::span<const std::byte> msg);
    [[nodiscard]] std::uint64_t messages() const noexcept;

private:
    std::FILE*    m_file     = nullptr;
    std::uint64_t m_messages = 0;
};

}   // namespace abt::replay
