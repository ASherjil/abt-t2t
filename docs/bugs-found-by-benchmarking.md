# Bugs and issues found by benchmarking

Every entry here was found by a benchmark run, not by reading code. Most of them did not show
up in the latency table at all. They showed up in the counters printed next to it, which is
why every run prints them. The entries are in the order they were found. Each one says what
was seen, how it was traced, what the cause was, what changed, and how the fix was checked.

The recurring lesson: a latency histogram can look perfect over a system that has quietly
stopped trading. Read the order manager's rejects, unknown acks and positions, the feed's gap
counters, and the card's own drop counters, on every run.

---

## 1. Transmit ring smaller than the opening burst

**Seen.** First `ef_vi` runs: four of the eight quoted symbols never quoted. The simulator
received all sixteen opening orders but the DUT's per-symbol report showed their slots stuck
in pending-new for the whole run, and orders per 150 s fell from 57k to 27k.

**Traced.** The per-symbol slot state in the end-of-run report, and the `quote-skips pending`
counter at 334k instead of the usual ~1k per minute.

**Cause.** The open sends sixteen orders back to back, two per quoted symbol. The transmit
ring had eight slots. The ninth send found no slot and was silently dropped, and the order
manager had already marked that side pending, so it waited for an ack that never came.

**Fix.** Sixty-four transmit slots, later 256. Rule: the ring must exceed the largest burst
the strategy can send from one packet.

**Checked.** Every slot state 2 (live) at the end of a run, pending skips back to ~1k per
minute.

## 2. `sudo` drops the CTPIO mode

**Seen.** 465 CTPIO fallbacks out of 44k sends. The card was falling back to DMA.

**Cause.** The DUT reads `EF_VI_CTPIO_MODE` from its environment. The run script re-executes
itself under `sudo`, which resets the environment, so the DUT ran the library's default paced
writer.

**Fix.** The script exports the variable after the re-exec and records it in `versions.txt`;
the DUT's ready line prints the mode it actually got. The same trap later applied to
`EF_VI_RXQ_SIZE` (entry 9).

**Checked.** `ctpio_fallbacks=0` on every published run.

## 3. Cut-through threshold below the frame size

**Seen.** Poisoned frames on the wire with cut-through enabled.

**Cause.** The cut-through threshold was 64 bytes. Order frames are 61 to 89 bytes. A frame
longer than the threshold starts transmitting before it has been fully written and underruns.

**Fix.** Threshold 128, so every order frame is store-and-forward inside the card.

## 4. Hardware-timestamp prefix pushed the sequence number past the guaranteed cache line

**Seen.** The first hardware-timestamp build reported feed gaps (278 gaps, 1,737 missed, 278
stale) while both the simulator's transmit counter and the card's receive counters showed
nothing lost. The software-timing build of the same morning showed zero.

**Traced.** Nothing lost anywhere plus forward jumps plus matching stale counts means the DUT
read the sequence number before it had arrived.

**Cause.** In payload-poll mode the receive path returns a frame the moment the first 64-byte
cache line of its buffer is written by DMA, and guarantees only that line. The MoldUDP64
sequence sits at frame offset 52. With hardware timestamps the card prepends a 14-byte prefix,
which moves the sequence to buffer offset 66, in the second cache line, which is not
guaranteed present. Under the opening burst the DUT caught buffers with line 0 written and
line 1 still holding the previous packet.

**Fix.** A guard word: the MoldUDP64 message count, which sits in line 1 and is never zero for
a data packet. The DUT zeroes it before returning a buffer to the card and spins on it, or on
the card's completion event, before decoding. Payload-poll with the guard was measured about
72 ns faster at the median than waiting for the completion event.

**Checked.** Gaps, missed and stale all zero over every run since, including the full day.

## 5. The simulator's status line blocked in the filesystem journal

**Seen.** In roughly one run in three the simulator fell 4 to 7 ms behind the tape, always in
a second that was a multiple of five from the start of the run, and only on the first run
after the machine had been idle.

**Traced.** ftrace on the simulator's core with a stack trace on every context switch. The
thread left the CPU inside `write()`: `ext4_dirty_inode` → `jbd2_journal_get_write_access` →
`wait_on_bit` → `io_schedule`. The once-a-second status line was written from the paced
thread, and the file's inode block was being committed to the journal by the 5-second commit
at that moment. The commit is phase-locked to the writes. The wait only took milliseconds when
the NVMe had been idle long enough to enter a low-power state, which is why back-to-back runs
never showed it. Not memory pressure, not page faults, not interrupts, not the DUT.

