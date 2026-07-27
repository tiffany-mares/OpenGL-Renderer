#!/usr/bin/env python3
"""Phase 6b pacing benchmark matrix runner.

Runs cube once per configuration (4 strategies x 3 rates + uncapped), saving
each run's frame CSV and stdout, then summarizes. ~19 minutes at the default
10,000 frames -- run with the machine otherwise idle or the tails are noise.
"""
import argparse
import subprocess
import sys
import time
from pathlib import Path

STRATEGIES = ["sleep", "timer", "timer_spin", "spin"]
RATES = [60, 144, 240]
RUN_TIMEOUT_S = 600  # sleep-60 is the slowest legitimate cell (~167 s + tick slop)


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--exe", default=str(Path("build") / "Release" / "cube.exe"),
                    help="path to the cube binary")
    ap.add_argument("--frames", type=int, default=10000,
                    help="frames per run (first 500 are warmup)")
    ap.add_argument("--out", default=None,
                    help="output dir (default bench/results/raw/<timestamp>)")
    args = ap.parse_args()

    out = Path(args.out) if args.out else (
        Path("bench") / "results" / "raw" / time.strftime("%Y%m%d-%H%M%S"))
    out.mkdir(parents=True, exist_ok=True)

    runs = [(s, r) for s in STRATEGIES for r in RATES] + [("uncapped", 0)]
    for strategy, rate in runs:
        name = f"{strategy}-{rate}" if rate else "uncapped"
        csv_path = out / f"{name}.csv"
        txt_path = out / f"{name}.txt"
        cmd = [args.exe, f"--bench-frames={args.frames}", f"--log={csv_path}"]
        if rate:
            cmd += [f"--fps={rate}", f"--pace={strategy}"]
        print(f"[{name}] {' '.join(cmd)}", flush=True)
        with open(txt_path, "w") as txt:
            result = subprocess.run(cmd, stdout=txt, stderr=subprocess.STDOUT,
                                    timeout=RUN_TIMEOUT_S)
        if result.returncode != 0:
            sys.exit(f"FATAL: {name} exited {result.returncode}; see {txt_path}")

    print(f"raw results in {out}", flush=True)
    subprocess.run([sys.executable, str(Path(__file__).with_name("summarize.py")),
                    str(out)], check=True)


if __name__ == "__main__":
    main()
