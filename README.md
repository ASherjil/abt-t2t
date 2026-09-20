# Ultra Low Latency Order Book (abt-t2t)

A NASDAQ ITCH 5.0 feed handler that measures tick-to-trade using HW timestamping. The ITCH 5.0 data is replayed by the exchange
simulator that plays real data. Since GitHub is littered with "Order books", this one is different because it implements the full thing: a real exchange simulator,
linux kernel bypass(Solarflare `ef_vi`) and HW timestamped measurements all the way up to P99.999 and max. 

## Tick-to-trade, full trading day

NASDAQ, 15 May 2026, the whole session from the first message at 03:02 to the 16:00 close,
replayed at wall-clock pace, quoting eight symbols. Every sample is the NIC's receive stamp of
the market-data packet subtracted from the NIC's transmit stamp of the order it caused, on one
clock. Same tape, same rig, same DUT logic on both rows; only the transport differs.

### Solarflare X2522-Plus, `ef_vi`

![Tick-to-trade histogram, full session, ef_vi](docs/images/t2t_full_day_efvi.png)

5,370,758 samples, nothing dropped. 937.6 million market data packets, no gaps, no dropped or
stale frames, zero context switches on the hot core, zero CTPIO fallbacks, zero map rehashes,
and the simulator never fell more than 158 µs behind the tape. The single worst sample,
3,197 ns, is the simulator's close-of-day packet; the worst sample the market produced was
3,004 ns.

### Solarflare X2522-Plus, Onload

![Tick-to-trade histogram, full session, Onload](docs/images/t2t_full_day_onload.png)

The same DUT built against plain BSD sockets and run under Onload with the low-latency
profile: spinning, interrupt-free, one stack per thread, CTPIO sends, hardware stamps through
`SO_TIMESTAMPING`. 5,117,062 samples, 854.6 million packets, no gaps, no drops in the Onload
stack or the kernel, every order matched to a transmit stamp. Fewer samples than the `ef_vi`
row because the longer acknowledgement path leaves more quote updates skipped while a replace
is still in flight.

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

Photos of the rig: [the two cards cabled back to back with the 25G DACs](docs/images/Server_backside.jpg),
[the X2522-25G Plus](docs/images/Solarflare_X2522_Plus.jpg) and [both cards seated in the
board, each under its own fan](docs/images/Upside_motherboard_shot.jpg).

## The latency critical tick-to-trade path 

Everything between the two hardware stamps runs on one thread, on one isolated core, with no
syscall, no lock, no allocation and no branch to code that is not on this path. This is the
`ef_vi` receive-to-send loop from `src/t2t/dut/DutSession.hpp`, trimmed of the
software-timing counters.

```cpp
void DutSession<Mode, Strat, Io>::poll()
    requires (Mode == IoMode::Transport && RxRing<Io> && TxRing<Io>)
{
    for (;;) {
        const auto raw = m_io.io->tryReceive();          // 1. next frame in the rx ring, or empty
        if (raw.empty()) {
            drainTxStamps();                             //    idle: collect tx stamps, keep spinning
            break;
        }
        const auto*       frame = raw.data();
        const auto*       p     = reinterpret_cast<const std::byte*>(raw.data());
        const std::size_t len   = net::udpPayloadLen(p, raw.size());
        prefetchFrame(frame, net::kL2L3L4Overhead + len);
        const std::uint64_t rxStamp = m_io.io->hwRxTimestamp();   // 2. NIC rx stamp from the 14-byte prefix
        const std::span<const std::byte> payload{p + net::kL2L3L4Overhead, len};
        if (udpDstPort(frame) == m_io.ackPort) {
            applyAck(payload);                           //    order-entry ack: update the slot state
        } else {
            if (raw.size() >= kGuardedFrame) {           // 3. payload-poll: the frame is read while the
                const volatile std::uint16_t* guard =    //    DMA is still landing, so spin on a word in
                    reinterpret_cast<const volatile std::uint16_t*>(frame + kGuardOffset);
                while (*guard == 0 && !m_io.io->rxFrameComplete()) {}   //    the second cache line
            }
            applyPacket(payload, rxStamp);               // 4. MoldUDP64 header, ITCH decode, book, quote
        }
        std::memset(const_cast<std::uint8_t*>(frame) + kGuardOffset, 0, sizeof(std::uint16_t));
        m_io.io->release();                              //    buffer back to the ring
    }
}
```

