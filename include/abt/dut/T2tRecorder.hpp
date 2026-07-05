#pragma once
//
// Tick-to-trade latency recorder: collects TX_hwts - RX_hwts samples (nanoseconds, both read
// off the single X2522 PHC so no clock sync is needed) and reports order statistics. record() is
// O(1) and allocation-free once warmed, so it is safe on the critical loop; the percentile
// queries sort a copy and are meant for off-path use (e.g. a shutdown summary).
//

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace abt::dut {

class T2tRecorder {
public:
    explicit T2tRecorder(std::size_t capacity = 1u << 16);

    void record(std::uint64_t latencyNs);

    [[nodiscard]] std::size_t   count() const noexcept;
    [[nodiscard]] std::uint64_t min() const noexcept;
    [[nodiscard]] std::uint64_t max() const noexcept;
    [[nodiscard]] std::uint64_t percentile(double p) const;

    void summary(std::string_view label) const;
    void clear() noexcept;

private:
    std::vector<std::uint64_t> m_samples;
    std::size_t                m_capacity;
    std::size_t                m_count = 0;
    std::uint64_t              m_min = 0;
    std::uint64_t              m_max = 0;
};

}