**Fix.** The paced thread snapshots its counters into a lock-free ring once a second. A
status thread on a housekeeping core (`log_core`) formats and writes them. The paced thread
makes no system call between the first message and the last.

**Checked.** Lateness in the tens to hundreds of microseconds on every run since, including a
full day at 158 µs, and none over a millisecond.

## 6. Live-order map resized mid-session

**Seen.** A 1.4 ms simulator stall at 09:31:23 on every run.

**Cause.** The simulator's live-order map was sized for 65,536 orders. MSFT peaks at 131k
live orders on this day, so the map rehashed during the open.

**Fix.** `order_reserve = 524288`, sized from the day's peak, and a rehash counter on the
status line so it can never happen silently again.

**Checked.** `rehash=0` on every run.

## 7. Pre-faulting the 30 GB replay file stalled the simulator

**Seen.** 6 to 7 ms stalls at the same replay time on every run, with the file mapped with
`MAP_POPULATE`.

**Cause.** With 45 GB usable and a 30 GB mapping, the kernel reclaimed mapped pages under the
simulator while it ran.

**Fix.** The populate option was removed. The file is read once before a run so the page
cache holds it, and residency is checked with `mincore` before the headline runs.

## 8. Socket runs on one machine took the kernel loopback

**Seen.** The first Onload comparison run received five million packets with no acceleration
and no hardware stamps.

**Cause.** Both endpoints' addresses were local to the same kernel, so the traffic never
touched the cable.

**Fix.** The run script moves the simulator's port into its own network namespace for socket
runs. A second issue surfaced immediately after: the default `rmem_max` clamped the receive
buffer to about 209 packets and 445 packets were lost at the stock-directory burst. The
script raises `rmem_max` and `wmem_max` to 64 MB.

**Checked.** `onload_stackdump`: receive-side overflow drops zero, packets received in user
space, interrupts in single digits.

## 9. Receive ring: too shallow to survive a stall, then too deep for the card

**Seen, part one.** At 32 quoted symbols, 21 minutes into a 90-minute run, the feed reported
two gaps and 306 missed packets, the book went invalid, and quoting stopped for the remaining
70 minutes. The card's own receive counters in the DUT report showed zero.

**Traced.** `ethtool -S` on the card: `port_rx_nodesc_drops` had risen by 354. The card had
frames to deliver and the application had no free buffer posted. The DUT posted 256 receive
buffers, about 5 ms of feed at that minute's rate, and the hot thread had paused for longer
than that. A hardware-latency detector pinned to an idle core ran through the next four
runs and recorded no platform stall; the original pause was never reproduced and its cause is
not known. The deeper ring makes a pause of that length harmless either way.

**Seen, part two.** Raising the posted buffers to 2,048 made the DUT receive exactly 2,046
packets in 89 minutes. The card dropped essentially the whole feed.

**Cause.** The `ef_vi` library allocates its receive descriptor ring at a default of 512 slots
regardless of how many buffers the application owns. Payload-poll reconciles the card's
events lazily, so the ring must have slack beyond the posted buffers. 256 buffers in a
512-slot ring worked by accident. 2,048 in a 2,048-slot ring starved.

**Fix.** Rule: the ring is twice the posted buffers. `EF_VI_RXQ_SIZE=4096` is exported by
the run script, 2,048 buffers are posted, and the script prints the card's no-descriptor drop
counter delta at the end of every run so a lost packet can never again go unreported.

**Checked.** Zero no-descriptor drops on every run since, including 937.7 million packets
over the full day. Cost, measured by A/B on the same five-minute window: about 3 ns at the
median and 30 ns at p99, split evenly between the deeper receive and transmit rings.

## 10. The order manager dropped fill acks on old quotes

**Seen.** Rejected replaces rose through every run: 4.7% of orders on the full day, 23% at 16
symbols, 45% at 32. On the full day MSFT closed 10,866,576 shares long while every other
symbol was within a few thousand of flat.

**Traced.** The reject rate rising with time and with symbol count, `unknown` ack counts of
11, 62 and 197, and one symbol's position running away with no sells.

