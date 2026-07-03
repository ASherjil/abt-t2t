# Tick-to-Trade Measurement Methodology

This document defines exactly *what* `abt-t2t` measures, *how*, and *why the number is
defensible*. Most public order-book repos report an **in-process** latency: `rdtsc`
before the match, `rdtsc` after, print nanoseconds. That measures a data structure, not
a trading system — it omits NIC RX, PCIe, the driver / kernel-bypass path, parsing,
serialization, and NIC TX, which together dominate real latency. `abt-t2t` measures the
**full wire-to-wire path** on the NIC's own hardware clock.

## What "tick-to-trade" means here

Tick-to-trade (t2t) is the elapsed time from the market-data packet ("tick") that
triggers a decision arriving at the trading system, to the resulting order leaving it:

```
tick on the wire ──► NIC RX ──► parse ITCH ──► update book ──► strategy ──► build OUCH ──► NIC TX ──► order on the wire
└──────────────────────────────── measured window ────────────────────────────────┘
```

## The rig

A single Intel Core i9-11900K (fixed 5.00 GHz, SMT off) with PCIe x8/x8 bifurcation,
carrying exactly two NICs cabled back-to-back with a 25 GbE DAC:

```
 Exchange simulator                          Device Under Test (DUT)
 [Intel XXV710-DA2 / ConnectX-4 Lx] ──25G DAC── [Solarflare X2522-PLUS]
   DPDK: ITCH out / OUCH in                       ef_vi: ITCH in / OUCH out
                                                  t2t = TX_hwts − RX_hwts   (one PHC)
```

Both roles run on the same die on **isolated cores** (`isolcpus` / `nohz_full` /
`rcu_nocbs`), busy-polling. The exchange simulator's own latency is entirely *outside*
the measured window, so its jitter never enters the t2t number.

## The measurement: one clock, no sync

The X2522 timestamps every frame in **hardware**, at the PHY, driven by its PHC (PTP
hardware clock), asynchronously to the CPU:

- `RX_hwts` — PHC time the triggering tick was received.
- `TX_hwts` — PHC time the resulting order was transmitted.
- **`t2t = TX_hwts − RX_hwts`.**

Because both stamps come from the **same PHC on the same card**, this is one clock read
minus another: **no PTP, no cross-machine sync, no offset/drift correction.** This is the
single most important property of the design — it removes clock synchronization as a
source of error entirely.

We additionally record a software (`rdtsc`) timestamp at parse and at send, and publish
`t2t_hw − t2t_sw` — the RX/PCIe/driver/TX latency that in-process measurements silently
omit. That delta *is* the differentiator, quantified.

## The ruler vs. the runner (why co-location is not a measurement error)

Two distinct things must not be conflated:

1. **The instrument** — hardware timestamp differencing on one PHC — is exact regardless
   of CPU load. The recorded number always equals the true wire-to-wire time.
2. **The system under test** — the DUT's *actual* processing time — can rise under
   contention, because the exchange simulator shares the i9's L3 and memory controller
   and can evict the DUT's working set mid-decision.

So single-die co-location can make the DUT genuinely slower **in the tail** (p99.9+),
but it **cannot bend the ruler**: a 3 µs result under load is a real 3 µs, truthfully
recorded, not a measurement artifact. (Software `rdtsc` timestamps *can* be corrupted —
the thread may be descheduled between reading the clock and the actual send. Hardware
stamps have no such failure mode.)

We treat this honestly and turn it into a data point: t2t is reported **sim-quiescent
vs. sim at line rate**, and the delta is published as the *co-location tail cost*.
Measuring your own noisy-neighbour effect is a sign of rigour, not a weakness.

## How production HFT firms measure t2t (and how this compares)

Firms do not trust the trading NIC to measure itself in isolation. The canonical rig
puts the measurement on a **third, passive** device:

```
market data ──[Solarflare]──┬──(fiber)──► exchange
                            │
                        [passive TAP]         ← optical splitter, copies both directions
                            │
                    [FPGA timestamper]        ← Arista 7130 / MetaWatch, Exablaze /
                                                Cisco Nexus 3550-F, Metamako, Corvil /
                                                Pico, cPacket, Endace — one clock, both dirs
```

A passive tap mirrors every frame (in both directions) to an FPGA capture appliance that
hardware-timestamps them on one clock. Two properties: **non-intrusive** (zero load on
the trading path — no Heisenberg effect) and **single-clock** (the same principle we use).

`abt-t2t` is the legitimate **single-box equivalent**: the X2522 both trades and stamps
its own RX/TX on one PHC. AMD/Solarflare **Onload ships TX timestamping specifically for
tick-to-trade monitoring**, so stamping on the trading NIC's PHC is a shipped production
feature, not a lab shortcut. The only thing we give up versus an external tap is full
non-intrusiveness — and hardware stamping is nearly free, so that gap is small and
quantified above. An external tap + FPGA appliance is a future upgrade, not a correctness
requirement.

## Reporting

Latency is recorded into an HdrHistogram (reusing abtrda3's `SingleRecorder`) with
coordinated-omission-aware sampling, warmup discard, and long-soak capacity. Every run
publishes Min / p50 / p99 / p99.9 / p99.99 / Max / Mean, plus the two headline deltas:

- `t2t_hw − t2t_sw` — the wire/PCIe/driver cost hidden from in-process benchmarks.
- `t2t(loaded) − t2t(quiescent)` — the single-die co-location tail cost.
