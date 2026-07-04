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
| OUCH 5.0 wire layer (9 core messages + TagValue appendages) | ✅ tested |
| Limit order book / matching engine (price-time, O(1) hot path) | ✅ tested |
| Venue glue: OUCH ↔ matching engine ↔ ITCH/OUCH events | ✅ tested |
| MoldUDP64 (market data) + SoupBinTCP (order entry) framing | ✅ tested |
| ExchangeSession: full SoupBin ⇄ OUCH ⇄ match ⇄ ITCH ⇄ MoldUDP64 loop | ✅ tested |
| Synthetic order-flow generator (deterministic) | ✅ tested |
| Kernel-socket transport + runnable `exchange_sim` binary (config 1) | ✅ tested + live smoke |
| **← exchange simulator runs over real TCP (order entry) + UDP (market data)** | |
| Manual Ethernet/IPv4/UDP framing (needed for ef_vi/DPDK paths) | ⏳ next — L3/L4 by hand |
| DUT transports: Onload (TCP) · ef_vi (UDP) · DPDK-sfc (UDP) | ⏳ hardware |
| HW-timestamped tick-to-trade harness + transport comparison | ⏳ hardware |

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

## Run the simulator (config 1: kernel sockets)

```bash
# exchange_sim [order_entry_tcp_port] [market_data_host] [market_data_udp_port]
./build/release/apps/exchange_sim 5001 127.0.0.1 5002
```

It waits for an order-entry client on the TCP port (SoupBinTCP), then publishes ITCH
market data over MoldUDP64/UDP and runs a synthetic market. Order entry is plain kernel
TCP with `TCP_NODELAY`; running the same binary under Onload accelerates both sockets
with no code change (that is the DUT's config-1 path).

## Design principles

Zero hot-path allocation, cache-line-aware layout, big-endian overlay structs decoded in
place, compile-time-sized messages, busy-poll on isolated cores. Conventions mirror the
sibling `abtrda3` transport benchmark repo.
