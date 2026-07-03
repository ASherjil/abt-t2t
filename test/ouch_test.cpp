//
// ouch_test.cpp -- verifies OUCH 5.0 message layouts, the 8-byte Price/Timestamp
// encoding, a zero-copy overlay round-trip, and the TagValue appendage helpers.
//
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

#include "abt/protocol/Endian.hpp"
#include "abt/protocol/Ouch50.hpp"
#include "TestHarness.hpp"

using namespace abt;

namespace {

template <class T>
std::uint8_t byte_at(const T& v, std::size_t off) {
    std::uint8_t b;
    std::memcpy(&b, reinterpret_cast<const std::byte*>(&v) + off, 1);
    return b;
}

void test_message_sizes() {
    CHECK_EQ(sizeof(ouch::EnterOrder), 47u);
    CHECK_EQ(sizeof(ouch::ReplaceOrder), 40u);
    CHECK_EQ(sizeof(ouch::CancelOrder), 11u);
    CHECK_EQ(sizeof(ouch::SystemEvent), 10u);
    CHECK_EQ(sizeof(ouch::Accepted), 64u);
    CHECK_EQ(sizeof(ouch::Replaced), 68u);
    CHECK_EQ(sizeof(ouch::Canceled), 20u);
    CHECK_EQ(sizeof(ouch::Executed), 36u);
    CHECK_EQ(sizeof(ouch::Rejected), 31u);
}

// Build an Enter Order and check its exact on-wire byte layout, incl. the 8-byte Price.
void test_enter_order_layout() {
    ouch::EnterOrder o{};
    o.type               = static_cast<char>(ouch::InType::EnterOrder);
    o.userRefNum         = 0x01020304u;
    o.side               = static_cast<char>(ouch::Side::Buy);
    o.quantity           = 100u;
    o.symbol             = std::string_view{"AAPL"};
    o.price              = 1502500u;                    // $150.25 (8-byte, 4 decimals)
    o.timeInForce        = static_cast<char>(ouch::TimeInForce::Day);
    o.display            = static_cast<char>(ouch::Display::Visible);
    o.capacity           = static_cast<char>(ouch::Capacity::Agency);
    o.imSweepEligibility = static_cast<char>(ouch::ImSweep::NotEligible);
    o.crossType          = static_cast<char>(ouch::CrossType::Continuous);
    o.clOrdId            = std::string_view{"ORDER-0001"};
    o.appendageLength    = 0u;

    CHECK_EQ(byte_at(o, 0), static_cast<std::uint8_t>('O'));   // type
    CHECK_EQ(byte_at(o, 1), 0x01u);                            // userRefNum MSB
    CHECK_EQ(byte_at(o, 4), 0x04u);                            // userRefNum LSB
    CHECK_EQ(byte_at(o, 5), static_cast<std::uint8_t>('B'));   // side
    CHECK_EQ(byte_at(o, 9), 100u);                             // quantity LSB (offset 6..9)
    CHECK_EQ(byte_at(o, 10), static_cast<std::uint8_t>('A'));  // symbol[0] (offset 10..17)
    CHECK_EQ(byte_at(o, 17), static_cast<std::uint8_t>(' '));  // symbol padding
    // price 1502500 == 0x0016ED24 -> 8 bytes big-endian at offset 18..25
    CHECK_EQ(byte_at(o, 18), 0x00u);
    CHECK_EQ(byte_at(o, 21), 0x00u);
    CHECK_EQ(byte_at(o, 23), 0x16u);
    CHECK_EQ(byte_at(o, 24), 0xEDu);
    CHECK_EQ(byte_at(o, 25), 0x24u);
    CHECK_EQ(byte_at(o, 26), static_cast<std::uint8_t>('0'));  // timeInForce = Day
    CHECK_EQ(byte_at(o, 27), static_cast<std::uint8_t>('Y'));  // display
    CHECK_EQ(byte_at(o, 30), static_cast<std::uint8_t>('N'));  // crossType (Continuous)
    CHECK_EQ(byte_at(o, 31), static_cast<std::uint8_t>('O'));  // clOrdId[0] = "ORDER..."
    CHECK_EQ(byte_at(o, 45), 0x00u);                           // appendageLength (offset 45..46)
    CHECK_EQ(byte_at(o, 46), 0x00u);
}

// Serialise an Accepted, decode it by overlay (the client's RX path).
void test_accepted_roundtrip() {
    ouch::Accepted a{};
    a.type                 = static_cast<char>(ouch::OutType::Accepted);
    a.timestamp            = 34200000000123ull;         // ns since midnight
    a.userRefNum           = 777u;
    a.side                 = static_cast<char>(ouch::Side::Sell);
    a.quantity             = 500u;
    a.symbol               = std::string_view{"MSFT"};
    a.price                = 4207500u;                  // $420.75
    a.orderReferenceNumber = 0xCAFED00DBEEFull;
    a.orderState           = static_cast<char>(ouch::OrderState::Live);
    a.clOrdId              = std::string_view{"CID-42"};
    a.appendageLength      = 0u;

    std::array<std::byte, sizeof(ouch::Accepted)> frame{};
    std::memcpy(frame.data(), &a, sizeof a);

    ouch::Accepted d{};
    std::memcpy(&d, frame.data(), sizeof d);

    CHECK_EQ(d.timestamp.value(), 34200000000123ull);
    CHECK_EQ(d.userRefNum.value(), 777u);
    CHECK(d.side == 'S');
    CHECK_EQ(d.quantity.value(), 500u);
    CHECK(d.symbol.view() == "MSFT");
    CHECK_EQ(d.price.value(), 4207500u);
    CHECK_EQ(d.price.value() / ouch::kPriceScale, 420u);
    CHECK_EQ(d.orderReferenceNumber.value(), 0xCAFED00DBEEFull);
    CHECK(d.orderState == 'L');
    CHECK(d.clOrdId.view() == "CID-42");
}

// Write a MinQty TagValue into an appendage buffer and iterate it back.
void test_appendage_tlv() {
    std::array<std::byte, 32> buf{};

    wire::u32be minQty{};
    minQty = 100u;                                      // MinQty is a 4-byte Integer
    const std::size_t n = ouch::writeOption(
        buf, ouch::OptionTag::MinQty,
        std::span<const std::byte>{minQty.bytes.data(), minQty.bytes.size()});
    CHECK_EQ(n, 6u);                                    // 1 len + 1 tag + 4 value
    CHECK_EQ(byte_at(buf[0], 0), 5u);                   // remaining length = tag + 4 value
    CHECK_EQ(byte_at(buf[1], 0), static_cast<std::uint8_t>(ouch::OptionTag::MinQty));

    int seen = 0;
    std::uint32_t decoded = 0;
    ouch::forEachOption(std::span<const std::byte>{buf.data(), n},
                        [&](ouch::OptionTag tag, std::span<const std::byte> value) {
        ++seen;
        if (tag == ouch::OptionTag::MinQty && value.size() == 4) {
            wire::u32be v{};
            std::memcpy(v.bytes.data(), value.data(), 4);
            decoded = v.value();
        }
    });
    CHECK_EQ(seen, 1);
    CHECK_EQ(decoded, 100u);
}

}  // namespace

int main() {
    test_message_sizes();
    test_enter_order_layout();
    test_accepted_roundtrip();
    test_appendage_tlv();
    return abt::test::summary("ouch_test");
}