Inside `applyPacket` each ITCH message is applied to its book in place. When a quoted symbol's
top of book changes, the quote decision and the send happen before the next message is read:

```cpp
const auto quote = [&](std::size_t h) {
    const BookBuilder& book = m_books.hotBook(h);
    QuoteTargets       targets{};
    if (!m_strats[h].onBook(book, m_oms.account(h), targets)) {   // 5. strategy: is our quote mispriced?
        return;                                                    //    no: nothing to send
    }
    const std::size_t n = m_oms.reconcile(h, targets, m_out);     // 6. OMS: which side to replace, build OUCH
    for (std::size_t i = 0; i < n; ++i) {
        sendOrder({m_out[i].buf.data(), m_out[i].len});           // 7. straight into the NIC
    }
};
```

```cpp
bool DutSession<Mode, Strat, Io>::sendOrder(std::span<const std::byte> ouch) {
    const auto    frameLen = static_cast<std::uint32_t>(net::kL2L3L4Overhead + ouch.size());
    std::uint8_t* buf      = m_io.io->acquire(frameLen);           // 8. next CTPIO tx slot
    std::memcpy(buf, m_io.oeHeaders[headerKind(ouch.size())].data(), net::kL2L3L4Overhead);
    std::memcpy(buf + net::kL2L3L4Overhead, ouch.data(), ouch.size());   //    prebuilt Ethernet/IP/UDP header
    m_txSeq                           = m_io.io->txSequence();
    m_txRefs[m_txSeq & (kTxRefs - 1)] = TxRef{.seq = m_txSeq, .userRef = kNoRef};   // 9. remember which order
    m_io.io->commit();                                             // 10. 64-byte posted writes to the card
    return true;                                                   //     tx stamp arrives later, matched by seq
}
```

