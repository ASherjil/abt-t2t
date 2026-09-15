#!/usr/bin/env python3
# hlog_to_hist.py - merge the intervals of one recorder tag from an abt-t2t HdrHistogram log
# into a per-bucket CSV (value_us,count) that plot_latency_hist.py understands.
# Usage: hlog_to_hist.py <dut.hlog> [--tag t2t_hw] [--out name.hist.csv]
import argparse
from hdrh.histogram import HdrHistogram

ap = argparse.ArgumentParser()
ap.add_argument("hlog")
ap.add_argument("--tag", default="t2t_hw")
ap.add_argument("--out", default=None)
ap.add_argument("--from-epoch", type=float, default=None)
ap.add_argument("--to-epoch", type=float, default=None)
ap.add_argument("--drop-last", type=int, default=0)
a = ap.parse_args()

total = None
intervals = 0
lines = [l for l in open(a.hlog) if l.startswith(f"Tag={a.tag},")]
if a.drop_last:
    lines = lines[:-a.drop_last]
if True:
    for line in lines:
        if not line.startswith(f"Tag={a.tag},"):
            continue
        fields = line.rstrip("\n").split(",", 4)
        start = float(fields[1]); end = start + float(fields[2])
        if a.from_epoch is not None and end <= a.from_epoch:
            continue
        if a.to_epoch is not None and start >= a.to_epoch:
            continue
        h = HdrHistogram.decode(fields[4])
        if total is None:
            total = h
        else:
            total.add(h)
        intervals += 1
if total is None:
    raise SystemExit(f"no intervals tagged {a.tag} in {a.hlog}")

out = a.out or a.hlog.rsplit("/", 1)[0] + f"/{a.tag}.hist.csv"
n = 0
with open(out, "w") as f:
    f.write("value_us,count\n")
    for item in total.get_recorded_iterator():
        c = item.count_added_in_this_iter_step
        f.write(f"{item.value_iterated_to / 1000.0:.3f},{c}\n")
        n += c
print(f"wrote {out}: {intervals} intervals, {n:,} samples, "
      f"min={total.get_min_value()/1000:.3f} p50={total.get_value_at_percentile(50)/1000:.3f} "
      f"p99={total.get_value_at_percentile(99)/1000:.3f} p99.9={total.get_value_at_percentile(99.9)/1000:.3f} "
      f"p99.99={total.get_value_at_percentile(99.99)/1000:.3f} p99.999={total.get_value_at_percentile(99.999)/1000:.3f} "
      f"max={total.get_max_value()/1000:.3f} us")
