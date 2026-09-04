#include "TestHarness.hpp"

#include "t2t/util/MemLock.hpp"

using namespace abt;

namespace {

void test_core_interrupts_readable() {
    const util::CoreInterrupts c = util::coreInterrupts(0);
    CHECK(c.tlbShootdowns >= 0);
    CHECK(c.functionCalls >= 0);
    CHECK(c.reschedules >= 0);
    CHECK(c.timerTicks > 0);
}

void test_core_interrupts_monotonic() {
    const util::CoreInterrupts a = util::coreInterrupts(0);
    const util::CoreInterrupts b = util::coreInterrupts(0);
    CHECK(b.timerTicks >= a.timerTicks);
    CHECK(b.tlbShootdowns >= a.tlbShootdowns);
}

void test_unknown_core_is_zero() {
    const util::CoreInterrupts c = util::coreInterrupts(4096);
    CHECK_EQ(c.tlbShootdowns, 0L);
    CHECK_EQ(c.functionCalls, 0L);
    CHECK_EQ(c.reschedules, 0L);
    CHECK_EQ(c.timerTicks, 0L);
}

void test_thread_counters_readable() {
    const util::ThreadCounters t = util::threadCounters();
    CHECK(t.minorFaults >= 0);
    CHECK(t.voluntarySwitches >= 0);
}

}   // namespace

int main() {
    test_core_interrupts_readable();
    test_core_interrupts_monotonic();
    test_unknown_core_is_zero();
    test_thread_counters_readable();
    return abt::test::summary("memlock");
}
