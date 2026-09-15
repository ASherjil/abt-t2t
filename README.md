# Ultra Low Latency Order Book (abt-t2t)

A NASDAQ ITCH 5.0 feed handler that measures tick-to-trade using HW timestamping. The ITCH 5.0 data is replayed by the exchange
simulator that plays real data. Since GitHub is littered with "Order books", this one is different because it implements the full thing: a real exchange simulator,
linux kernel bypass(Solarflare `ef_vi`) and HW timestamped measurements all the way up to P99.999 and max. 

## Tick-to-trade, full trading day 

NASDAQ, 15 May 2026, the whole session from the first message at 03:02 to the 16:00 close,
replayed at wall-clock pace. Solarflare X2522-Plus, `ef_vi`, one order timed per quote update.

![Tick-to-trade histogram, full session](docs/images/t2t_full_day.png)

Every sample is the NIC's receive stamp of the market-data packet subtracted from the NIC's
transmit stamp of the order it caused, on one clock. 5,371,047 samples, nothing dropped. The
run was clean: 937.7 million market data packets, no gaps, no dropped or stale frames, zero
context switches on the hot core, zero CTPIO fallbacks, zero map rehashes, and the simulator
never fell more than 158 µs behind the tape. The single worst sample, 3,095 ns, is the
simulator's close-of-day packet; the worst sample the market produced was 2,600 ns.

## Tick-to-trade against the number of quoted symbols

Same tape, same binary, same 90 minutes over the open (09:29:50 to 11:00:00), quoting 8, 16,
32, 64 and 128 symbols. Every other symbol on the tape is booked in every run. The 8-symbol
point is cut from the full-day run above. Sixteen times the quoted set costs 10 ns at the
median and about 300 ns at p99.999.

![Tick-to-trade against quoted symbols](docs/images/t2t_scaling.png)

## What is measured ?

Feed handler:

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

Both timestamps are taken using a Solarflare X2522-Plus. The recieve timestamp is written when the ITCH market-data arrives on the 
wire. The transmit timestamp is written when the OUCH response leaves the NIC. 

### C++ code architecture

Ultra low latency is achieved by using kernel bypass combined with busy-polling. 

The sequence is as follows: 

1. Poll the receive ring, the CPU spinning at 100%.
2. Read the MoldUDP64 header and check the sequence against the tracker. Gaps are counted
   and surfaced, never silently absorbed.
3. Decode each ITCH 5.0 message in place, as a big-endian overlay on the received bytes.
   Nothing is copied into a parsed structure.
4. If the message belongs to a quoted symbol, apply it to that book here: add, execute,
   cancel, delete and replace are all constant-time on this path.
5. If it belongs to any other symbol, hand the frame to the second core and move on.
6. Ask the quoter whether the new top of book has moved its own quote out of position.
7. If it has, build the OUCH 5.0 order and write it into the card with CTPIO.
8. Only after the order is on the wire, push the latency sample onto a lock-free SPSC for
   the histogram thread.
9. The histogram thread is pinned on another CPU core where it stores the latency using HdrHistogram. 

**The strategy.** A resting two-sided quote on each quoted symbol, one level per side,
held near the touch. When the book moves, the order manager replaces the side that is now
mispriced, and it tracks the in-flight state of each side so a second order is never sent
against an unacknowledged one. The point of the strategy is not that it is profitable. It
is that it is a real decision made from a real book on every tick, so the measured path
includes a branch that has to be right.

**Hot and cold.** Eight symbols are quoted. Every other symbol on the tape, roughly twelve
and a half thousand of them, is still fully booked, on a second isolated core fed by a
lock-free ring of frame references. Those frames are read in place out of the receive
buffers, so nothing is copied to hand them over, and the ring is sized against the buffer
pool so a frame can never be recycled while the second core is still reading it. At the
busiest point of the session that side is carrying several million live orders.

### The quoted set

| symbol | what it is | top of book | resting orders |
|---|---|---|---|
| AAPL | Apple, mega-cap single stock | $297.25 | 37,603 |
| MSFT | Microsoft, deepest book of the eight | $415.14 | 86,364 |
| AMD | semiconductor, heavy message rate | $432.12 | 36,427 |
| INTC | low-priced semiconductor, heavy churn | $108.91 | 32,428 |
| QQQ | NASDAQ-100 ETF, NASDAQ-listed | $709.04 | 27,175 |
| SPY | S&P 500 ETF, NYSE Arca-listed | $739.95 | 768 |
| TQQQ | 3x leveraged NASDAQ-100 ETF | $75.36 | 26,909 |
| GOOGL | Alphabet, mega-cap single stock | $393.66 | 27,934 |

### Threads and CPU isolation

The application is multi-threaded with only one thread pinned to a specific isolated CPU core. The breakdown is as follows:

The CPU Intel Core i9-11900k contains 8 cores, this is how the threads were pinned. 

