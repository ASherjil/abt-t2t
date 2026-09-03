#include "TestHarness.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>

#include "t2t/dut/PacketCapture.hpp"
#include "t2t/protocol/Itch50.hpp"
#include "t2t/protocol/Itch50Text.hpp"

using namespace abt;

namespace {

std::array<std::byte, 4> payloadOf(std::uint32_t tag) {
    std::array<std::byte, 4> b{};
    std::memcpy(b.data(), &tag, sizeof tag);
    return b;
}

void test_keeps_slowest() {
    dut::PacketCapture cap;
    CHECK_EQ(cap.floor(), 0u);
    for (std::uint32_t i = 1; i <= 20; ++i) {
        const auto pkt = payloadOf(i);
        if (static_cast<std::uint64_t>(i) > cap.floor()) {
            cap.offer(i, i * 100, i, pkt);
        }
    }
    const auto sorted = cap.sorted();
    CHECK_EQ(sorted.size(), dut::PacketCapture::kSlots);
    CHECK_EQ(cap.floor(), 13u);
    CHECK_EQ(sorted.front()->ticks, 20u);
    CHECK_EQ(sorted.back()->ticks, 13u);
    for (std::size_t k = 1; k < sorted.size(); ++k) {
        CHECK(sorted[k - 1]->ticks > sorted[k]->ticks);
    }
    std::uint32_t tag = 0;
    std::memcpy(&tag, sorted.front()->packet().data(), sizeof tag);
    CHECK_EQ(tag, 20u);
    CHECK_EQ(sorted.front()->len, 4u);
    CHECK_EQ(sorted.front()->ctx, 2000u);
}

void test_truncates_long_packets() {
    dut::PacketCapture              cap;
    std::array<std::byte, 4000>     big{};
    cap.offer(5, 0, 0, big);
    CHECK_EQ(cap.sorted().front()->len, dut::CapturedPacket::kMaxBytes);
}

void test_describe_add_order() {
    itch::AddOrder a{};
    a.messageType = itch::MessageType::AddOrder;
    a.stockLocate = 6562;
    a.timestamp   = 34254268124797ull;
    a.orderRef    = 14911155;
    a.side        = itch::Side::Buy;
    a.shares      = 200;
    a.stock       = "QQQ";
    a.price       = 2204100;
    const std::string s = itch::describe({reinterpret_cast<const std::byte*>(&a), sizeof a});
    CHECK(s == "A loc=6562 ts=34254268124797 ref=14911155 B sh=200 QQQ px=2204100");
    itch::OrderDelete d{};
    d.messageType = itch::MessageType::OrderDelete;
    d.stockLocate = 7457;
    d.orderRef    = 19080696;
    CHECK(itch::describe({reinterpret_cast<const std::byte*>(&d), sizeof d}) == "D loc=7457 ts=0 ref=19080696");
    CHECK(itch::describe({reinterpret_cast<const std::byte*>(&d), 5}) == "?? 5 bytes");
}

}   // namespace

int main() {
    test_keeps_slowest();
    test_truncates_long_packets();
    test_describe_add_order();
    return abt::test::summary("capture_test");
}