The functions for Rx(`tryReceive()`, `release()`) and Tx(`acquire()`, `commit()`) come from [ABTRDA3](https://github.com/ASherjil/ABTRDA3), 
which benchmarks `ef_vi`, Verbs, DPDK and AF_XDP on this same rig.

## How to run the exchange simulator

No command-line arguments. Everything is in `config/exchange_sim.toml`. Put a NASDAQ
TotalView-ITCH 5.0 day in `data/itch/` (see `data/README.md`), build with
`scripts/build.sh`, then set these:

```toml
[venue]
symbols  = ["AAPL", "MSFT", "AMD", "INTC", "QQQ", "SPY", "TQQQ", "GOOGL"]   # symbols with a matching engine behind them; every symbol on the tape is still sent

[replay]
file     = "data/itch/itch50_05_15"   # the unpacked ITCH day
speed    = 1.0                        # real time-of-day pacing; 0 = as fast as possible
loops    = 1                          # play the window once; 0 = repeat until stopped
skip_to  = "09:29:50"                 # fast-forward to here (books are built), then pace; "" = from the first message
stop_at  = "11:00:00"                 # stop here; "" = end of file
wait_for_dut = true                   # hold the replay until the DUT logs in

[transport]
interface = "cx0"                     # the NIC the simulator drives
cpu_core  = 4                         # isolated core for the replay thread
log_core  = 3                         # housekeeping core for the status line

[network]                             # no ARP, both ends hardcoded
local_mac = "b8:83:03:9c:d4:5c"
local_ip  = "10.0.0.1"
peer_mac  = "00:0f:53:ad:3c:91"       # the DUT's NIC
peer_ip   = "10.0.0.2"
```

Run the binary for the transport, from the repo root:

```bash
build/release/apps/exchange_sim_verbs    # ConnectX, libibverbs (the headline runs)
build/release/apps/exchange_sim_ef_vi    # Solarflare ef_vi
build/release/apps/exchange_sim_dpdk     # DPDK
build/release/apps/exchange_sim          # kernel sockets, uses [socket] instead of [network]
```

It waits for the DUT, replays, prints one status line per second and a summary at the end.

**Standalone ITCH replayer.** Set `wait_for_dut = false` and start a kernel-bypass binary. It
puts every message on the wire as MoldUDP64 to `peer_mac`/`peer_ip` with nothing on the other
end, at real time or as fast as it can (`speed = 0`, about a million messages a second). No
retransmission server: a receiver that drops a packet handles the gap itself.

## How to run the DUT

No command-line arguments. Everything is in `config/dut.toml`. Build with `release-hw` for
hardware-timestamped results, then set these:

```toml
[venue]
symbols  = ["AAPL", "MSFT", "AMD", "INTC", "QQQ", "SPY", "TQQQ", "GOOGL"]   # quoted symbols, must match the simulator's list
profile  = "data/symbols.profile"     # per-symbol book sizing from a previous session; "" = size on the fly
cold_core = 5                         # isolated core that books every other symbol on the tape

[quoter]
half_spread_ticks   = 1               # ticks from fair value to each quote
quote_qty           = 100             # shares per side
skew_ticks_per_unit = 0.001           # quote shift per share of inventory

[measure]
histogram_core = 3                    # core of the HdrHistogram thread
log_file       = "results/dut.hlog"   # HdrHistogram interval log

[transport]
interface = "enp1s0f1"                # X2522-Plus port 1
cpu_core  = 6                         # isolated core for the tick-to-trade thread
order_core = 7                        # socket/Onload build only: the TCP order thread

[network]
local_mac = "00:0f:53:ad:3c:91"
local_ip  = "10.0.0.2"
peer_mac  = "b8:83:03:9c:d4:5c"       # the simulator's NIC
peer_ip   = "10.0.0.1"
```

The profile is made once with
`build/release/apps/itch_replay data/itch/<file> --all --write-profile data/symbols.profile`.
Start the simulator first, then the DUT from the repo root:

```bash
build/release-hw/apps/dut_ef_vi    # Solarflare ef_vi, the headline
build/release-hw/apps/dut_verbs    # ConnectX, libibverbs
build/release-hw/apps/dut_dpdk     # DPDK
build/release-hw/apps/dut          # kernel sockets; the same binary under onload is the Onload row
```

It prints one status line per second, a percentile line per minute, and the full report at
the end: the latency table, per-symbol positions, and every counter a run is judged by.

To run both on one machine and collect the logs into `results/`:

```bash
sudo scripts/loopback_test.sh release-hw
```

It reads both config files, picks the binaries from the `backend` key in each `[transport]`
section, launches both, and stops when the replay window ends.

## Methodology

[`docs/measurement-methodology.md`](docs/measurement-methodology.md) documents the full
measurement in detail: what is inside and outside the interval, the rig and its kernel
configuration, how the hardware timestamps are taken and matched, the feed handler's data
structures and their time complexity, the exchange simulator and how it decides fills by
queue position, the kernel-bypass transports, the NASDAQ session used, every thread, and
what is not claimed.

[`docs/bugs-found-by-benchmarking.md`](docs/bugs-found-by-benchmarking.md) lists every bug the
benchmarks exposed, how each one was found, what it did to the numbers, and how the fix was
verified.

## License

Apache-2.0. See [LICENSE](LICENSE) and [NOTICE](NOTICE).

NASDAQ TotalView-ITCH data is the property of Nasdaq, Inc. and is not included in this
repository.