1. Core 3: Thread 1 Histogram thread for latency measurements. Thread 2 status thread for the exchange simulator status. These are not latency critical.
2. Core 4: Thread for the exchange simulator. 
3. Core 5: Thread for booking the cold symbols. 
4. Core 6: Thread for the feed handler. The most latency critical where tick-to-trade lives. 
5. Core 7: Thread for the socket implementation Solarflare Onload. (Not used for the kernel bypass version).

Core 0-2 are not isolated they are left for linux housekeeping. 

## How to run the exchange simulator

The simulator replays a real NASDAQ ITCH 5.0 day. It sends every message on the tape to the
DUT over MoldUDP64 on UDP, and takes the DUT's OUCH orders back. There are no command-line
arguments. Everything comes from `config/exchange_sim.toml`.

1. Get a day of NASDAQ TotalView-ITCH 5.0 data in BinaryFILE format, unzip it, and put it in
   `data/itch/`. The data is not in this repo, see `data/README.md`.
2. Build with `scripts/build.sh`. It asks three questions: clean or not, the build type
   (`release`, `release-hw` for hardware timestamps, `debug`), and which transports to build.
   Binaries land in `build/<type>/apps/`.
3. Edit `config/exchange_sim.toml`:
   - `[replay]`: `file` is the ITCH file. `skip_to` and `stop_at` pick the time-of-day window.
     `speed = 1.0` replays at real time. `loops = 1` plays the window once.
   - `[venue]`: `symbols` are the symbols the DUT is allowed to trade. Every symbol on the tape
     is still sent; these are the ones with a matching engine behind them.
   - Pick the transport. `[socket]` is for plain kernel sockets: market data goes to
     `md_host:md_port` over UDP and orders come in on TCP port `oe_port`. `[transport]`,
     `[network]`, `[market_data]` and `[order_entry]` are for the kernel-bypass binaries: the
     NIC, both MAC and IP addresses, and the UDP ports.
4. Run the binary for the transport you picked, from the repo root:

   ```bash
   build/release/apps/exchange_sim          # kernel sockets
   build/release/apps/exchange_sim_verbs    # ConnectX, libibverbs
   build/release/apps/exchange_sim_dpdk     # DPDK
   build/release/apps/exchange_sim_ef_vi    # Solarflare ef_vi
   ```

   It waits for the DUT to log in, then replays. It prints one status line per second and a
   summary at the end.

## How to run the DUT

The DUT is the feed handler and market maker. It receives ITCH over MoldUDP64, keeps the
books, quotes the configured symbols and sends OUCH orders. No command-line arguments.
Everything comes from `config/dut.toml`.

1. Build as above. Use `release-hw` for hardware-timestamped results. `release` adds software
   timers for development.
2. Edit `config/dut.toml`:
   - `[venue]`: `symbols` are the quoted symbols. The list must match the simulator's. Every
     other symbol on the tape is booked but not traded.
   - `[venue]`: `profile` points at a per-symbol profile that sizes the books before the session
     starts. Make it once with
     `build/release/apps/itch_replay data/itch/<file> --all --write-profile data/symbols.profile`.
     Leave it empty to size the books on the fly.
   - Pick the transport, the same way as the simulator. `[socket]` for kernel sockets:
     `md_bind_host:md_port` is where market data arrives, `oe_host:oe_port` is where orders go.
     `[transport]`, `[network]`, `[market_data]` and `[order_entry]` for kernel bypass.
   - `[measure]`: `log_file` is where the HdrHistogram log is written.
3. Start the simulator first, then run the DUT from the repo root:

   ```bash
   build/release-hw/apps/dut          # kernel sockets; the same binary under onload is the Onload row
   build/release-hw/apps/dut_verbs    # ConnectX, libibverbs
   build/release-hw/apps/dut_dpdk     # DPDK
   build/release-hw/apps/dut_ef_vi    # Solarflare ef_vi, the headline
   ```

   It prints one status line per second, a percentile line per minute, and the full report at
   the end: the latency table, per-symbol positions, and every counter a run is judged by.

To run both on one machine and collect the logs into `results/`, use
`sudo scripts/loopback_test.sh release-hw`. It reads both config files, launches both binaries,
and stops when the replay window ends.

## Methodology

[`docs/measurement-methodology.md`](docs/measurement-methodology.md) documents the full
measurement in detail: what is inside and outside the interval, the rig and its kernel
configuration, how the hardware timestamps are taken and matched, the feed handler's data
structures and their time complexity, the exchange simulator and how it decides fills by
queue position, the kernel-bypass transports, the NASDAQ session used, every thread, and
what is not claimed.

Kernel bypass transports come from the sibling project
[ABTRDA3](https://github.com/ASherjil/ABTRDA3), which benchmarks `ef_vi`, Verbs, DPDK and
AF_XDP on this same rig.

[`docs/bugs-found-by-benchmarking.md`](docs/bugs-found-by-benchmarking.md) lists every bug the
benchmarks exposed, how each one was found, what it did to the numbers, and how the fix was
verified.

## License

Apache-2.0. See [LICENSE](LICENSE) and [NOTICE](NOTICE).

NASDAQ TotalView-ITCH data is the property of Nasdaq, Inc. and is not included in this
repository.
