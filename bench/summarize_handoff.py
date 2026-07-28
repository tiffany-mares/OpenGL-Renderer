#!/usr/bin/env python3
"""Summarize a raw handoff-benchmark directory into summary.md + summary.csv.

Three tables: (A) uncontended per-op cost, (B) the app table -- input-latency
p50/p99 from the frame CSVs (post-warmup rows per each run's bench: line) and
retries/sec from the handoff: lines, (C) the contended sweep. The bitmask
backend cannot carry publish_ns, so its latency cells are n/a by design.
percentile() and BENCH_RE are copied from bench/summarize.py (kept standalone
on purpose -- coupling would put 6b's committed results at risk).

`--micro-only` skips the six GL app cells and emits only tables A and C
(cost + sweep), for headless CI runners with no GL context.
"""
import csv
import re
import sys
from pathlib import Path

BACKENDS = ["mutex", "bitmask", "seqlock"]
POLL_RATES = [1000, 10000]

BENCH_RE = re.compile(
    r"bench: frames=(\d+) warmup=(\d+) measured=(\d+) "
    r"cpu_ns=(\d+) wall_ns=(\d+) cpu_pct=([\d.]+)")
HANDOFF_RE = re.compile(
    r"handoff: backend=(\w+) poll_hz=(\d+) publishes=(\d+) wall_ns=(\d+) "
    r"achieved_hz=([\d.]+) missed=(\d+) retries=(\d+)")
CLOCK_RE = re.compile(r"handoff_clock: pair_ns=([\d.]+)")
COST_RE = re.compile(
    r"handoff_cost: backend=(\w+) op=(\w+) iters=(\d+) reps=(\d+) "
    r"ns_per_op=([\d.]+)")
SWEEP_RE = re.compile(
    r"handoff_sweep: backend=(\w+) target_hz=(\d+) achieved_hz=([\d.]+) "
    r"reads=(\d+) read_p50_ns=(\d+) read_p99_ns=(\d+) read_max_ns=(\d+) "
    r"write_p99_ns=(\d+) retries=(\d+) retries_per_sec=([\d.]+) "
    r"samples=(\d+) torn=(\d+) window_s=([\d.]+)")


def percentile(sorted_vals, q):
    """Nearest-rank on a pre-sorted list: value at round(q * (n - 1))."""
    if not sorted_vals:
        return 0
    return sorted_vals[round(q * (len(sorted_vals) - 1))]


def load_app_cell(raw: Path, backend: str, hz: int):
    name = f"handoff-{backend}-{hz}"
    txt = (raw / f"{name}.txt").read_text()
    mb = BENCH_RE.search(txt)
    mh = HANDOFF_RE.search(txt)
    if not mb or not mh:
        sys.exit(f"FATAL: missing bench:/handoff: line in {name}.txt")
    warmup = int(mb.group(2))
    with open(raw / f"{name}.csv", newline="") as f:
        rows = list(csv.DictReader(f))
    measured = rows[warmup:]
    if not measured:
        sys.exit(f"FATAL: {name}.csv has no post-warmup rows -- truncated log?")
    lat = sorted(int(r["input_latency_ns"]) for r in measured)
    wall_s = int(mh.group(4)) / 1e9
    return {
        "n": len(lat),
        "lat_p50": percentile(lat, 0.50),
        "lat_p99": percentile(lat, 0.99),
        "achieved_hz": float(mh.group(5)),
        "missed": int(mh.group(6)),
        "retries": int(mh.group(7)),
        "retries_per_sec": int(mh.group(7)) / wall_s if wall_s else 0.0,
    }


def us(ns):
    return f"{ns / 1e3:.1f}"


