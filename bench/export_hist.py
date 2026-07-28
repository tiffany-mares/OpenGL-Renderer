#!/usr/bin/env python3
"""Phase 8b histogram exporter: raw pacing matrix -> web/data/frametime-hist.json.

Bins frame START-TO-START intervals at a fixed 50 us per bin for all 13
matrix cells and writes ONE committed JSON with embedded provenance. The
web dashboard renders this file; nothing is re-measured in the browser.

Start-to-start, deliberately NOT the CSV's frame_time_ns -- the same
reasoning and the same derivation as bench/plot_frames.py (load_starts +
consecutive deltas): deadline-to-deadline re-pins to the period after every
miss-resync, which would fake a spike for every strategy. Warmup is parsed
from each cell's bench: line and dropped.

The raw dir is git-ignored; this script runs on the bench machine and the
JSON is the committed derivative. Stdlib-only, like every runner in bench/.
Never wired into ctest -- CI stays Python-free.
"""
import argparse
import csv
import json
import re
import sys
from pathlib import Path

BIN_NS = 50_000  # 50 us -- same bin width as the committed README figure
STRATEGIES = ["sleep", "timer", "timer_spin", "spin"]
RATES = [60, 144, 240]
BENCH_RE = re.compile(r"bench: frames=(\d+) warmup=(\d+)")

DEFAULT_RAW = Path("bench") / "results" / "raw" / "20260727-210652"
SUMMARY_CSV = Path("bench") / "results" / "2026-07-27-summary.csv"
# Provenance of the run of record (bench/results/2026-07-27-pacing-matrix.md).
SOURCE_COMMIT = "45f87ee"
RUN_DATE = "2026-07-27"
MACHINE = ("Intel(R) Core(TM) Ultra 7 155H; Intel(R) Arc(TM) Graphics; "
           "Windows 11 Home; AC power, idle")
METHOD = ("start-to-start deltas of post-warmup frame_start_ns; warmup "
          "parsed from each cell's bench: line; bin index = delta // bin_ns; "
          "derivation identical to bench/plot_frames.py")


def cell_stem(strategy: str, rate: int) -> str:
    # The uncapped cell's files are uncapped.csv/.txt, not uncapped-0.*.
    return "uncapped" if strategy == "uncapped" else f"{strategy}-{rate}"


def load_starts(raw: Path, name: str) -> list:
    """Post-warmup frame_start_ns column (bench/plot_frames.py load_starts)."""
    txt_path = raw / f"{name}.txt"
    if not txt_path.is_file():
        sys.exit(f"FATAL: {txt_path} is missing -- wrong --raw dir?")
    m = BENCH_RE.search(txt_path.read_text())
    if not m:
        sys.exit(f"FATAL: no bench: line in {name}.txt -- was this a bench run?")
    warmup = int(m.group(2))
    with open(raw / f"{name}.csv", newline="") as f:
        rows = list(csv.DictReader(f))
    starts = [int(r["frame_start_ns"]) for r in rows[warmup:]]
    if len(starts) < 2:
        sys.exit(f"FATAL: {name}.csv has <2 post-warmup rows -- truncated log?")
    return starts


def bin_intervals(starts):
    """Sparse 50 us histogram: ascending bin indices + parallel counts.

    Bin b covers [b*BIN_NS, (b+1)*BIN_NS) ns."""
    acc = {}
    for a, b in zip(starts, starts[1:]):
        idx = (b - a) // BIN_NS
        acc[idx] = acc.get(idx, 0) + 1
    bins = sorted(acc)
    return bins, [acc[b] for b in bins]


def selftest() -> None:
    # deltas: 50_000, 50_000, 49_999, 200_001 -> bin 1 x2, bin 0 x1, bin 4 x1.
    # Checks the left-closed edge (a delta of exactly BIN_NS lands in bin 1).
    starts = [0, 50_000, 100_000, 149_999, 350_000]
    bins, counts = bin_intervals(starts)
    assert bins == [0, 1, 4], f"selftest bins {bins}"
    assert counts == [1, 2, 1], f"selftest counts {counts}"
    assert sum(counts) == len(starts) - 1
    print("selftest: ok")


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--raw", default=None,
                    help=f"raw pacing-matrix dir (default {DEFAULT_RAW})")
    ap.add_argument("--out", default=None,
                    help="output JSON (default web/data/frametime-hist.json)")
    ap.add_argument("--selftest", action="store_true",
                    help="run the synthetic binning check and exit")
    args = ap.parse_args()
    if args.selftest:
        selftest()
        return

    root = Path(__file__).resolve().parent.parent
    default_raw = args.raw is None
    raw = Path(args.raw) if args.raw else root / DEFAULT_RAW
    out = Path(args.out) if args.out else root / "web" / "data" / "frametime-hist.json"
    if not raw.is_dir():
        sys.exit(f"FATAL: raw dir {raw} not found -- the run of record is "
                 f"git-ignored; this script runs on the bench machine")

    summary = root / SUMMARY_CSV
    if not summary.is_file():
        sys.exit(f"FATAL: {summary} is missing -- nothing to cross-check against")
    with open(summary, newline="") as f:
        expected_n = {(r["strategy"], int(r["rate_hz"])): int(r["n"])
                      for r in csv.DictReader(f)}

    cell_list = [(s, r) for s in STRATEGIES for r in RATES] + [("uncapped", 0)]
    cells = {}
    interval_ns = set()
    for strategy, rate in cell_list:
        key = f"{strategy}-{rate}"
        if (strategy, rate) not in expected_n:
            sys.exit(f"FATAL: {key} absent from {summary.name}")
        starts = load_starts(raw, cell_stem(strategy, rate))
        if len(starts) != expected_n[(strategy, rate)]:
            sys.exit(f"FATAL: {key}: {len(starts)} post-warmup rows != "
                     f"summary n={expected_n[(strategy, rate)]} -- wrong raw dir?")
        bins, counts = bin_intervals(starts)
        n = len(starts) - 1  # intervals, one fewer than frames
        if sum(counts) != n:
            sys.exit(f"FATAL: {key}: bin counts sum {sum(counts)} != {n} intervals")
        cells[key] = {"n": n, "bins": bins, "counts": counts}
        interval_ns.add(n)
    if len(interval_ns) != 1:
        sys.exit(f"FATAL: cells have mismatched interval counts {sorted(interval_ns)}")

    doc = {
        "provenance": {
            "generated_by": "bench/export_hist.py",
            "raw_dir": raw.name if default_raw else str(raw),
            "source_commit": SOURCE_COMMIT if default_raw else "unspecified (non-default --raw)",
            "run_date": RUN_DATE if default_raw else "unspecified (non-default --raw)",
            "machine": MACHINE if default_raw else "unspecified (non-default --raw)",
            "method": METHOD,
            "results_doc": "bench/results/2026-07-27-pacing-matrix.md",
        },
        "bin_ns": BIN_NS,
        "cells": cells,
    }
    out.parent.mkdir(parents=True, exist_ok=True)
    text = json.dumps(doc, separators=(",", ":")) + "\n"
    out.write_text(text, newline="\n")

    nonzero = sum(len(c["bins"]) for c in cells.values())
    print(f"hist: cells={len(cells)} intervals_per_cell={interval_ns.pop()} "
          f"bin_ns={BIN_NS} nonzero_bins={nonzero} bytes={len(text)} out={out}",
          flush=True)


if __name__ == "__main__":
    main()
