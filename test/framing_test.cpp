//
// framing_test.cpp -- MoldUDP64 (market-data) and SoupBinTCP (order-entry) framing:
// build datagrams/packets, parse them back, verify sequencing and payload fidelity.
//
#include <array>
#include <cstddef>
#include <cstring>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include "abt/protocol/MoldUdp64.hpp"
#include "abt/protocol/SoupBinTcp.hpp"
#include "TestHarness.hpp"

using namespace abt;

namespace {

std::span<const std::byte> bytes(const void* p, std::size_t n) {
    return {reinterpret_cast<const std::byte*>(p), n};
}

// ============================ MoldUDP64 ============================

void test_mold_pack_and_read() {
    const unsigned char m1[] = {0xAA, 0xBB};
    const unsigned char m2[] = {0x01};
    const unsigned char m3[] = {0x02, 0x03, 0x04};

    std::array<std::byte, 256> buf{};
    mold::Packer packer("SESSION001", 1);

    packer.reset(buf.data(), buf.size());
    CHECK(packer.append(bytes(m1, sizeof m1)));
    CHECK(packer.append(bytes(m2, sizeof m2)));
    CHECK(packer.append(bytes(m3, sizeof m3)));
    const auto pkt = packer.finalize();

    CHECK(mold::sessionOf(pkt) == "SESSION001");
    CHECK_EQ(mold::sequenceOf(pkt), 1u);
    CHECK_EQ(mold::countOf(pkt), 3u);
    CHECK_EQ(packer.nextSequence(), 4u);

    std::vector<std::pair<std::uint64_t, std::vector<std::byte>>> got;
    const std::size_t n = mold::forEachMessage(pkt, [&](std::uint64_t seq,
                                                        std::span<const std::byte> msg) {
        got.emplace_back(seq, std::vector<std::byte>(msg.begin(), msg.end()));
    });
    CHECK_EQ(n, 3u);
    CHECK_EQ(got.size(), 3u);
    CHECK_EQ(got[0].first, 1u);
    CHECK_EQ(got[1].first, 2u);
    CHECK_EQ(got[2].first, 3u);
    CHECK_EQ(got[0].second.size(), 2u);
    CHECK_EQ(static_cast<unsigned char>(got[0].second[0]), 0xAAu);
    CHECK_EQ(static_cast<unsigned char>(got[2].second[2]), 0x04u);

    // A second datagram continues the sequence.
    packer.reset(buf.data(), buf.size());
    CHECK(packer.append(bytes(m1, sizeof m1)));
    CHECK(packer.append(bytes(m2, sizeof m2)));
    const auto pkt2 = packer.finalize();
    CHECK_EQ(mold::sequenceOf(pkt2), 4u);
    CHECK_EQ(mold::countOf(pkt2), 2u);
    CHECK_EQ(packer.nextSequence(), 6u);
}

void test_mold_heartbeat_and_eos() {
    std::array<std::byte, 64> buf{};
    mold::Packer packer("SESSION001", 7);

    const auto hb = packer.heartbeat(buf.data());
    CHECK_EQ(hb.size(), mold::kHeaderSize);
    CHECK_EQ(mold::countOf(hb), mold::kHeartbeat);
    CHECK_EQ(mold::sequenceOf(hb), 7u);
    CHECK_EQ(mold::forEachMessage(hb, [](std::uint64_t, std::span<const std::byte>) {}), 0u);

    const auto eos = packer.endOfSession(buf.data());
    CHECK_EQ(mold::countOf(eos), mold::kEndOfSession);
}

void test_mold_overflow_stops_cleanly() {
    const unsigned char big[100] = {};
    std::array<std::byte, 64> small{};
    mold::Packer packer("SESSION001", 1);
    packer.reset(small.data(), small.size());
    CHECK(!packer.append(bytes(big, sizeof big)));   // too large -> refused
    CHECK_EQ(packer.count(), 0u);
}

// ============================ SoupBinTCP ============================

void test_soup_sequenced_data_roundtrip() {
    const unsigned char ouch[] = {0x11, 0x22, 0x33};
    std::array<std::byte, 64> buf{};
    const auto pkt = soup::packSequencedData(buf.data(), bytes(ouch, sizeof ouch));
    CHECK_EQ(pkt.size(), 2u + 1u + 3u);

    soup::Packet p{};
    const std::size_t consumed = soup::parse(pkt, p);
    CHECK_EQ(consumed, pkt.size());
    CHECK(p.type == soup::Type::SequencedData);
    CHECK_EQ(p.payload.size(), 3u);
    CHECK_EQ(static_cast<unsigned char>(p.payload[2]), 0x33u);
}

void test_soup_login_accepted() {
    std::array<std::byte, 64> buf{};
    const auto pkt = soup::packLoginAccepted(buf.data(), "SESSION001", 42);
    soup::Packet p{};
    CHECK_EQ(soup::parse(pkt, p), pkt.size());
    CHECK(p.type == soup::Type::LoginAccepted);
    CHECK_EQ(p.payload.size(), sizeof(soup::LoginAccepted));

    soup::LoginAccepted a{};
    std::memcpy(&a, p.payload.data(), sizeof a);
    CHECK(a.session.view() == "SESSION001");
    CHECK_EQ(soup::parseSeq(a.sequenceNumber), 42u);
}

void test_soup_seq_field_format() {
    wire::Alpha<20> f{};
    soup::formatSeq(f, 123456);
    CHECK_EQ(soup::parseSeq(f), 123456u);
    CHECK(f.chars[19] == '6');    // right-justified
    CHECK(f.chars[0] == ' ');     // space-padded on the left
}

void test_soup_stream_two_packets() {
    std::array<std::byte, 128> buf{};
    const unsigned char ouch[] = {0x55};
    const auto a = soup::packSequencedData(buf.data(), bytes(ouch, sizeof ouch));
    const auto b = soup::packServerHeartbeat(buf.data() + a.size());
    const std::size_t totalLen = a.size() + b.size();

    std::span<const std::byte> stream{buf.data(), totalLen};
    soup::Packet p{};
    const std::size_t c1 = soup::parse(stream, p);
    CHECK(c1 > 0);
    CHECK(p.type == soup::Type::SequencedData);

    const std::size_t c2 = soup::parse(stream.subspan(c1), p);
    CHECK_EQ(c1 + c2, totalLen);
    CHECK(p.type == soup::Type::ServerHeartbeat);
    CHECK_EQ(p.payload.size(), 0u);

    // A truncated tail yields 0 (needs more bytes).
    CHECK_EQ(soup::parse(stream.subspan(0, 2), p), 0u);
}

}  // namespace

int main() {
    test_mold_pack_and_read();
    test_mold_heartbeat_and_eos();
    test_mold_overflow_stops_cleanly();
    test_soup_sequenced_data_roundtrip();
    test_soup_login_accepted();
    test_soup_seq_field_format();
    test_soup_stream_two_packets();
    return abt::test::summary("framing_test");
}