def main() -> None:
    argv = sys.argv[1:]
    micro_only = "--micro-only" in argv
    if micro_only:
        argv = [a for a in argv if a != "--micro-only"]
    if len(argv) != 1:
        sys.exit("usage: summarize_handoff.py [--micro-only] RAW_DIR")
    raw = Path(argv[0])
    bench_txt = (raw / "handoff_bench.txt").read_text()

    mclock = CLOCK_RE.search(bench_txt)
    if not mclock:
        sys.exit("FATAL: no handoff_clock: line in handoff_bench.txt")
    costs = {(m.group(1), m.group(2)): float(m.group(5))
             for m in COST_RE.finditer(bench_txt)}
    sweeps = [m.groups() for m in SWEEP_RE.finditer(bench_txt)]
    if len(costs) != 6 or not sweeps:
        sys.exit("FATAL: incomplete handoff_cost:/handoff_sweep: lines")

    app = None
    if not micro_only:
        app = {(b, hz): load_app_cell(raw, b, hz)
               for b in BACKENDS for hz in POLL_RATES}

    md = ["# Input-handoff benchmark\n",
          f"Raw dir: `{raw}`  \n"
          f"Timed sweep samples each carry one clock pair "
          f"(~{float(mclock.group(1)):.0f} ns), identical across backends, "
          f"so it cancels in cross-backend comparison. Batched cost means "
          f"amortize the clock to nothing. Bitmask latency is n/a by design "
          f"(cannot carry publish_ns).\n"]

    md.append("\n## A. Uncontended per-op cost (batched median)\n")
    md.append("| backend | publish ns/op | read ns/op |")
    md.append("|---|---|---|")
    for b in BACKENDS:
        md.append(f"| {b} | {costs[(b, 'publish')]:.1f} "
                  f"| {costs[(b, 'read')]:.1f} |")

    if app is not None:
        md.append("\n## B. In-app handoff (144 Hz consumer)\n")
        md.append("| backend | publish cost ns | read cost ns "
                  "| latency p50/p99 us @1 kHz | latency p50/p99 us @10 kHz "
                  "| retries/sec @1 kHz | retries/sec @10 kHz |")
        md.append("|---|---|---|---|---|---|---|")
        for b in BACKENDS:
            cells = []
            for hz in POLL_RATES:
                a = app[(b, hz)]
                if b == "bitmask":
                    cells.append("n/a")
                else:
                    cells.append(f"{us(a['lat_p50'])} / {us(a['lat_p99'])}")
            r1 = app[(b, 1000)]["retries_per_sec"]
            r10 = app[(b, 10000)]["retries_per_sec"]
            md.append(f"| {b} | {costs[(b, 'publish')]:.1f} "
                      f"| {costs[(b, 'read')]:.1f} | {cells[0]} | {cells[1]} "
                      f"| {r1:.2f} | {r10:.2f} |")
        md.append("")
        for b in BACKENDS:
            for hz in POLL_RATES:
                a = app[(b, hz)]
                md.append(f"- {b} @{hz} Hz: achieved_hz={a['achieved_hz']:.1f} "
                          f"missed={a['missed']} n={a['n']}")
    if app is None:
        md.append("\nMicro-bench only run (`--micro-only`): the six GL app "
                  "cells were not run; table B is absent by design.\n")

    md.append("\n## C. Contended sweep (tight-loop reader)\n")
    md.append("| backend | target Hz | achieved Hz | read p50 ns | read p99 ns "
              "| read max ns | write p99 ns | retries/sec | torn |")
    md.append("|---|---|---|---|---|---|---|---|---|")
    for g in sweeps:
        target = "max" if g[1] == "0" else g[1]
        md.append(f"| {g[0]} | {target} | {float(g[2]):.0f} | {g[4]} | {g[5]} "
                  f"| {g[6]} | {g[7]} | {float(g[9]):.1f} | {g[11]} |")

    (raw / "summary.md").write_text("\n".join(md) + "\n")

    with open(raw / "summary.csv", "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["table", "backend", "rate_hz", "publish_ns", "read_ns",
                    "lat_p50_ns", "lat_p99_ns", "retries_per_sec",
                    "achieved_hz", "read_p99_ns", "torn"])
        for b in BACKENDS:
            w.writerow(["cost", b, "", f"{costs[(b, 'publish')]:.1f}",
                        f"{costs[(b, 'read')]:.1f}", "", "", "", "", "", ""])
        if app is not None:
            for b in BACKENDS:
                for hz in POLL_RATES:
                    a = app[(b, hz)]
                    lat50 = "" if b == "bitmask" else a["lat_p50"]
                    lat99 = "" if b == "bitmask" else a["lat_p99"]
                    w.writerow(["app", b, hz, "", "", lat50, lat99,
                                f"{a['retries_per_sec']:.2f}",
                                f"{a['achieved_hz']:.1f}", "", ""])
        for g in sweeps:
            w.writerow(["sweep", g[0], g[1], "", "", "", "",
                        f"{float(g[9]):.1f}", f"{float(g[2]):.1f}",
                        g[5], g[11]])

    print((raw / "summary.md").read_text())


if __name__ == "__main__":
    main()
