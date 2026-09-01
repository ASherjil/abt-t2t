#pragma once

#include <atomic>
#include <cstddef>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <rigtorp/SPSCQueue.h>

#include "t2t/util/Histogram.hpp"

namespace abt::dut {

class LatencyRecorder {
public:
    LatencyRecorder(std::string name, std::size_t queueCapacity, double nsPerUnit, int sigFigs = 3);

    LatencyRecorder(const LatencyRecorder&)            = delete;
    LatencyRecorder& operator=(const LatencyRecorder&) = delete;

    void record(std::uint64_t raw) noexcept;

    bool        drainOne() noexcept;
    std::size_t drain() noexcept;

    [[nodiscard]] std::string_view       name() const noexcept;
    [[nodiscard]] const util::Histogram& histogram() const noexcept;
    [[nodiscard]] std::int64_t           count() const noexcept;
    [[nodiscard]] std::int64_t           min() const noexcept;
    [[nodiscard]] std::int64_t           max() const noexcept;
    [[nodiscard]] std::int64_t           percentile(double p) const noexcept;
    [[nodiscard]] std::uint64_t          dropped() const noexcept;

    void reset() noexcept;
    void summary() const;

private:
    std::string                       m_name;
    rigtorp::SPSCQueue<std::uint64_t> m_queue;
    util::Histogram                   m_hist;
    double                            m_nsPerUnit;
    std::atomic<std::uint64_t>        m_dropped{0};
};

class RecorderThread {
public:
    explicit RecorderThread(std::vector<LatencyRecorder*> recorders, int cpuCore = -1);
    ~RecorderThread();

    RecorderThread(const RecorderThread&)            = delete;
    RecorderThread& operator=(const RecorderThread&) = delete;

    void               start();
    void               stop();
    [[nodiscard]] bool running() const noexcept;

private:
    void loop() noexcept;

    std::vector<LatencyRecorder*> m_recorders;
    int                           m_core;
    std::atomic<bool>             m_run{false};
    std::thread                   m_thread;
};

inline void LatencyRecorder::record(std::uint64_t raw) noexcept {
    if (!m_queue.try_push(raw)) [[unlikely]] {
        m_dropped.fetch_add(1, std::memory_order_relaxed);
    }
}

}   // namespace abt::dut
