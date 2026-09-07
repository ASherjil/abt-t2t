#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

namespace abt::dut {

struct CapturedPacket {
    static constexpr std::size_t kMaxBytes = 1500;

    std::uint64_t                    ticks  = 0;
    std::uint64_t                    ctx    = 0;
    std::uint64_t                    stages = 0;
    std::uint16_t                    len    = 0;
    std::array<std::byte, kMaxBytes> bytes{};

    [[nodiscard]] std::span<const std::byte> packet() const noexcept {
        return {bytes.data(), len};
    }
};

class PacketCapture {
public:
    static constexpr std::size_t kSlots = 8;

    [[nodiscard]] std::uint64_t floor() const noexcept {
        return m_floor;
    }

    void offer(std::uint64_t ticks, std::uint64_t ctx, std::uint64_t stages,
               std::span<const std::byte> pkt) noexcept {
        CapturedPacket& c = *std::ranges::min_element(m_slots, {}, &CapturedPacket::ticks);
        c.ticks           = ticks;
        c.ctx             = ctx;
        c.stages          = stages;
        c.len             = static_cast<std::uint16_t>(std::min(pkt.size(), CapturedPacket::kMaxBytes));
        std::memcpy(c.bytes.data(), pkt.data(), c.len);
        m_floor = std::ranges::min_element(m_slots, {}, &CapturedPacket::ticks)->ticks;
    }

    [[nodiscard]] std::vector<const CapturedPacket*> sorted() const {
        std::vector<const CapturedPacket*> out;
        for (const CapturedPacket& c : m_slots) {
            if (c.ticks != 0) {
                out.push_back(&c);
            }
        }
        std::sort(out.begin(), out.end(), [](const CapturedPacket* a, const CapturedPacket* b) {
            return a->ticks > b->ticks;
        });
        return out;
    }

private:
    std::array<CapturedPacket, kSlots> m_slots{};
    std::uint64_t                      m_floor = 0;
};

}   // namespace abt::dut
