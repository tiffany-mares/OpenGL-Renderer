#!/usr/bin/env python3
"""Phase 6d README figures from a run_plots.py raw directory.

Figure 1 (frametime-hist-144.png): overlaid step histograms of frame
START-TO-START intervals at 144 Hz for sleep / timer / timer_spin, log count
axis. Start-to-start, deliberately NOT the CSV's frame_time_ns:
deadline-to-deadline re-pins to the period after every miss-resync, which
would fake a spike for every strategy. Start-to-start is the cadence a viewer
actually experiences.

Figure 2 (drift-60s.png): drift from the ideal 144 Hz grid over 60 seconds,
absolute (next += period) vs relative (next = now + period) rescheduling,
both under the plain high-res timer strategy.
drift_i = frame_start[i] - (frame_start[0] + i * period), post-warmup rows.

This is the only script in bench/ that imports matplotlib (Agg backend; it
never runs in CI). It prints one stats line per series -- paste those into
the results note so the committed binary PNGs have text provenance a
reviewer can regenerate and diff.
"""
import argparse
import csv
import re
import sys
from pathlib import Path

import matplotlib
matplotlib.use("Agg")  # before pyplot: headless, no display needed
import matplotlib.pyplot as plt

# Same integer division as FramePacer's period: 10^9 // 144 = 6_944_444 ns.
# Using float 1e9/144 here would fabricate ~0.44 ns/frame of fake drift.
PERIOD_NS = 1_000_000_000 // 144
BENCH_RE = re.compile(r"bench: frames=(\d+) warmup=(\d+)")

# Categorical palette (fixed slot order, validated: worst adjacent CVD
# dE 47.2, all slots in-band) + chart chrome. Text wears ink, never a
# series color.
BLUE, AQUA, YELLOW = "#2a78d6", "#1baf7a", "#eda100"
INK, MUTED, GRID, BASELINE = "#0b0b0b", "#898781", "#e1e0d9", "#c3c2b7"

HIST_XMAX_MS = 18.0   # fixed: covers max observed 17.5 ms; outliers annotated
HIST_BIN_MS = 0.05    # resolves the 1 us-wide timer_spin spike as one bin


def load_starts(raw: Path, name: str) -> list:
    """Post-warmup frame_start_ns column; warmup parsed from the bench: line."""
    txt = (raw / f"{name}.txt").read_text()
    m = BENCH_RE.search(txt)
    if not m:
        sys.exit(f"FATAL: no bench: line in {name}.txt -- was this a bench run?")
    warmup = int(m.group(2))
    with open(raw / f"{name}.csv", newline="") as f:
        rows = list(csv.DictReader(f))
    starts = [int(r["frame_start_ns"]) for r in rows[warmup:]]
    if len(starts) < 2:
        sys.exit(f"FATAL: {name}.csv has <2 post-warmup rows -- truncated log?")
    return starts


def percentile(sorted_vals, q):
    """Nearest-rank on a pre-sorted list (matches bench/summarize.py)."""
    return sorted_vals[round(q * (len(sorted_vals) - 1))]


def style_axes(ax) -> None:
    ax.set_axisbelow(True)
    ax.grid(True, color=GRID, linewidth=0.8)
    for side in ("top", "right"):
        ax.spines[side].set_visible(False)
    for side in ("left", "bottom"):
        ax.spines[side].set_color(BASELINE)
    ax.tick_params(colors=MUTED, labelsize=9)
    ax.xaxis.label.set_color(MUTED)
    ax.yaxis.label.set_color(MUTED)


def save(fig, path: Path, raw: Path, footer: str) -> None:
    if footer:
        fig.text(0.995, 0.005, footer, ha="right", va="bottom",
                 fontsize=6.5, color=MUTED)
    fig.tight_layout()
    fig.savefig(path, metadata={
        "Software": "bench/plot_frames.py",
        "Comment": f"raw data: {raw}" + (f"; {footer}" if footer else "")})
    plt.close(fig)
    print(f"wrote {path}")