**Cause.** The order manager maps a user reference back to its quote slot through a
4,096-entry hint ring. Three of the four ack handlers fall back to scanning the slots when
the hint misses. The fill handler did not; it counted the ack as unknown and returned. A fill
on a quote that had rested longer than 4,096 allocations was dropped. The venue had removed
the order; the DUT still marked the slot live, so every later replace was rejected and
nothing could ever settle the slot. MSFT's ask died early in the day and its bid kept getting
filled.

**Fix.** The fill handler uses the same lookup as the other handlers: ring hit, else slot
scan.

**Why the latency numbers were unaffected.** A dead slot sends a replace through exactly the
same path as a live one, and the sample is taken before the venue answers. The distributions
were right; the trading was not.

## 11. The simulator routed replaces to the wrong venue

**Seen.** After fix 10, rejects at 64 symbols were a flat 35% from the first second, 20
rejects per fill, with no dependence on fills.

**Cause.** An OUCH replace carries no symbol. The simulator finds the venue holding the
original order through a 4,096-entry hint ring, and on a miss returned venue 0 instead of
scanning. Venue 0 had never seen the reference and answered "replace not allowed". The DUT's
slot went back to live, the next book move sent another replace for it, and the cycle
repeated until the real venue happened to fill the order.

**Fix.** Ring hit, else ask each venue whether it holds the reference (a constant-time map
lookup per venue, at most two live orders each), else reject. A `route=scans/misses` counter
on the simulator's status line.

**Checked.** Rejects fell to 0.07% to 0.16% of replaces, highest at the open and falling
through the run, with `misses` in the tens: the genuine case of an order filled in the
microseconds before its replace arrived, which a real venue also rejects.

## 12. The DUT decoded order-entry acks before the bytes had landed

**Seen.** After fixes 10 and 11, one slot in 128 still wedged per five-minute run: state
pending-replace, a pending reference from mid-run, never answered. Four acks per run counted
as unknown.

**Traced.** A diagnostic that records the first sixteen unknown acks with their raw bytes and
the acks that preceded them. The recorded bytes did not match the values the handler had
parsed moments earlier: a reference of 822,083,584 that was never allocated, a Replaced for
PANW decoded with the wrong reference.

**Cause.** The same mechanism as entry 4, on the other socket. The market-data path had a
guard word; the order-entry path did not. With the 14-byte timestamp prefix the OUCH user
reference sits in the second cache line, so a few acks per run were decoded from the buffer's
previous contents.

**Fix.** A guard on the ack path: the four-byte reference word, zeroed when the buffer is
returned to the card, spun on before an ack is decoded.

**Checked.** Zero unknown acks and zero wedged slots on every run since: five-minute smoke
runs at 64 and 128 symbols, 90-minute runs at 16, 32, 64 and 128, and the full day.

---

## Artifacts that are not bugs

**The close-of-window packet.** When the simulator reaches `stop_at` it resets its venues and
emits one packet of 15 to 18 messages. The DUT decodes all of them before its first order
leaves, and that packet is the worst sample of every windowed run. The published full-day
figures include it (max 3,095 ns; the worst market-driven sample was 2,600 ns). The scaling
chart plots percentiles only, since one packet per run says nothing about symbol count.

**Rejected replaces at 0.1%.** After fixes 10 to 12 the remaining rejects are orders filled
in the instant before the replace for them reached the venue. The simulator's routing counter
confirms they were found at the right venue and were simply gone. A real exchange rejects
these too.

**Out-of-band adds on high-priced names.** The simulator's venues cover prices up to $2,000.
NASDAQ carries far-from-touch limit orders on names like ASML and MELI above that, which the
simulator counts as out of band and drops. They never reach the touch.

---

## What every run prints now, because of the above

- Feed: gaps, missed, stale, book validity.
- Card: receive discards and drops from the driver, and the no-descriptor drop counter delta
  from the card itself.
- Hot core: context switches, page faults, interrupts during the run.
- Transmit: CTPIO wins and fallbacks, transmit stamp overflow.
- Order manager: enters, replaces, accepts, fills, rejects by reason, unknown acks with their
  bytes, pending skips, and every quoted symbol's position and slot state.
- Books: rehashes and re-anchors, hot and cold, and the memory each side uses.
- Simulator: maximum lateness and the count over a millisecond, rehashes, routing scans and
  misses, and the fill breakdown by mechanism.
- Environment: commit, build preset, transport, CTPIO mode, ring size, replay window.
