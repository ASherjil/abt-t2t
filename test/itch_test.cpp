//
// Verifies ITCH 5.0 wire layouts and big-endian overlay encoding.
//

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "abt/protocol/Endian.hpp"
#include "abt/protocol/Itch50.hpp"
#include "TestHarness.hpp"

using namespace abt;

namespace {

template <class T>
std::uint8_t byte_at(const T& v, std::size_t off) {
    std::uint8_t b;
    std::memcpy(&b, reinterpret_cast<const std::byte*>(&v) + off, 1);
    return b;
}

void test_bigendian() {
    wire::u32be p{};
    p = 1234500u;
    CHECK_EQ(p.value(), 1234500u);
    CHECK_EQ(byte_at(p, 0), 0x00u);
    CHECK_EQ(byte_at(p, 1), 0x12u);
    CHECK_EQ(byte_at(p, 2), 0xD6u);
    CHECK_EQ(byte_at(p, 3), 0x44u);

    wire::u64be r{};
    r = 0x0102030405060708ull;
    CHECK_EQ(byte_at(r, 0), 0x01u);
    CHECK_EQ(byte_at(r, 7), 0x08u);
    CHECK_EQ(r.value(), 0x0102030405060708ull);
}

void test_uint48() {
    wire::Uint48 ts{};
    const std::uint64_t ns = 34200000000000ull;
    ts = ns;
    CHECK_EQ(ts.value(), ns);
    CHECK_EQ(byte_at(ts, 0), 0x1Fu);
    CHECK_EQ(byte_at(ts, 5), 0x00u);
    CHECK_EQ(sizeof(wire::Uint48), 6u);
}

void test_alpha() {
    wire::Alpha<8> s{};
    s = std::string_view{"AAPL"};
    CHECK(s.view() == "AAPL");
    CHECK_EQ(byte_at(s, 0), static_cast<std::uint8_t>('A'));
    CHECK_EQ(byte_at(s, 4), static_cast<std::uint8_t>(' '));
    CHECK_EQ(byte_at(s, 7), static_cast<std::uint8_t>(' '));

    wire::Alpha<8> full{};
    full = std::string_view{"BERKSHIRE"};
    CHECK(full.view() == "BERKSHIR");
}

void test_message_sizes() {
    CHECK_EQ(sizeof(itch::SystemEvent), 12u);
    CHECK_EQ(sizeof(itch::StockDirectory), 39u);
    CHECK_EQ(sizeof(itch::StockTradingAction), 25u);
    CHECK_EQ(sizeof(itch::AddOrder), 36u);
    CHECK_EQ(sizeof(itch::AddOrderMpid), 40u);
    CHECK_EQ(sizeof(itch::OrderExecuted), 31u);
    CHECK_EQ(sizeof(itch::OrderExecutedWithPrice), 36u);
    CHECK_EQ(sizeof(itch::OrderCancel), 23u);
    CHECK_EQ(sizeof(itch::OrderDelete), 19u);
    CHECK_EQ(sizeof(itch::OrderReplace), 35u);
    CHECK_EQ(sizeof(itch::TradeNonCross), 44u);
    CHECK_EQ(sizeof(itch::CrossTrade), 40u);
}

void test_add_order_layout() {
    itch::AddOrder a{};
    a.messageType    = static_cast<char>(itch::MessageType::AddOrder);
    a.stockLocate    = 0x1234u;
    a.trackingNumber = 0u;
    a.timestamp      = 34200000000001ull;
    a.orderRef       = 0xDEADBEEFCAFEBABEull;
    a.side           = static_cast<char>(itch::Side::Buy);
    a.shares         = 100u;
    a.stock          = std::string_view{"AAPL"};
    a.price          = 1502500u;

    CHECK_EQ(byte_at(a, 0), static_cast<std::uint8_t>('A'));
    CHECK_EQ(byte_at(a, 1), 0x12u);
    CHECK_EQ(byte_at(a, 2), 0x34u);
    CHECK_EQ(byte_at(a, 11), 0xDEu);
    CHECK_EQ(byte_at(a, 18), 0xBEu);
    CHECK_EQ(byte_at(a, 19), static_cast<std::uint8_t>('B'));
    CHECK_EQ(byte_at(a, 23), 100u);
    CHECK_EQ(byte_at(a, 24), static_cast<std::uint8_t>('A'));
    CHECK_EQ(byte_at(a, 31), static_cast<std::uint8_t>(' '));
    CHECK_EQ(byte_at(a, 32), 0x00u);
    CHECK_EQ(byte_at(a, 33), 0x16u);
    CHECK_EQ(byte_at(a, 34), 0xEDu);
    CHECK_EQ(byte_at(a, 35), 0x24u);
}

void test_overlay_roundtrip() {
    itch::AddOrder src{};
    src.messageType = 'A';
    src.orderRef    = 42ull;
    src.side        = 'S';
    src.shares      = 250u;
    src.stock       = std::string_view{"MSFT"};
    src.price       = 4207500u;

    std::array<std::byte, sizeof(itch::AddOrder)> frame{};
    std::memcpy(frame.data(), &src, sizeof src);

    itch::AddOrder dst{};
    std::memcpy(&dst, frame.data(), sizeof dst);

    CHECK_EQ(dst.orderRef.value(), 42ull);
    CHECK(dst.side == 'S');
    CHECK_EQ(dst.shares.value(), 250u);
    CHECK(dst.stock.view() == "MSFT");
    CHECK_EQ(dst.price.value(), 4207500u);
    CHECK_EQ(dst.price.value() / itch::kPriceScale, 420u);
}

}

int main() {
    test_bigendian();
    test_uint48();
    test_alpha();
    test_message_sizes();
    test_add_order_layout();
    test_overlay_roundtrip();
    return abt::test::summary("itch_test");
}
