# Tick-to-Trade Measurement Methodology

This document defines what `abt-t2t` measures, how the number is produced, and what is
inside and outside it. It also documents the two engines in enough detail to judge the
result: the feed handler under test and the exchange simulator that drives it, with their
data structures and the cost of every operation on a packet path.

The short version. Most public order-book projects report an in-process latency: read a
cycle counter before the match, read it again after, print the difference. That measures a
data structure. It omits the receive path, the transfer into host memory, the decode, the
serialisation and the send, which together are most of what a trading system pays for a
tick. `abt-t2t` measures the full wire-to-wire path on the network card's own clock.

---

## Contents

1. [What tick-to-trade means here](#1-what-tick-to-trade-means-here)
2. [The rig](#2-the-rig)
3. [The measurement: one clock, no synchronisation](#3-the-measurement-one-clock-no-synchronisation)
4. [The ruler and the runner](#4-the-ruler-and-the-runner)
5. [How production firms measure, and how this compares](#5-how-production-firms-measure-and-how-this-compares)
6. [Kernel bypass](#6-kernel-bypass)
7. [The feed handler](#7-the-feed-handler)
   - [7.1 Dispatch: from packet to symbol](#71-dispatch-from-packet-to-symbol)
   - [7.2 The per-symbol book](#72-the-per-symbol-book)
   - [7.3 The decision](#73-the-decision)
   - [7.4 The cold shard](#74-the-cold-shard)
8. [The exchange simulator](#8-the-exchange-simulator)
   - [8.1 One book per symbol, holding both sides of the fiction](#81-one-book-per-symbol-holding-both-sides-of-the-fiction)
   - [8.2 Two paths into the same book](#82-two-paths-into-the-same-book)
   - [8.3 How the DUT gets filled](#83-how-the-dut-gets-filled)
   - [8.4 Pacing, and how fidelity is proven](#84-pacing-and-how-fidelity-is-proven)
9. [Threads and cores](#9-threads-and-cores)
10. [The market data](#10-the-market-data)
11. [Reporting](#11-reporting)
12. [What is not claimed](#12-what-is-not-claimed)

---

## 1. What tick-to-trade means here

Tick-to-trade is the elapsed time from the market-data packet that triggers a decision
arriving at the trading system, to the resulting order leaving it.

```
               ┌────────────── t2t = tx stamp − rx stamp ──────────────┐
               │                                                       │
          rx hw stamp                                             tx hw stamp
               │                                                       │
               ▼                                                       ▼
 ITCH   ┌─────────────┐  DMA   ┌──────┐  load  ┌────────┐ CTPIO ┌─────────────┐  OUCH
 ──────►│   NIC rx    │───────►│ DDR4 │───────►│ core 6 │──────►│   NIC tx    │──────►
        └─────────────┘hugepage└──────┘  DRAM  └────────┘  PIO  └─────────────┘
                                                    │
                                                    └──► MoldUDP64 frame, sequence check
                                                         ITCH 5.0 decode in place
                                                         order book update
                                                         quote decision
                                                         OUCH 5.0 build
```

**Inside the interval.** The card writing the frame into host memory across PCIe; the
core's first read of that frame, which misses all the way to DRAM because the data arrived
by DMA and no core has touched it; MoldUDP64 framing and sequence validation; ITCH 5.0
decode; the order book update; the quoting decision; order-manager reconciliation; OUCH
5.0 encoding; and the write of the frame into the card.

**Outside the interval.** The cable in both directions, and everything the exchange
simulator does. The simulator's own latency and jitter cannot enter the number, because
both stamps are taken at the device under test's own port.

**Only packets that cause an order are timed.** A market-data packet that does not move the
quote produces no order and no sample. The published distribution is therefore the
distribution of *decisions*, not of packet arrivals, which is the quantity a trading desk
cares about. Section 3 gives the exact rule.

---

## 2. The rig

One machine, two network cards, cabled to each other. No switch, no second host.

```
 exchange simulator                                 device under test
 ConnectX-4 Lx, cx0  ◄──────── 25 GbE DAC ────────► Solarflare X2522-Plus, enp1s0f1
 libibverbs raw QP                                  ef_vi (headline) or Onload (comparison)
 core 4                                             core 6
```

| | |
|---|---|
| CPU | Intel Core i9-11900K, 8 cores, SMT disabled |
| Memory | 48 GB DDR4 |
| DUT NIC | AMD/Solarflare X2522-Plus, 25 GbE, ef_vi and Onload |
| Simulator NIC | Mellanox ConnectX-4 Lx, 25 GbE, libibverbs |
| Storage | NVMe SSD, ext4 |
| Kernel | 6.9.12, low-latency configuration |

Kernel parameters that matter to the measurement:

- `isolcpus=domain,managed_irq,3-7` removes cores 3 to 7 from the scheduler's load
  balancing and keeps managed device interrupts off them.
- `irqaffinity=0,1` pins every other interrupt to the two housekeeping cores.
- `nohz_full` and `rcu_nocbs` over the isolated range stop the timer tick and move RCU
  callback processing off those cores.
- `nosmt` so a hot core owns its whole physical core.
- `intel_idle.max_cstate=1 processor.max_cstate=1` and the performance governor keep the
  cores out of deep sleep states, whose exit latency would appear in the tail.
- `clocksource=tsc tsc=reliable`, `nowatchdog`, `nmi_watchdog=0`, `mce=ignore_ce`,
  `pcie_aspm=off`, `transparent_hugepage=never`.
- Explicit 1 GiB and 2 MiB hugepage pools for the book arena and the packet buffers.

Both roles run on the same die. Section 4 explains why that is honest, and what it costs.

---

## 3. The measurement: one clock, no synchronisation

The X2522 timestamps frames in hardware, at the port, from its own PTP hardware clock,
asynchronously to the CPU.

- `rx stamp` is the clock reading when the triggering market-data packet arrived.
- `tx stamp` is the clock reading when the resulting order was transmitted.
- `t2t = tx stamp − rx stamp`.

Both readings come from the same oscillator on the same card. The result is one clock read
minus another, so there is no PTP session, no cross-machine synchronisation, no offset or
drift correction, and no calibration constant. Clock synchronisation is removed as a source
of error rather than bounded.

**How each stamp reaches the application.**

On the ef_vi path, the receive stamp arrives with the frame's completion event and the
transmit stamp arrives on the send completion event for the frame the application just
pushed, so the pairing is unambiguous.

On the Onload path the same information travels the standard Linux socket route. Receive
stamps come from `SO_TIMESTAMPING` with `RX_HARDWARE | RAW_HARDWARE` as an `scm_timestamping`
control message. Transmit stamps come back on the socket error queue with
`SOF_TIMESTAMPING_TX_HARDWARE | RAW_HARDWARE | OPT_ID | OPT_TSONLY | OPT_ID_TCP`, keyed by
the byte offset of the order in the stream, which is how a transmit stamp is matched to the
order that produced it over a byte-stream protocol.

**The measurement does not perturb what it measures.** In the shipped hardware-timestamp
build there is no timestamp-counter instruction anywhere on the packet path. That is
verified by disassembling the binary that produced the numbers and confirming that no
`rdtsc` or `rdtscp` appears in the project's own code. A separate build keeps a software
timing path for development; it is compiled out of the build used for published results.

**What counts as a sample** is defined exactly. When a market-data packet causes the quoter
to move its quote, the interval runs from that packet's receive stamp to the transmit stamp
of the first order it produced. A packet that does not move the quote produces no order and
no sample. When a quote update sends both sides, only the first order is timed: the second
is queued behind it, so timing it would measure the first send rather than the decision.
Orders that exist only to keep the path warm before the open carry no reference through the
transmit path and are never timed, which is why the order count in a report is larger than
the sample count. No measured sample is ever discarded: there is no warm-up cut, no
sampling, and no coordinated-omission correction.

---

## 4. The ruler and the runner

Two things must not be conflated.

**The instrument** is hardware timestamp differencing on one clock. It is exact regardless
of CPU load. The recorded value always equals the true time between the packet arriving at
the port and the order leaving it.

**The system under test** can genuinely be slower under contention. The simulator shares
the die's last-level cache and memory controller with the device under test and can evict
part of its working set mid-decision.

Single-box co-location can therefore make the device under test genuinely slower in the
tail. It cannot bend the ruler. A slow sample is a real slow sample, truthfully recorded,
not a measurement artefact. This is the practical advantage of hardware stamps over
software ones: a software timestamp can be corrupted by the thread being descheduled
between reading the clock and the actual send, and the recorded number is then simply
wrong. A hardware stamp has no such failure mode, because the card writes it when the
frame crosses the port whatever the CPU was doing.

---

## 5. How production firms measure, and how this compares

Firms do not usually trust the trading card to measure itself. The canonical rig puts the
measurement on a third, passive device.

```
 market data ──[trading NIC]──┬──(fiber)──► exchange
                              │
                         [passive tap]        optical splitter, copies both directions
                              │
                      [FPGA timestamper]      Arista 7130 / MetaWatch, Cisco Nexus 3550-F,
                                              Corvil, cPacket, Endace: one clock, both directions
```

The tap mirrors every frame in both directions to a capture appliance that timestamps them
in hardware on a single clock. Two properties matter: it is non-intrusive, adding no load
to the trading path, and it is single-clock, which is the same principle used here.

`abt-t2t` is the single-box equivalent. The X2522 both trades and stamps its own receive
and transmit on one clock. Stamping on the trading card's own clock is a shipped production
feature rather than a lab shortcut: Onload ships transmit timestamping specifically for
tick-to-trade monitoring. The property given up against an external tap is full
non-intrusiveness, and hardware stamping is close to free, so the gap is small. An external
tap and capture appliance would be an upgrade, not a correctness requirement.

For calibration against a published, audited result, see the STAC-N1 benchmark report
**SFC180604b** (Solarflare X2522, Onload, UDP sockets). The Onload row in this project is
the closest comparable configuration and should be read against it.

---

## 6. Kernel bypass

Two transports are measured, with the same code above the transport boundary.

**ef_vi (headline).** The card's lowest-level user-space interface. The application owns
the receive and transmit descriptor rings directly. There is no socket, no protocol stack,
no system call and no interrupt on the data path. Receive buffers are pinned 2 MiB
hugepages registered with the card, so the DMA target is physically resident and covered by
few TLB entries. The receive loop polls, so a packet is noticed as soon as it lands rather
than when an interrupt is delivered.

Orders are sent with **CTPIO**, cut-through programmed I/O: the core writes the frame into
the card across PCIe and the card begins putting bytes on the wire before the whole frame
has been handed over, rather than the application writing a descriptor, ringing a doorbell,
and waiting for the card to fetch the frame back over DMA. The mode is *in-order*, which
guarantees the card never reorders around a fallback. Whether CTPIO was actually used is
not assumed: every run reports `ctpio_wins` against `ctpio_fallbacks` from the card's own
counters, and a published run has zero fallbacks.

On this path OUCH 5.0 is carried directly over UDP. There is no TCP stack in the send path
at all, which is the point of the row.

**Onload (comparison).** Ordinary BSD sockets, with no vendor API anywhere in the source.
The same binary runs accelerated by launching it under Onload, which intercepts the socket
calls and services them from user space against the same card. Market data arrives on a UDP
socket read with `recvmmsg` in batches; orders go out on a TCP socket carrying SoupBinTCP
framing around OUCH 5.0. The row exists to answer "what does this cost if you do not write
against the card directly", and to be checkable against the STAC result above.

The Onload configuration is verified rather than asserted. Each run records the environment
used and the card's own counters, and a published run shows: packets received in user space
with essentially none taking the kernel path, transmit events all served by CTPIO, zero
transmit DMA doorbells, and interrupt counts in the low single digits across tens of
millions of packets, all of which come from startup and shutdown.

**Where the transports come from.** The ring abstractions and the ef_vi, Verbs, DPDK and
AF_XDP backends were built and benchmarked in the sibling project
[ABTRDA3](https://github.com/ASherjil/ABTRDA3), which measures the transports themselves in
isolation: round-trip latency, packet rate and loss per backend on this same rig. `abt-t2t`
consumes that work behind a common ring concept (`src/third_party/abtrda3/RingConcepts.hpp`)
and adds the hardware-timestamp variant and the trading application on top. If the question
is "how fast is the transport", that is the repository to read. If the question is "how fast
is the trading system", it is this one.

**Simulator side.** The simulator drives the ConnectX-4 Lx through libibverbs with a raw
packet queue pair, building Ethernet, IPv4 and UDP headers itself. It is kernel bypass too,
for a different reason: the simulator must not fall behind the tape, and a socket path
cannot keep up with the open of a NASDAQ session.

---

## 7. The feed handler

One thread on one isolated core. Between the start of the session and the end of it, that
thread makes no system call, takes no lock, allocates nothing and writes nothing to a file.
Everything below is on that thread unless stated otherwise.

### 7.1 Dispatch: from packet to symbol

A MoldUDP64 packet carries a session, a sequence number, a message count, and that many
ITCH 5.0 messages back to back.

`SequenceTracker` compares the packet's sequence against the expected value and classifies
it as in-order, a gap or stale, counting each. A gap is surfaced in the report; it is never
silently absorbed. The tracker is a handful of integers, O(1).

Every ITCH message carries a **stock locate**, a 16-bit exchange-assigned symbol index.
`BookTable` exploits that directly: it holds a flat array of 65,536 entries indexed by the
locate, so finding a symbol's book is an array index, not a hash. There is no string
comparison and no hashing anywhere on the packet path; symbol names are resolved once, when
the stock directory arrives at the start of the session, into a name-keyed map that the hot
path never touches.

Hot and cold discrimination is a bitmap: 65,536 bits, one per locate, so `isHot(locate)` is
a shift, a mask and a single cache line. Quoted symbols are handled on this core. Everything
else is handed to the cold shard.

| operation | structure | cost |
|---|---|---|
| sequence check | four integers | O(1) |
| locate to book | flat array of 65,536 entries | O(1), one indexed load |
| hot or cold | 65,536-bit bitmap | O(1), one load and a mask |
| symbol name to book | hash map, session setup only | not on the packet path |

### 7.2 The per-symbol book

The device under test keeps an **L2 book with order-reference tracking**, which is exactly
what the ITCH protocol provides and exactly what a quoter needs. It deliberately does not
keep per-level order queues, because it never needs to answer a question about queue
position. (The simulator does, and pays for it: section 8.)

Each `BookBuilder` holds:

- **Two flat size arrays**, one per side, indexed by price tick, holding aggregate resting
  shares at each level. Four bytes per level per side. Price-to-index is a subtraction and a
  division by the tick size; the division uses a precomputed reciprocal multiply
  (`util::DivBy`, the Granlund-Montgomery method) because a hardware divide is over twenty
  cycles and sits on the critical path of every add and every remove.
- **A two-level hierarchical bitmap per side** (`LevelBits`): one bit per price level, plus
  a summary bit per 64 levels. When the best level empties, the next best is found with
  hardware bit-scan instructions over at most a couple of 64-bit words rather than by walking
  the price array. This is the difference between an O(1) and an O(band width) operation on a
  quiet book with a wide band.
- **An order-reference map** (`util::FlatHashMap`): open addressing, linear probing, and
  backward-shift deletion (Knuth 6.4 Algorithm R) so the table stays free of tombstones under
  the constant insert-and-erase churn of a real feed. A lookup touches one cache line and
  follows a short probe run, against the two dependent pointer chases a chained map pays. The
  value is packed into eight bytes: a four-byte price, thirty bits of shares, a side bit and a
  bit marking the order as the DUT's own. A slot is therefore sixteen bytes, four to a cache
  line.
- **Incrementally maintained best bid and ask**, plus a version counter that increments only
  when the top of book actually changes, so the quoter can decide whether to re-evaluate by
  reading one integer.

Capacity is chosen before the session starts, not discovered during it. Each quoted symbol's
map is sized from a profile of the previous session's peak resting order count, at twice that
peak, rounded to a power of two. The profile also supplies a reference price used to anchor
the level arrays. Maps still count their own growth, and a run reports it: a published run
shows zero rehashes, so no resize happened while the clock was running.

**Banding.** A symbol's level arrays cover a price band around an anchor, widened
proportionally for expensive names and capped. Orders outside the band are counted and
parked rather than booked, and a genuine move of the market outside the band triggers a
re-anchor that rebuilds the arrays. Re-anchors and out-of-band adds are both counted and
reported. This bounds memory per symbol without bounding correctness: the alternative, a
book that covers every representable price, would be gigabytes per symbol.

Messages handled on the book path: Add Order, Add Order with MPID, Order Executed, Order
Executed with Price, Order Cancel, Order Delete, Order Replace, Trade and Cross Trade.

| ITCH message | work | cost |
|---|---|---|
| Add Order (A, F) | map insert, level add, bit set, best update | O(1) expected |
| Order Executed (E, C) | map find, level decrement, bit clear if empty | O(1) |
| Order Cancel (X) | map find, partial decrement | O(1) |
| Order Delete (D) | map erase with backward shift, level decrement | O(1) |
| Order Replace (U) | erase then insert | O(1) |
| new best after depletion | two-level bit scan | O(1) typical; bounded by band width / 4096 words |
| re-anchor | rebuild level arrays | O(band width), counted, rare |

All book memory for the quoted symbols comes from a 1 GiB hugepage arena through a
polymorphic memory resource, so the hot books sit behind a single TLB entry and nothing on
the packet path calls the allocator.

### 7.3 The decision

`QuoterStrategy` recomputes a fair value from the size-weighted micro-price, which leans
toward the side with less resting size, places a two-sided quote a configured half-spread
around it, and shifts both quotes by an inventory skew: long inventory pushes the quotes
down so the system leans net seller, short pushes them up. Prices are rounded to the tick
grid, bid down and ask up, kept from crossing, and clamped to the book's band. This is
reservation-price and inventory-skew behaviour of the kind market makers actually run,
without hard position limits.

The strategy keeps the fair-value interval that would produce its current quote, so a
market-data update that moves fair value within that interval returns "no change" without
touching the order manager. The work is a handful of floating-point operations and two
comparisons, O(1). The published run quotes 100 shares a side, one tick either side of fair
value, with a skew of one tick per thousand shares of inventory.

This is the decision, as it runs on the hot core (`src/t2t/dut/QuoterStrategy.cpp`):

```cpp
bool QuoterStrategy::onBook(const BookBuilder& book, const Account& acct, QuoteTargets& out) noexcept {
    const Price bb = book.bestBid();
    const Price ba = book.bestAsk();
    if (bb == kNoPrice || ba == kNoPrice) {
        forget();
        out = QuoteTargets{};
        return true;
    }
    const std::uint64_t bidSz = book.sizeAt(Side::Buy, bb);
    const std::uint64_t askSz = book.sizeAt(Side::Sell, ba);
    const std::uint64_t total = bidSz + askSz;
    const double        num   = total == 0 ? static_cast<double>(bb) + static_cast<double>(ba)
                                           : static_cast<double>(static_cast<std::uint64_t>(bb) * askSz +
                                                                 static_cast<std::uint64_t>(ba) * bidSz);
    const double        den   = total == 0 ? 2.0 : static_cast<double>(total);
    if (num > m_fairLo * den && num < m_fairHi * den) {
        return false;
    }
    const double fair = num / den;
    const double tick = static_cast<double>(m_cfg.tickWire);
    const double half = static_cast<double>(m_cfg.halfSpreadTicks) * tick;
    const double skew = -static_cast<double>(acct.position) * m_cfg.skewTicksPerUnit * tick;

    const Price origin   = book.bandLow();
    Price       bidPrice = roundDownToTick(fair - half + skew, origin);
    Price       askPrice = roundUpToTick(fair + half + skew, origin);
    const bool  plain    = bidPrice > origin && askPrice > origin && bidPrice < askPrice &&
                       bidPrice >= book.bandLow() && askPrice <= book.bandHigh();
    if (plain) {
        m_fairLo = std::max(static_cast<double>(bidPrice) + half - skew,
                            static_cast<double>(askPrice - m_cfg.tickWire) - half - skew);
        m_fairHi = std::min(static_cast<double>(bidPrice + m_cfg.tickWire) + half - skew,
                            static_cast<double>(askPrice) - half - skew);
    } else {
        forget();
    }
    if (bidPrice >= askPrice) {
        bidPrice = askPrice - m_cfg.tickWire;
    }
    bidPrice = clampToBand(bidPrice, book);
    askPrice = clampToBand(askPrice, book);

    out.quoteBid = true;
    out.bidPrice = bidPrice;
    out.bidQty   = m_cfg.quoteQty;
    out.quoteAsk = true;
    out.askPrice = askPrice;
    out.askQty   = m_cfg.quoteQty;
    return true;
}
```

Reading it top to bottom: the micro-price weights each side's best price by the *other*
side's resting size, so a thin ask pulls fair value up. The early `return false` is the
hysteresis: `m_fairLo` and `m_fairHi` bound the fair values that round to the quote already
resting, and most updates land inside them. The skew term moves both quotes against the
inventory. Rounding is asymmetric, bid down and ask up, so the quote never narrows past the
configured spread by rounding. The interval is only kept when the quote is "plain", meaning
neither side touched the band edge, because a clamped quote no longer corresponds to a
clean fair-value interval.

`OrderManager` holds one slot per symbol per side with an explicit state machine: idle,
pending-new, live, pending-replace, pending-cancel. Reconciling a desired quote against the
current state produces at most two outbound messages, one per side, encoded into fixed
64-byte buffers that are members of the manager, so no allocation happens and no copy is
made beyond the encode itself. The state machine is what prevents a second order being sent
against an unacknowledged one. Acks, fills and rejects arriving from the venue are applied
to the same slots, and every outcome is counted, including each category of reject.

The point of having a real strategy is not profit. It is that the measured path contains a
decision computed from a real book on every tick, with a branch that has to be right.

### 7.4 The cold shard

Quoting eight symbols while ignoring the rest of the tape would make the hot path fast for
the wrong reason. Every other symbol in the session, roughly twelve and a half thousand of
them, is fully booked on a second isolated core.

The hot thread pushes a frame reference, not a copy, onto a single-producer single-consumer
ring. The cold core reads the bytes in place out of the receive buffers. The ring depth is
sized against the card's receive ring with a safety margin so that a buffer cannot be
recycled by the card while the cold core is still reading it, and the shard counts any frame
it finds stale. A published run shows zero stale frames and a peak ring occupancy of single
digits against a capacity of 8,192 across tens of millions of packets: the hand-off is
nowhere near saturation.

Cold books use the same structures with a narrower band and a smaller initial map. At the
busiest point of a session that side carries several million live orders across the
directory.

---

## 8. The exchange simulator

The simulator has a harder correctness problem than the feed handler and an easier latency
problem. It must reproduce a real NASDAQ session faithfully, in real time, and it must
decide what the device under test's orders would actually have done in that session.

### 8.1 One book per symbol, holding both sides of the fiction

Each `Venue` owns a full **L3 price-time-priority order book** (`OrderBook`) that contains
both the orders from the tape and the device under test's own orders, in the same structure,
interleaved in true arrival order.

- **Flat level arrays per side**, indexed by price tick, sixteen bytes per level: aggregate
  quantity, head and tail handles, and an order count.
- **An intrusive doubly-linked FIFO per level**, threaded through a **single pooled array of
  orders**. An order is 32 bytes and is addressed by a 32-bit handle, never a pointer, so the
  pool can grow without invalidating anything. Freed slots are recycled through a free list
  threaded through the same `next` field, so there is no allocator traffic in steady state.
- **Incrementally maintained best bid and ask.**

Because a handle is an index, cancelling or reducing an order is a direct array access
followed by an unlink: no search of the level, no search of the book.

The venue also keeps two `FlatHashMap`s: order reference to live-order record, which is what
the tape's later messages refer to, and the DUT's OUCH user reference to order reference,
which is what its cancels and replaces refer to. The live map is sized ahead of the session
from the day's peak, because a resize costs milliseconds and a millisecond of the simulator
stalling is a millisecond of the tape arriving late.

### 8.2 Two paths into the same book

**The tape.** Add, execute, cancel, delete and replace messages from the file are applied to
the book directly. An add is *placed*, not matched: the file already tells us what executed,
so re-deriving it would be both wasteful and wrong. This is a faithful reconstruction of the
real book, order by order, including queue position.

**The device under test.** Orders arriving over OUCH are matched properly, against that same
book, by price-time priority.

### 8.3 How the DUT gets filled

This is the part that decides whether the simulation is worth anything, and it is where most
simulators cheat. Three mechanisms, all driven by queue position in the reconstructed book:

- **Impact fills.** A DUT order that crosses the book takes real resting liquidity and pays
  the real price. The matching engine handles it like any aggressive order.
- **Cross fills.** An incoming order from the tape that is marketable against a DUT order
  resting in the book fills it, before the remainder is placed. A resting quote is therefore
  hit by the same flow that hit the real book.
- **Shadow fills.** When the tape reports that a real order was executed, the simulator asks
  whether the DUT's order at that price level was *ahead of it in the queue*. If it was, the
  DUT is filled, because in the real session that liquidity would have been taken first. The
  check walks the level's FIFO until it finds either the DUT's order or the real one, which is
  the one linear scan in the whole design; it is bounded by queue position and happens only
  when a level containing a DUT order sees an execution.

Self-trades, over-reductions, unknown references and out-of-band prices are all counted
rather than silently dropped, and appear in the run's status line.

| operation | work | cost |
|---|---|---|
| tape add | free-list pop, FIFO append, best update | O(1) |
| tape execute or cancel | map find, handle deref, quantity reduce | O(1) |
| tape delete | map erase, unlink by handle | O(1) |
| tape replace | delete then place | O(1) |
| DUT order, resting | as tape add, plus client tracking | O(1) |
| DUT order, marketable | match down the book | O(orders filled) |
| new best after level empties | linear rescan of the price array | O(distance to next level) |
| shadow-fill check | walk the level FIFO | O(queue position ahead) |

The simulator's best-price rescan is linear where the feed handler's is a bit scan. That is
deliberate rather than an oversight: the simulator is not on the measured path, and its cost
is already accounted for by the pacing check in the next section.

### 8.4 Pacing, and how fidelity is proven

The file is memory-mapped, not read, and the whole file is resident in the page cache before
a run starts. Messages are dispatched when their own ITCH timestamp says they should be,
against a monotonic anchor taken at the start of the paced window.

Every message records how late it was against its scheduled time. The run reports the maximum
lateness and the count of messages more than a millisecond late. This is the simulator's
honesty check, and it is a headline number of every run: if the tape did not go out on time,
the session the device under test saw was not the session on the file, and the latency
distribution is measuring something else. A clean run shows lateness in the tens of
microseconds and zero messages over a millisecond.

That check earned its place. An earlier version of the simulator wrote its own progress line
from the thread pacing the tape. Roughly one run in three, that write blocked for several
milliseconds inside the filesystem journal, and the tape went out in a burst afterwards. It
was found precisely because the lateness counter made it visible, and fixed by moving all
output off the paced thread, which is the arrangement described in section 9.

---

## 9. Threads and cores

Five threads across two processes for the ef_vi measurement, six for the Onload one.

| process | thread | core | what it does | what it may not do |
|---|---|---|---|---|
| simulator | replay and match | 4 | paces the tape, applies it to the books, matches DUT orders, drives the card | any system call between the first message and the last |
| simulator | status | 3 | pops counter snapshots from a lock-free ring once a second, formats and writes them | touch a book or a packet |
| DUT | feed and quote | 6 | polls the card, validates sequence, decodes, books the quoted symbols, decides, encodes, sends | allocate, lock, log, or call the kernel |
| DUT | cold shard | 5 | books every unquoted symbol from frame references read in place | send anything, or block the hot thread |
| DUT | histogram | 3 | drains latency samples into HdrHistogram, writes status and interval lines and the histogram log | appear anywhere on a packet path |
| DUT | order entry | 7 | Onload row only: owns the TCP socket, sends orders, matches transmit stamps, reads acks | exist at all on the ef_vi row |

Every core from 4 upward carries exactly one thread, and that thread never blocks during a
session. Core 3 is the housekeeping core for the measurement itself; both threads on it exist
so that the other three never have to format a number or touch a file. Cores 0 to 2 carry the
kernel, every device interrupt, and the run script.

The hand-offs are all single-producer single-consumer rings with no locks: latency samples
from the hot thread to the histogram thread, frame references from the hot thread to the cold
shard, counter snapshots from each engine to its reporting thread, and on the Onload row,
orders from the feed thread to the order thread and acknowledgements back. Every ring counts
its own drops, and a published run shows none.

---

## 10. The market data

| | |
|---|---|
| Source | NASDAQ TotalView-ITCH 5.0, BinaryFILE format |
| Session | 15 May 2026, full day |
| Messages | 960,764,857 |
| Symbols | 12,655 in the stock directory |
| Size | 29.3 GB uncompressed |
| First message | 03:02:33 |
| Replay | wall-clock pace, one pass, no loop |

This is a real, unmodified session: every add, execute, cancel, delete, replace, trade, cross,
trading action and system event that NASDAQ published that day, in the order it published
them. Nothing is synthesised, sampled or smoothed.

The file is validated before use by a separate tool in this repository, `itch_replay`, which
replays every message of the session through the same book-building code the device under
test runs and reports what it found, per symbol and in total: references that were executed
or cancelled without ever having been added, reductions larger than the resting quantity,
books that ended up crossed or locked, adds outside the price band, re-anchors, and the peak
live order count. This is a semantic check rather than a framing check, and it is the same
pass that writes the symbol profile used to size the books before a run.

The whole file is held in the page cache during a run, which the 48 GB of memory permits. No
disk read occurs on any path that matters. This was measured rather than assumed: an earlier
attempt to pre-fault the mapping caused multi-millisecond stalls as the kernel reclaimed
mapped pages, and was removed in favour of letting the page cache hold the file.

A full-day run covers the pre-market session, the open, the whole regular session and the
close, so the reported distribution includes the open, which is the worst few seconds of the
day for a feed handler and the part a benchmark starting at 10:00 quietly omits.

The data is not redistributed with this repository. See `data/README.md`.

---

## 11. Reporting

Samples go into HdrHistogram at nanosecond resolution with three significant figures. The hot
thread pushes a raw sample onto a lock-free ring; the histogram thread converts and records it.

Every run produces a directory under `results/` containing:

- `dut.log`, the device under test's per-second status lines, a percentile summary for every
  sixty-second interval, and the end-of-run report: the full-session latency table, scheduling
  and interrupt counters for the hot core, memory residency, order-manager outcomes per symbol,
  feed integrity, cold-shard statistics, and the card's own transmit and receive counters.
- `sim.log`, the simulator's per-second status lines including pacing lateness, and its
  end-of-run report.
- `dut.hlog`, the HdrHistogram interval log, one record per recorder per minute, which is what
  the full-session histogram is built from.
- `versions.txt`, the commit, the build preset, the transports, the replay window, and the
  environment used.

The published figures are minimum, median, p99, p99.9, p99.99, p99.999 and maximum, in
nanoseconds, with the sample count. The mean is not published: it is not a quantity anyone
trades on, and it hides exactly the tail the rest of the table exists to show.

A result is only meaningful next to the evidence that the run was clean. Any published run
should be read together with the counters that say the measurement was not disturbed: zero
involuntary context switches on the hot core, zero packet gaps, zero card receive discards,
zero CTPIO fallbacks, zero ring drops, zero map rehashes, and simulator lateness in the
microseconds.

For the published full-session run (15 May 2026, 03:02 to 16:00, `ef_vi`), those counters
were:

| check | value |
|---|---|
| samples | 5,413,430 |
| market-data packets received | 936,773,700 |
| sequence gaps, missed, stale | 0, 0, 0 |
| card receive discards, drops | 0, 0 |
| hot-core context switches, involuntary and voluntary | 0, 0 |
| hot-thread page faults during the run | 0 |
| CTPIO wins, fallbacks | 12,460,232, 0 |
| transmit hardware stamps lost | 0 |
| hot-book rehashes, re-anchors | 0, 0 |
| cold-shard ring drops, stale frames, peak depth | 0, 0, 4 of 8,192 |
| simulator maximum lateness, messages over 1 ms late | 149 µs, 0 |
| simulator transmit drops | 0 |

The latency table for that run, in nanoseconds: min 966, p50 1,089, p99 1,312, p99.9 1,504,
p99.99 1,709, p99.999 1,992, max 2,703.

---

## 12. What is not claimed

- SoupBinTCP is implemented to the extent the measurement needs: login, sequenced and
  unsequenced data. There are no heartbeats and no recovery session.
- On the ef_vi row, OUCH 5.0 is carried over raw UDP rather than over TCP. That is the
  configuration being measured, and it is stated in the result rather than hidden in it.
- Eight symbols are quoted. The rest of the directory is booked but not traded.
- The device under test and the simulator share one machine. Section 4 sets out what that can
  and cannot do to the number.
- This is a measurement rig, not a production trading system. There is no risk system, no
  position limit, no drop-copy, no failover and no clearing.
