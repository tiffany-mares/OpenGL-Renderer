#!/usr/bin/env python3
"""Phase 6d README-figure runner.

Runs the five cells behind the two README figures: three 144 Hz histogram
runs (sleep / timer / timer_spin, 10,000 frames each) and two 60-second drift
runs (high-res timer strategy, absolute vs relative rescheduling,
9,140 frames = 500 warmup + 8,640 measured at 144 Hz). Saves each run's frame
CSV and stdout, then invokes plot_frames.py -- the only step that needs
matplotlib; everything before it is stdlib-only, and the raw data survives on
disk if the plot step fails.

~7 minutes total -- run on AC power with the machine otherwise idle or the
tails are noise.
"""
import argparse
import re
import subprocess
import sys
import time
from pathlib import Path

HIST_STRATEGIES = ["sleep", "timer", "timer_spin"]
DRIFT_POLICIES = ["absolute", "relative"]
RUN_TIMEOUT_S = 300  # slowest cell is hist-sleep (~90 s); drift-relative ~74 s
BENCH_RE = re.compile(r"bench: frames=(\d+)")


def run_cell(name: str, cmd: list, out: Path, expected_frames: int) -> None:
    csv_path = out / f"{name}.csv"
    if csv_path.exists():
        sys.exit(f"FATAL: {name}.csv already exists in {out} -- refusing to "
                  f"overwrite prior run data")
    txt_path = out / f"{name}.txt"
    print(f"[{name}] {' '.join(cmd)}", flush=True)
    with open(txt_path, "w") as txt:
        result = subprocess.run(cmd, stdout=txt, stderr=subprocess.STDOUT,
                                timeout=RUN_TIMEOUT_S)
    if result.returncode != 0:
        sys.exit(f"FATAL: {name} exited {result.returncode}; see {txt_path}")
    # cube exits 0 when its window closes mid-bench, so a nonzero returncode
    # alone can't catch a truncated run -- confirm the bench: line is present
    # AND its frame count matches what we asked for.
    text = txt_path.read_text()
    m = BENCH_RE.search(text)
    if not m:
        sys.exit(f"FATAL: {name} produced no bench: line -- run truncated? "
                  f"see {txt_path}")
    actual_frames = int(m.group(1))
    if actual_frames != expected_frames:
        sys.exit(f"FATAL: {name} reported frames={actual_frames}, expected "
                  f"{expected_frames} -- run truncated? see {txt_path}")


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--exe", default=str(Path("build") / "Release" / "cube.exe"),
                    help="path to the cube binary")
    ap.add_argument("--hist-frames", type=int, default=10000,
                    help="frames per histogram cell (first 500 are warmup)")
    ap.add_argument("--drift-frames", type=int, default=9140,
                    help="frames per drift cell (500 warmup + 8640 = 60 s at 144 Hz)")
    ap.add_argument("--out", default=None,
                    help="output dir (default bench/results/raw/<timestamp>)")
    ap.add_argument("--no-plot", action="store_true",
                    help="collect data only; skip the matplotlib step")
    args = ap.parse_args()

    out = Path(args.out) if args.out else (
        Path("bench") / "results" / "raw" / time.strftime("%Y%m%d-%H%M%S"))
    out.mkdir(parents=True, exist_ok=True)

    for strategy in HIST_STRATEGIES:
        name = f"hist-{strategy}-144"
        run_cell(name, [args.exe, "--fps=144", f"--pace={strategy}",
                        f"--bench-frames={args.hist_frames}",
                        f"--log={out / (name + '.csv')}"], out, args.hist_frames)

    for policy in DRIFT_POLICIES:
        name = f"drift-{policy}-144"
        run_cell(name, [args.exe, "--fps=144", "--pace=timer",
                        f"--resched={policy}",
                        f"--bench-frames={args.drift_frames}",
                        f"--log={out / (name + '.csv')}"], out, args.drift_frames)

    print(f"raw results in {out}", flush=True)
    if not args.no_plot:
        subprocess.run([sys.executable,
                        str(Path(__file__).with_name("plot_frames.py")),
                        str(out)], check=True)


if __name__ == "__main__":
    main()
