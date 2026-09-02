#pragma once

namespace abt::util {

class UniqueFd {
public:
    // Rule of 5 must be implemented here
    UniqueFd() noexcept = default;
    explicit UniqueFd(int fd) noexcept;
    UniqueFd(const UniqueFd&) = delete;
    UniqueFd(UniqueFd&& other) noexcept;
    UniqueFd& operator=(const UniqueFd&) = delete;
    UniqueFd& operator=(UniqueFd&& other) noexcept;
    ~UniqueFd();

    [[nodiscard]] int release() noexcept;

    [[nodiscard, gnu::hot, gnu::always_inline]] int get() const noexcept {
        return m_fd;
    }

    [[nodiscard, gnu::hot, gnu::always_inline]] explicit operator bool() const noexcept {
        return m_fd >= 0;
    }

    void reset(int fd = -1) noexcept;

private:
    int m_fd{-1};
};

}   // namespace abt::util
