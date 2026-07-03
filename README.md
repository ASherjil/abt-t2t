# abt-t2t

**A wire-to-wire, hardware-timestamped tick-to-trade engine.** An ITCH/OUCH limit order
book driven over kernel bypass, with an exchange simulator on the other end of a 25 GbE
link, measuring the full path a real trading system pays — not an in-process microbenchmark.

## Why this is different

Almost every order-book repo measures **in-process**: `rdtsc` before the match, `rdtsc`
after. That number omits everything that dominates real latency — NIC RX, PCIe, the
kernel-bypass path, parsing, serialization, NIC TX. It benchmarks a data structure.

`abt-t2t` measures the **full wire-to-wire path** on the NIC's own PHC:

```
 Exchange simulator                          Device Under Test (DUT)
 [Intel XXV710-DA2 / ConnectX-4 Lx] ──25G DAC── [Solarflare X2522-PLUS]
   DPDK: ITCH 5.0 out / OUCH 5.0 in               ef_vi: ITCH in / OUCH out
                                                  t2t = TX_hwts − RX_hwts   (one PHC, no sync)
```

- **Real exchange protocols** — Nasdaq TotalView-ITCH 5.0 market data and OUCH 5.0 order
  entry, over MoldUDP64 / SoupBinTCP. Not a toy struct.
- **Kernel bypass** — the DPDK / AF_XDP / Verbs / ef_vi transports benchmarked in the
  sibling [`abtrda3`](../abtrda3) repo, reused behind a common `TxRing`/`RxRing` concept.
- **Hardware-timestamped tick-to-trade** — `TX_hwts − RX_hwts` on a single Solarflare
  X2522 PHC, so the number needs no clock sync and survives interview scrutiny. See
  [`docs/measurement-methodology.md`](docs/measurement-methodology.md).

## Status

Built incrementally. Current state:

| Component | Status |
|---|---|
| Project scaffold (C++20, CMake, Ninja presets) | ✅ |
| ITCH 5.0 wire layer (12 core messages, zero-copy overlay) | ✅ tested |
| OUCH 5.0 wire layer | ⏳ next |
| Limit order book / matching engine | ⏳ |
| MoldUDP64 / SoupBinTCP framing | ⏳ |
| DPDK exchange-simulator venue loop | ⏳ |
| Synthetic order-flow generator | ⏳ |
| ef_vi DUT + HW-timestamped t2t harness | ⏳ |

## Layout

```
include/abt/protocol/   ITCH/OUCH wire structs + big-endian overlay primitives
test/                   unit tests (in-tree harness; ctest)
docs/                   measurement methodology & design notes
cmake/                  shared warning/arch/opt flags
```

## Build & test

```bash
cmake --preset release
cmake --build --preset release
ctest --preset release
```

Requires a C++20 compiler (GCC 13+/Clang 16+), CMake ≥ 3.21, Ninja. Kernel-bypass
components additionally require DPDK and the corresponding NIC setup (added as they land).

## Design principles

Zero hot-path allocation, cache-line-aware layout, big-endian overlay structs decoded in
place, compile-time-sized messages, busy-poll on isolated cores. Conventions mirror the
sibling `abtrda3` transport benchmark repo.
