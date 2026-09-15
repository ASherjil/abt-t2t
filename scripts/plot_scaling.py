#!/usr/bin/env python3
# plot_scaling.py - tick-to-trade percentiles against the number of quoted symbols, from the
# per-bucket CSVs that hlog_to_hist.py writes (value_us,count), one file per point.
# Usage: plot_scaling.py results/scaling/08.hist.csv results/scaling/16.hist.csv ... [--out f.png] [--title "..."]
import argparse, os, re
import numpy as np
import matplotlib; matplotlib.use("Agg")
import matplotlib.pyplot as plt

ap = argparse.ArgumentParser()
ap.add_argument("files", nargs="+")
ap.add_argument("--out", default="docs/images/t2t_scaling.png")
ap.add_argument("--title", default=None)
a = ap.parse_args()

PCTS = [("p50", 50.0), ("p99", 99.0), ("p99.9", 99.9), ("p99.99", 99.99), ("p99.999", 99.999), ("max", 100.0)]
PLOTTED = PCTS[:-1]

points = []
for path in a.files:
    m = re.search(r"(\d+)\.hist\.csv$", os.path.basename(path))
    n = int(m.group(1)) if m else len(points)
    d = np.genfromtxt(path, delimiter=",", names=True)
    v = np.atleast_1d(d["value_us"]).astype(float)
    c = np.atleast_1d(d["count"]).astype(float)
    o = np.argsort(v); v, c = v[o], c[o]
    cum = np.cumsum(c); total = cum[-1]
    def pct(p):
        if p >= 100.0:
            return v[-1]
        return v[min(int(np.searchsorted(cum, p / 100.0 * total)), len(v) - 1)]
    points.append((n, int(total), v[0], {name: pct(p) for name, p in PCTS}))
points.sort()

xs = [p[0] for p in points]
fig, ax = plt.subplots(figsize=(9, 5.5))
for name, _ in PLOTTED:
    ax.plot(xs, [p[3][name] for p in points], marker="o", linewidth=1.6, label=name)
ax.set_xscale("log", base=2)
ax.set_xticks(xs); ax.set_xticklabels([str(x) for x in xs])
ax.set_xlabel("Quoted Symbols", fontsize=12)
ax.set_ylabel("Tick-to-trade(us)", fontsize=12)
ax.set_title(a.title or "Tick-to-trade vs quoted symbols, 09:29:50 to 11:00:00, NASDAQ 2026-05-15, ef_vi", fontsize=12)
ax.grid(axis="y", alpha=0.3); ax.grid(axis="x", alpha=0.15)
ax.set_ylim(bottom=0)
ax.legend(ncol=5, fontsize=9, loc="lower right")
plt.tight_layout(); plt.savefig(a.out, dpi=150, bbox_inches="tight")
print("wrote", a.out)

print("| symbols | samples | min | p50 | p99 | p99.9 | p99.99 | p99.999 | max |")
print("|---|---|---|---|---|---|---|---|---|")
for n, total, mn, pc in points:
    row = [f"{int(round(x * 1000)):,}" for x in (mn, pc["p50"], pc["p99"], pc["p99.9"], pc["p99.99"], pc["p99.999"], pc["max"])]
    print(f"| {n} | {total:,} | " + " | ".join(row) + " |")
