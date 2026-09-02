#include "UniqueFd.hpp"

#include <utility>

#include <unistd.h>

namespace abt::util {

UniqueFd::UniqueFd(int fd) noexcept
    : m_fd(fd) {
}

UniqueFd::UniqueFd(UniqueFd&& other) noexcept
    : m_fd(std::exchange(other.m_fd, -1)) {
}

UniqueFd& UniqueFd::operator=(UniqueFd&& other) noexcept {
    reset(std::exchange(other.m_fd, -1));
    return *this;
}

UniqueFd::~UniqueFd() {
    reset();
}

[[nodiscard]] int UniqueFd::release() noexcept {
    return std::exchange(m_fd, -1);
}

void UniqueFd::reset(int fd) noexcept {
    if (m_fd >= 0) {
        ::close(m_fd);
    }
    m_fd = fd;
}

}   // namespace abt::util
