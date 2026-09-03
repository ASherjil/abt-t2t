#include "t2t/dut/ColdShard.hpp"

#include <immintrin.h>

#include "t2t/protocol/Itch50.hpp"
#include "t2t/protocol/MoldUdp64.hpp"
#include "t2t/util/Affinity.hpp"

namespace abt::dut {

ColdShard::ColdShard(const BookTableConfig& cfg, std::size_t queueCapacity, int core)
    : m_books(cfg),
      m_queue(queueCapacity),
      m_safeDepth(queueCapacity > kLifetimeMargin ? queueCapacity - kLifetimeMargin : queueCapacity),
      m_core(core) {
}

ColdShard::~ColdShard() {
    stop();
}

bool ColdShard::push(std::span<const std::byte> moldPacket) noexcept {
    if (!m_queue.try_emplace(FrameRef{.data = moldPacket.data(),
                                      .len  = static_cast<std::uint32_t>(moldPacket.size())})) [[unlikely]] {
        ++m_dropped;
        return false;
    }
    return true;
}

void ColdShard::reset() noexcept {
    while (!m_queue.try_emplace(FrameRef{})) {
        _mm_pause();
    }
}

void ColdShard::start() {
    if (m_thread.joinable()) {
        return;
    }
    m_thread = std::jthread([this](const std::stop_token& stop) {
        loop(stop);
    });
}

void ColdShard::stop() {
    if (m_thread.joinable()) {
        m_thread.request_stop();
        m_thread.join();
    }
}

void ColdShard::loop(const std::stop_token& stop) noexcept {
    (void)util::pinThread(m_core);
    std::uint32_t sinceDepth = 0;
    while (!stop.stop_requested()) {
        const FrameRef* f = m_queue.front();
        if (f == nullptr) {
            _mm_pause();
            continue;
        }
        consume(*f);
        m_queue.pop();
        if (++sinceDepth == kDepthSampleEvery) {
            sinceDepth              = 0;
            const std::size_t depth = m_queue.size();
            if (depth > m_maxDepth) {
                m_maxDepth = depth;
            }
        }
    }
    while (const FrameRef* f = m_queue.front()) {
        consume(*f);
        m_queue.pop();
    }
}

void ColdShard::consume(const FrameRef& f) noexcept {
    if (f.data == nullptr) {
        m_books.clearAll();
        return;
    }
    if (m_queue.size() > m_safeDepth) [[unlikely]] {
        ++m_stale;
        return;
    }
    applyFrame({f.data, f.len});
}

void ColdShard::applyFrame(std::span<const std::byte> moldPacket) noexcept {
    ++m_packets;
    (void)mold::forEachMessage(moldPacket, [this](std::uint64_t, std::span<const std::byte> msg) {
        applyOne(msg);
    });
}

void ColdShard::applyOne(std::span<const std::byte> msg) noexcept {
    if (msg.size() < 11) [[unlikely]] {
        return;
    }
    if (static_cast<char>(msg[0]) == 'S') [[unlikely]] {
        if (msg.size() >= sizeof(itch::SystemEvent)) {
            const auto* s = reinterpret_cast<const itch::SystemEvent*>(msg.data());
            if (static_cast<itch::SystemEventCode>(s->eventCode) == itch::SystemEventCode::StartOfMessages) {
                m_books.clearAll();
            }
        }
        return;
    }
    (void)m_books.apply(msg);
    ++m_applied;
}

const BookTable& ColdShard::books() const noexcept {
    return m_books;
}

std::uint64_t ColdShard::applied() const noexcept {
    return m_applied;
}

std::uint64_t ColdShard::packets() const noexcept {
    return m_packets;
}

std::uint64_t ColdShard::dropped() const noexcept {
    return m_dropped;
}

std::uint64_t ColdShard::stale() const noexcept {
    return m_stale;
}

std::size_t ColdShard::maxDepth() const noexcept {
    return m_maxDepth;
}

int ColdShard::core() const noexcept {
    return m_core;
}

bool ColdShard::running() const noexcept {
    return m_thread.joinable();
}

}   // namespace abt::dut
