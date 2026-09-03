#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <stop_token>
#include <thread>

#include <rigtorp/SPSCQueue.h>

#include "t2t/dut/BookTable.hpp"

namespace abt::dut {

struct FrameRef {
    const std::byte* data = nullptr;
    std::uint32_t    len  = 0;
    std::uint32_t    pad  = 0;
};

class ColdShard {
public:
    static constexpr std::size_t kLifetimeMargin = 1024;

    ColdShard(const BookTableConfig& cfg, std::size_t queueCapacity, int core);
    ~ColdShard();

    ColdShard(const ColdShard&)            = delete;
    ColdShard& operator=(const ColdShard&) = delete;

    bool push(std::span<const std::byte> moldPacket) noexcept;
    void reset() noexcept;
    void start();
    void stop();

    [[nodiscard]] const BookTable& books() const noexcept;
    [[nodiscard]] std::uint64_t    applied() const noexcept;
    [[nodiscard]] std::uint64_t    packets() const noexcept;
    [[nodiscard]] std::uint64_t    dropped() const noexcept;
    [[nodiscard]] std::uint64_t    stale() const noexcept;
    [[nodiscard]] std::size_t      maxDepth() const noexcept;
    [[nodiscard]] int              core() const noexcept;
    [[nodiscard]] bool             running() const noexcept;

private:
    static constexpr std::uint32_t kDepthSampleEvery = 256;

    void loop(const std::stop_token& stop) noexcept;
    void consume(const FrameRef& f) noexcept;
    void applyFrame(std::span<const std::byte> moldPacket) noexcept;
    void applyOne(std::span<const std::byte> msg) noexcept;

    BookTable                    m_books;
    rigtorp::SPSCQueue<FrameRef> m_queue;
    std::size_t                  m_safeDepth;
    int                          m_core;
    std::uint64_t                m_dropped  = 0;
    std::uint64_t                m_applied  = 0;
    std::uint64_t                m_packets  = 0;
    std::uint64_t                m_stale    = 0;
    std::size_t                  m_maxDepth = 0;
    std::jthread                 m_thread;
};

}   // namespace abt::dut
