#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <rigtorp/SPSCQueue.h>

#include "t2t/dut/DutStatus.hpp"
#include "t2t/util/Histogram.hpp"

namespace abt::dut {

struct Outlier {
    std::int64_t  ns     = 0;
    std::uint64_t ctx    = 0;
    std::uint64_t stages = 0;
};

using StageNames = std::array<const char*, 4>;

class LatencyRecorder {
public:
    static constexpr std::size_t kWorst = 8;

    LatencyRecorder(std::string name, std::size_t queueCapacity, double nsPerUnit, int sigFigs = 3);

    void record(std::uint64_t raw, std::uint64_t ctx = 0, std::uint64_t stages = 0) noexcept;
    void setStageNames(const StageNames& names) noexcept;

    bool        drainOne() noexcept;
    std::size_t drain() noexcept;

    [[nodiscard]] std::string_view         name() const noexcept;
    [[nodiscard]] const StageNames&        stageNames() const noexcept;
    [[nodiscard]] const util::Histogram&   histogram() const noexcept;
    [[nodiscard]] const util::Histogram&   interval() const noexcept;
    [[nodiscard]] std::span<const Outlier> worstRun() noexcept;
    [[nodiscard]] std::span<const Outlier> worstInterval() noexcept;
    void                                   resetInterval() noexcept;
    [[nodiscard]] std::int64_t             count() const noexcept;
    [[nodiscard]] std::int64_t             min() const noexcept;
    [[nodiscard]] std::int64_t             max() const noexcept;
    [[nodiscard]] std::int64_t             percentile(double p) const noexcept;
    [[nodiscard]] std::uint64_t            dropped() const noexcept;

    void reset() noexcept;
    void summary();

private:
    struct Sample {
        std::uint64_t raw;
        std::uint64_t ctx;
        std::uint64_t stages;
    };

    struct Worst {
        std::array<Outlier, kWorst> items{};
        std::size_t                 n = 0;

        void offer(std::int64_t ns, std::uint64_t ctx, std::uint64_t stages) noexcept;
        void clear() noexcept;
        [[nodiscard]] std::span<const Outlier> sorted() noexcept;
    };

    std::string                m_name;
    rigtorp::SPSCQueue<Sample> m_queue;
    util::Histogram            m_hist;
    util::Histogram            m_interval;
    double                     m_nsPerUnit;
    std::atomic<std::uint64_t> m_dropped{0};
    Worst                      m_worstRun;
    Worst                      m_worstInterval;
    StageNames                 m_stageNames{};
};

struct FlushConfig {
    std::string   logFile;
    std::uint64_t intervalNs = 60'000'000'000ull;
};

class RecorderThread {
public:
    using StatusQueue = rigtorp::SPSCQueue<DutStatus>;

    explicit RecorderThread(std::vector<LatencyRecorder*> recorders, int cpuCore = -1, FlushConfig flush = {},
                            StatusQueue* status = nullptr);

    void               stop();
    [[nodiscard]] bool running() const noexcept;

private:
    static void loop(const std::stop_token& stop, const std::vector<LatencyRecorder*>& recorders, int core,
                     const FlushConfig& flush, StatusQueue* status) noexcept;
    static void flushInterval(util::HistogramLog& log, const std::vector<LatencyRecorder*>& recorders,
                              std::uint64_t startNs, std::uint64_t endNs) noexcept;

    std::jthread m_thread;
};

inline void LatencyRecorder::record(std::uint64_t raw, std::uint64_t ctx, std::uint64_t stages) noexcept {
    if (!m_queue.try_push(Sample{raw, ctx, stages})) [[unlikely]] {
        m_dropped.fetch_add(1, std::memory_order_relaxed);
    }
}

}   // namespace abt::dut
