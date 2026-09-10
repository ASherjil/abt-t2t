#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <rigtorp/SPSCQueue.h>

#include "t2t/lob/Types.hpp"
#include "t2t/util/Histogram.hpp"

namespace abt::dut {

struct DutStatus {
    std::uint64_t elapsedNs = 0;
    std::uint64_t packets   = 0;
    std::uint64_t seq       = 0;
    std::uint64_t gaps      = 0;
    std::uint64_t live      = 0;
    std::uint64_t enters    = 0;
    std::uint64_t replaces  = 0;
    std::uint64_t cancels   = 0;
    std::uint64_t accepts   = 0;
    std::uint64_t fills     = 0;
    std::uint64_t rejects   = 0;
    std::int64_t  position  = 0;
    std::uint32_t sent      = 0;
    Price         bid       = kNoPrice;
    Price         ask       = kNoPrice;
    bool          feedValid = true;
};

void printDutStatus(const DutStatus& s);

struct SampleContext {
    static constexpr std::uint8_t kSent     = 1u << 0;
    static constexpr std::uint8_t kRehash   = 1u << 1;
    static constexpr std::uint8_t kGap      = 1u << 2;
    static constexpr std::uint8_t kReanchor = 1u << 3;
    static constexpr std::uint8_t kNewBook  = 1u << 4;
    static constexpr std::uint8_t kRescan   = 1u << 5;
    static constexpr std::uint8_t kMulti    = 1u << 6;
    static constexpr std::uint8_t kTxReap   = 1u << 7;

    [[nodiscard]] static constexpr std::uint64_t pack(std::uint64_t seq, std::uint16_t msgs,
                                                      std::uint8_t flags) noexcept {
        const std::uint64_t m = msgs > 255u ? 255u : msgs;
        return (seq << 16) | (m << 8) | flags;
    }

    [[nodiscard]] static constexpr std::uint64_t seq(std::uint64_t ctx) noexcept {
        return ctx >> 16;
    }

    [[nodiscard]] static constexpr std::uint8_t msgs(std::uint64_t ctx) noexcept {
        return static_cast<std::uint8_t>((ctx >> 8) & 0xffu);
    }

    [[nodiscard]] static constexpr std::uint8_t flags(std::uint64_t ctx) noexcept {
        return static_cast<std::uint8_t>(ctx & 0xffu);
    }

    static constexpr std::size_t kStages = 4;

    [[nodiscard]] static constexpr std::uint64_t clampStage(std::uint64_t v) noexcept {
        return v > 0xffffu ? 0xffffu : v;
    }

    [[nodiscard]] static constexpr std::uint64_t packStages(std::uint64_t a, std::uint64_t b, std::uint64_t c,
                                                            std::uint64_t d) noexcept {
        return clampStage(a) | (clampStage(b) << 16) | (clampStage(c) << 32) | (clampStage(d) << 48);
    }

    [[nodiscard]] static constexpr std::uint64_t stage(std::uint64_t stages, std::size_t i) noexcept {
        return (stages >> (16 * i)) & 0xffffu;
    }
};

struct Outlier {
    std::int64_t  ns     = 0;
    std::uint64_t ctx    = 0;
    std::uint64_t stages = 0;
};

using StageNames = std::array<const char*, 4>;

[[nodiscard]] std::string describeContext(std::uint64_t ctx);
[[nodiscard]] std::string describeStages(std::uint64_t stagesNs, const StageNames& names);

class LatencyRecorder {
public:
    static constexpr std::size_t kWorst = 8;

    using Converter = std::int64_t (*)(std::uint64_t raw, std::uint64_t param) noexcept;

    LatencyRecorder(std::string name, std::size_t queueCapacity, double nsPerUnit, int sigFigs = 3);

    void record(std::uint64_t raw, std::uint64_t ctx = 0, std::uint64_t stages = 0) noexcept;
    void setStageNames(const StageNames& names) noexcept;
    void setConverter(Converter convert, std::uint64_t param) noexcept;
    void setHardwareClock() noexcept;

    bool        drainOne() noexcept;
    std::size_t drain() noexcept;

    [[nodiscard]] std::string_view         name() const noexcept;
    [[nodiscard]] const char*              clockName() const noexcept;
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
    [[nodiscard]] std::uint64_t            rejected() const noexcept;

    void reset() noexcept;
    void summary();

    static void printSummary(std::span<LatencyRecorder* const> recorders);

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
    Converter                  m_convert      = nullptr;
    std::uint64_t              m_convertParam = 0;
    std::uint64_t              m_rejected     = 0;
    bool                       m_hwClock      = false;
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
    if (!m_queue.try_push(Sample{.raw = raw, .ctx = ctx, .stages = stages})) [[unlikely]] {
        m_dropped.fetch_add(1, std::memory_order_relaxed);
    }
}

}   // namespace abt::dut
