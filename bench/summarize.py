#!/usr/bin/env python3
"""Summarize a raw pacing-matrix directory into summary.md and summary.csv.

Per cell: p50/p95/p99/max/stddev of frame_time_ns (rows after the warmup
count parsed from the run's bench: line), missed deadlines, and render-thread
CPU%. frame_time_ns is deadline-to-deadline for paced runs and
frame-start-to-frame-start for the uncapped baseline.
"""
import csv
import re
import statistics
import sys
from pathlib import Path

STRATEGIES = ["sleep", "timer", "timer_spin", "spin"]
RATES = [60, 144, 240]
STRATEGY_LABELS = {
    "sleep": "sleep_for (naive)",
    "timer": "high-res timer only",
    "timer_spin": "timer + spin (default)",
    "spin": "pure spin",
    "uncapped": "uncapped",
}
BENCH_RE = re.compile(
    r"bench: frames=(\d+) warmup=(\d+) measured=(\d+) "
    r"cpu_ns=(\d+) wall_ns=(\d+) cpu_pct=([\d.]+)")


def percentile(sorted_vals, q):
    """Nearest-rank on a pre-sorted list: value at round(q * (n - 1))."""
    if not sorted_vals:
        return 0
    return sorted_vals[round(q * (len(sorted_vals) - 1))]


def load_run(raw: Path, name: str):
    txt = (raw / f"{name}.txt").read_text()
    m = BENCH_RE.search(txt)
    if not m:
        sys.exit(f"FATAL: no bench: line in {name}.txt -- was this a bench run?")
    warmup = int(m.group(2))
    cpu_pct = float(m.group(6))
    with open(raw / f"{name}.csv", newline="") as f:
        rows = list(csv.DictReader(f))
    measured = rows[warmup:]
    ft = sorted(int(r["frame_time_ns"]) for r in measured)
    return {
        "n": len(ft),
        "p50": percentile(ft, 0.50),
        "p95": percentile(ft, 0.95),
        "p99": percentile(ft, 0.99),
        "max": ft[-1] if ft else 0,
        "stddev": statistics.stdev(ft) if len(ft) > 1 else 0.0,
        "missed": sum(int(r["missed"]) for r in measured),
        "cpu_pct": cpu_pct,
    }


def ms(ns):
    return f"{ns / 1e6:.3f}"


def main() -> None:
    if len(sys.argv) != 2:
        sys.exit("usage: summarize.py RAW_DIR")
    raw = Path(sys.argv[1])

    results = {}
    for strategy in STRATEGIES:
        for rate in RATES:
            results[(strategy, rate)] = load_run(raw, f"{strategy}-{rate}")
    results[("uncapped", 0)] = load_run(raw, "uncapped")

    md = [f"# Pacing benchmark matrix\n",
          f"Raw dir: `{raw}`  \n"
          f"frame_time_ns is deadline-to-deadline (paced) or start-to-start "
          f"(uncapped); first-warmup frames discarded per the runs' bench lines. "
          f"CPU%% is render-thread time / wall time over the measured window.\n"]
    header = ("| strategy | p50 ms | p95 ms | p99 ms | max ms | stddev ms "
              "| missed | cpu % |")
    rule = "|---|---|---|---|---|---|---|---|"
    for rate in RATES:
        period_ms = 1000.0 / rate
        md.append(f"\n## {rate} Hz (period {period_ms:.3f} ms)\n")
        md.append(header)
        md.append(rule)
        for strategy in STRATEGIES:
            s = results[(strategy, rate)]
            md.append(f"| {STRATEGY_LABELS[strategy]} | {ms(s['p50'])} "
                      f"| {ms(s['p95'])} | {ms(s['p99'])} | {ms(s['max'])} "
                      f"| {ms(s['stddev'])} | {s['missed']} | {s['cpu_pct']:.1f} |")
    s = results[("uncapped", 0)]
    md.append("\n## Uncapped baseline (no target)\n")
    md.append(header)
    md.append(rule)
    md.append(f"| {STRATEGY_LABELS['uncapped']} | {ms(s['p50'])} | {ms(s['p95'])} "
              f"| {ms(s['p99'])} | {ms(s['max'])} | {ms(s['stddev'])} "
              f"| {s['missed']} | {s['cpu_pct']:.1f} |")

    (raw / "summary.md").write_text("\n".join(md) + "\n")

    with open(raw / "summary.csv", "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["strategy", "rate_hz", "n", "p50_ns", "p95_ns", "p99_ns",
                    "max_ns", "stddev_ns", "missed", "cpu_pct"])
        for (strategy, rate), s in results.items():
            w.writerow([strategy, rate, s["n"], s["p50"], s["p95"], s["p99"],
                        s["max"], f"{s['stddev']:.1f}", s["missed"],
                        f"{s['cpu_pct']:.2f}"])

    print((raw / "summary.md").read_text())


if __name__ == "__main__":
    main()