def fig_histogram(raw: Path, out_path: Path, footer: str) -> None:
    # Draw order is back-to-front; legend order is re-sorted hero-first below.
    series = [
        ("hist-sleep-144", "sleep_for (naive)", "sleep_for", YELLOW),
        ("hist-timer-144", "high-res timer", "timer", AQUA),
        ("hist-timer_spin-144", "timer + spin (shipping default)", "timer_spin", BLUE),
    ]
    period_ms = PERIOD_NS / 1e6
    nbins = int(round(HIST_XMAX_MS / HIST_BIN_MS))
    bins = [i * HIST_BIN_MS for i in range(nbins + 1)]

    fig, ax = plt.subplots(figsize=(7.5, 4.3), dpi=160)
    n = 0
    overflow_notes = []
    direct_labels = []  # (short_name, anchor_x_ms, height_frac)
    for name, label, short, color in series:
        starts = load_starts(raw, name)
        deltas = sorted((b - a) / 1e6 for a, b in zip(starts, starts[1:]))
        n = len(deltas)
        p50, p99 = percentile(deltas, 0.50), percentile(deltas, 0.99)
        overflow = sum(1 for d in deltas if d > HIST_XMAX_MS)
        print(f"hist {name}: n={n} p50={p50:.3f}ms p99={p99:.3f}ms "
              f"max={deltas[-1]:.3f}ms over_{HIST_XMAX_MS:g}ms={overflow}")
        if overflow:
            overflow_notes.append(
                f"{label}: {overflow} frame(s) beyond {HIST_XMAX_MS:g} ms not shown")
        ax.hist(deltas, bins=bins, histtype="stepfilled",
                color=color, alpha=0.30, edgecolor="none")
        ax.hist(deltas, bins=bins, histtype="step", linewidth=1.6, color=color,
                label=f"{label} — p50 {p50:.3f} ms, p99 {p99:.3f} ms")
        # Anchor each series' in-ink identity label where its mass lives:
        # the spike at its p50, the smeared/bimodal series at their p99s.
        if short == "timer_spin":
            direct_labels.append((short, p50, 0.55))
        elif short == "timer":
            direct_labels.append((short, min(p99 + 0.3, HIST_XMAX_MS - 0.5), 0.68))
        else:  # sleep_for: label rides the tick-wake cluster
            direct_labels.append((short, min(p99, HIST_XMAX_MS - 1.0), 0.80))

    ax.set_yscale("log")
    ax.set_ylim(bottom=0.8)
    ax.set_xlim(0, HIST_XMAX_MS)
    ax.axvline(period_ms, color=MUTED, linewidth=1.2, linestyle="--", zorder=1)
    ax.text(period_ms, 0.76, " 6.944 ms period", transform=ax.get_xaxis_transform(),
            ha="left", va="top", fontsize=8, color=MUTED)
    # Direct identity labels in ink: identity must not ride on color alone,
    # and timer/timer_spin overlap spatially near the period line.
    for short, x_ms, frac in direct_labels:
        ax.text(x_ms, frac, short, transform=ax.get_xaxis_transform(),
                ha="center", va="bottom", fontsize=8, color=INK)
    if overflow_notes:
        # Figure corner, mirroring the provenance footer at bottom-right:
        # every in-axes spot collides with something (bottom-left drowns in
        # sleep_for's sub-2 ms cluster, top-left hits the legend and spike).
        fig.text(0.005, 0.005, "\n".join(overflow_notes),
                 ha="left", va="bottom", fontsize=7, color=MUTED)

    ax.set_xlabel("frame start-to-start interval (ms)")
    ax.set_ylabel("frames (log scale)")
    ax.set_title(f"Frame-time distribution at 144 Hz — {n:,} frames per "
                 f"strategy, 500-frame warmup dropped",
                 fontsize=11, color=INK)
    handles, labels = ax.get_legend_handles_labels()
    order = [2, 1, 0]  # hero (timer_spin) first in the legend
    ax.legend([handles[i] for i in order], [labels[i] for i in order],
              loc="upper right", frameon=False, fontsize=8.5)
    style_axes(ax)
    save(fig, out_path, raw, footer)


def fig_drift(raw: Path, out_path: Path, footer: str) -> None:
    series = [
        ("drift-absolute-144", "absolute: next += period (shipping)", BLUE, (-8, 10)),
        ("drift-relative-144", "relative: next = now + period", AQUA, (-8, -14)),
    ]
    fig, ax = plt.subplots(figsize=(7.5, 4.0), dpi=160)
    ax.axhline(0, color=BASELINE, linewidth=1.0, zorder=1)
    for name, label, color, offset in series:
        starts = load_starts(raw, name)
        t0 = starts[0]
        xs = [(s - t0) / 1e9 for s in starts]
        drift = [(s - (t0 + i * PERIOD_NS)) / 1e6 for i, s in enumerate(starts)]
        nframes = len(starts) - 1
        rate = drift[-1] / nframes if nframes else 0.0
        print(f"drift {name}: frames={nframes} final={drift[-1]:+.3f}ms "
              f"rate={rate:+.4f}ms/frame wall={xs[-1]:.1f}s")
        ax.plot(xs, drift, color=color, linewidth=2.0, label=label, zorder=3)
        ax.annotate(f"{drift[-1]:+,.1f} ms after {xs[-1]:.0f} s",
                    xy=(xs[-1], drift[-1]), xytext=offset,
                    textcoords="offset points", ha="right",
                    fontsize=8.5, color=INK)

    ax.set_xlabel("seconds since first measured frame")
    ax.set_ylabel("drift from ideal 6.944 ms grid (ms)")
    ax.set_title("Schedule drift over 60 s at 144 Hz — high-res timer, "
                 "absolute vs relative rescheduling",
                 fontsize=11, color=INK)
    ax.legend(loc="upper left", frameon=False, fontsize=8.5)
    style_axes(ax)
    save(fig, out_path, raw, footer)


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("raw_dir", help="run_plots.py output directory")
    ap.add_argument("--out-dir", default=str(Path("docs") / "plots"),
                    help="where the PNGs go (default docs/plots)")
    ap.add_argument("--footer", default="",
                    help="small provenance line drawn in each figure corner "
                         "(e.g. 'commit abc1234, 2026-07-28, Core Ultra 7 155H')")
    args = ap.parse_args()

    raw = Path(args.raw_dir)
    out_dir = Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    fig_histogram(raw, out_dir / "frametime-hist-144.png", args.footer)
    fig_drift(raw, out_dir / "drift-60s.png", args.footer)


if __name__ == "__main__":
    main()
