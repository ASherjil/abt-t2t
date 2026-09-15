#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# SPDX-FileCopyrightText: 2026 Areeb Sherjil
# plot_latency_hist.py — one latency histogram (frequency vs latency) from a
# HdrHistogram per-bucket CSV (value_us,count). Small stats box top-right.
# x-tick spacing AUTO-scales to the data range (~12 labels) unless --xmajor is given.
# Usage: plot_latency_hist.py <name>.hist.csv [--title "..."] [--xlabel "..."]
#                             [--out f.png] [--logy] [--bins N]
#                             [--xmajor N] [--xminor N]   # optional; auto if omitted
import argparse, math, numpy as np
import matplotlib; matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.ticker import MultipleLocator


def fmt_count(n):
    """Sample count -> compact label, rounded UP: 15.6M->16M, 47.3B->48B."""
    if n >= 1e9:
        return f"{math.ceil(n / 1e9)}B"
    if n >= 1e6:
        return f"{math.ceil(n / 1e6)}M"
    if n >= 1e3:
        return f"{math.ceil(n / 1e3)}K"
    return str(int(n))

ap = argparse.ArgumentParser()
ap.add_argument("file")
ap.add_argument("--title", default=None)
ap.add_argument("--xlabel", default="Tick-to-trade (us)")
ap.add_argument("--out", default=None)
ap.add_argument("--bins", type=int, default=220)
ap.add_argument("--xmajor", type=float, default=None)  # labeled tick spacing (us); auto from range if unset
ap.add_argument("--xminor", type=float, default=None)  # minor tick spacing (us); auto = xmajor/5 if unset
ap.add_argument("--logy", action="store_true")
a = ap.parse_args()

d = np.genfromtxt(a.file, delimiter=",", names=True)
v = np.atleast_1d(d["value_us"]).astype(float)
c = np.atleast_1d(d["count"]).astype(float)
o = np.argsort(v); v, c = v[o], c[o]
N = c.sum(); cum = np.cumsum(c)
def pct(p):
    return v[min(int(np.searchsorted(cum, p / 100.0 * N)), len(v) - 1)]
mn, mx = v[0], v[-1]
med, p99, p999, p9999, p99999 = pct(50), pct(99), pct(99.9), pct(99.99), pct(99.999)

# auto-pick a "nice" x-tick spacing (~12 major labels) from the data range unless
# overridden, so plots of any width (tight verbs vs wide RTT) come out readable with no flags.
def nice_tick(span, target=12):
    if span <= 0:
        return 1.0
    raw = span / target
    mag = 10.0 ** math.floor(math.log10(raw))
    for m in (1, 2, 2.5, 5, 10):
        if m * mag >= raw:
            return m * mag
    return 10 * mag

xmajor = a.xmajor if a.xmajor else nice_tick(mx - mn)
xminor = a.xminor if a.xminor else xmajor / 5.0

# rebin native HdrHistogram buckets into uniform display bins
edges = np.linspace(mn, mx, a.bins + 1)
idx = np.clip(np.searchsorted(edges, v, side="right") - 1, 0, a.bins - 1)
hist = np.zeros(a.bins); np.add.at(hist, idx, c)
centers = 0.5 * (edges[:-1] + edges[1:]); width = edges[1] - edges[0]

fig, ax = plt.subplots(figsize=(11, 6))
ax.bar(centers, hist, width=width, color="#2c7fb8", edgecolor="none")
if a.logy:
    ax.set_yscale("log")
ax.set_xlabel(a.xlabel, fontsize=12)
ax.set_ylabel(f"Frequency({fmt_count(N)} samples)" + (" — log" if a.logy else ""), fontsize=12)
ax.set_title(a.title or f"Latency histogram  (N = {int(N):,})", fontsize=13)
ax.set_xlim(mn, mx)
ax.xaxis.set_major_locator(MultipleLocator(xmajor))
ax.xaxis.set_minor_locator(MultipleLocator(xminor))
ax.tick_params(axis="x", labelsize=8)
ax.grid(axis="x", which="major", alpha=0.25)
ax.grid(axis="x", which="minor", alpha=0.10)

box = "\n".join([
    f"Min      {mn:7.3f}",
    f"Median   {med:7.3f}",
    f"P99      {p99:7.3f}",
    f"P99.9    {p999:7.3f}",
    f"P99.99   {p9999:7.3f}",
    f"P99.999  {p99999:7.3f}",
    f"Max      {mx:7.3f}",
])
ax.text(0.99, 0.975, box, transform=ax.transAxes, ha="right", va="top",
        fontsize=6.5, family="monospace",
        bbox=dict(boxstyle="round,pad=0.3", fc="white", ec="0.6", alpha=0.85))

out = a.out or (a.file[:-len(".hist.csv")] if a.file.endswith(".hist.csv") else a.file) + "_hist.png"
plt.tight_layout(); plt.savefig(out, dpi=150, bbox_inches="tight")
print("wrote", out)
print(f"N={int(N):,} min={mn:.3f} med={med:.3f} p99={p99:.3f} "
      f"p99.9={p999:.3f} p99.99={p9999:.3f} p99.999={p99999:.3f} max={mx:.3f}")
