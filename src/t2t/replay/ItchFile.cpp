#include "t2t/replay/ItchFile.hpp"

#include <cstring>

#include <zlib.h>

namespace abt::replay {

ItchFileReader::ItchFileReader(const std::string& path, std::size_t bufferBytes)
    : m_buf(bufferBytes < 65536 ? 65536 : bufferBytes) {
    gzFile_s* file = gzopen(path.c_str(), "rb");
    m_file.reset(file);
    if (m_file) {
        (void)gzbuffer(m_file.get(), 1u << 20);
    }
}

bool ItchFileReader::ok() const noexcept {
    return m_file != nullptr;
}

bool ItchFileReader::fill(std::size_t need) {
    if (m_len - m_pos >= need) {
        return true;
    }
    if (m_eof) {
        return false;
    }
    if (m_pos > 0) {
        std::memmove(m_buf.data(), m_buf.data() + m_pos, m_len - m_pos);
        m_len -= m_pos;
        m_pos = 0;
    }
    while (m_len < need) {
        const std::size_t room = m_buf.size() - m_len;
        const int         n    = gzread(m_file.get(), m_buf.data() + m_len, static_cast<unsigned>(room));
        if (n <= 0) {
            int err = Z_OK;
            (void)gzerror(m_file.get(), &err);
            m_truncated = (err != Z_OK && err != Z_STREAM_END) || (n == 0 && m_len > 0);
            m_eof       = true;
            break;
        }
        m_len += static_cast<std::size_t>(n);
    }
    return m_len - m_pos >= need;
}

bool ItchFileReader::next(std::span<const std::byte>& msg) {
    if (m_file == nullptr) {
        return false;
    }
    if (!fill(2)) {
        return false;
    }
    const std::byte*  p   = m_buf.data() + m_pos;
    const std::size_t len = (std::to_integer<std::size_t>(p[0]) << 8) | std::to_integer<std::size_t>(p[1]);
    if (len == 0) {
        m_pos += 2;
        m_bytes += 2;
        return next(msg);
    }
    if (!fill(2 + len)) {
        m_truncated = true;
        return false;
    }
    msg = std::span<const std::byte>{m_buf.data() + m_pos + 2, len};
    m_pos += 2 + len;
    m_bytes += 2 + len;
    ++m_messages;
    return true;
}

std::uint64_t ItchFileReader::messages() const noexcept {
    return m_messages;
}

std::uint64_t ItchFileReader::bytes() const noexcept {
    return m_bytes;
}

bool ItchFileReader::truncated() const noexcept {
    return m_truncated;
}

void ItchFileReader::GzClose::operator()(gzFile_s* file) const noexcept {
    (void)gzclose(file);
}

ItchFileWriter::ItchFileWriter(const std::string& path) {
    std::FILE* file = std::fopen(path.c_str(), "wb");
    m_file.reset(file);
}

bool ItchFileWriter::ok() const noexcept {
    return m_file != nullptr;
}

void ItchFileWriter::write(std::span<const std::byte> msg) {
    if (m_file == nullptr || msg.empty() || msg.size() > 0xFFFF) {
        return;
    }
    const unsigned char len[2] = {static_cast<unsigned char>(msg.size() >> 8),
                                  static_cast<unsigned char>(msg.size() & 0xFFu)};
    (void)std::fwrite(len, 1, 2, m_file.get());
    (void)std::fwrite(msg.data(), 1, msg.size(), m_file.get());
    ++m_messages;
}

std::uint64_t ItchFileWriter::messages() const noexcept {
    return m_messages;
}

void ItchFileWriter::FClose::operator()(std::FILE* file) const noexcept {
    (void)std::fclose(file);
}

}   // namespace abt::replay
