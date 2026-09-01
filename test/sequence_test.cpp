#include "TestHarness.hpp"

#include <cstdint>

#include "t2t/dut/SequenceTracker.hpp"
#include "t2t/protocol/MoldUdp64.hpp"

using namespace abt;
using R = dut::SequenceTracker::Result;

namespace {

void test_in_order() {
    dut::SequenceTracker s;
    CHECK(!s.started());
    CHECK(s.onPacket(1, 3) == R::InOrder);
    CHECK(s.started());
    CHECK_EQ(s.expected(), 4u);
    CHECK(s.onPacket(4, 1) == R::InOrder);
    CHECK(s.onPacket(5, 2) == R::InOrder);
    CHECK_EQ(s.expected(), 7u);
    CHECK_EQ(s.gaps(), 0u);
}

void test_gap() {
    dut::SequenceTracker s;
    (void)s.onPacket(1, 2);
    CHECK(s.onPacket(10, 1) == R::Gap);
    CHECK_EQ(s.gaps(), 1u);
    CHECK_EQ(s.missed(), 7u);
    CHECK_EQ(s.expected(), 11u);
    CHECK(s.onPacket(11, 1) == R::InOrder);
}

void test_stale_and_overlap() {
    dut::SequenceTracker s;
    (void)s.onPacket(1, 5);
    CHECK(s.onPacket(3, 1) == R::Stale);
    CHECK_EQ(s.stale(), 1u);
    CHECK_EQ(s.expected(), 6u);
    CHECK(s.onPacket(4, 4) == R::InOrder);
    CHECK_EQ(s.expected(), 8u);
}

void test_heartbeat_and_end() {
    dut::SequenceTracker s;
    (void)s.onPacket(1, 2);
    CHECK(s.onPacket(3, mold::kHeartbeat) == R::InOrder);
    CHECK_EQ(s.expected(), 3u);
    CHECK(s.onPacket(3, 1) == R::InOrder);
    CHECK(s.onPacket(4, mold::kEndOfSession) == R::InOrder);
    CHECK_EQ(s.expected(), 4u);
    CHECK(s.onPacket(9, mold::kHeartbeat) == R::Gap);
    CHECK_EQ(s.missed(), 5u);
}

void test_reset() {
    dut::SequenceTracker s;
    (void)s.onPacket(1, 1);
    (void)s.onPacket(5, 1);
    s.reset();
    CHECK(!s.started());
    CHECK_EQ(s.gaps(), 0u);
    CHECK(s.onPacket(100, 1) == R::InOrder);
}

}   // namespace

int main() {
    test_in_order();
    test_gap();
    test_stale_and_overlap();
    test_heartbeat_and_end();
    test_reset();
    return abt::test::summary("sequence");
}
