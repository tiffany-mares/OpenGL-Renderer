#!/usr/bin/env python3
"""Phase 6c input-handoff benchmark runner.

Runs handoff_bench (uncontended costs + contended sweep), then cube once per
app cell (3 backends x {1,10} kHz poll), saving each run's frame CSV and
stdout, then summarizes. ~10 minutes total -- run on AC power with the
machine otherwise idle or the tails are noise.
"""
import argparse
import subprocess
import sys
import time
from pathlib import Path

BACKENDS = ["mutex", "bitmask", "seqlock"]
POLL_RATES = [1000, 10000]
RUN_TIMEOUT_S = 300


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--exe", default=str(Path("build") / "Release" / "cube.exe"),
                    help="path to the cube binary")
    ap.add_argument("--bench-exe",
                    default=str(Path("build") / "Release" / "handoff_bench.exe"),
                    help="path to the handoff_bench binary")
    ap.add_argument("--frames", type=int, default=10000,
                    help="frames per app cell (first 500 are warmup)")
    ap.add_argument("--out", default=None,
                    help="output dir (default bench/results/raw/<timestamp>)")
    args = ap.parse_args()

    out = Path(args.out) if args.out else (
        Path("bench") / "results" / "raw" / time.strftime("%Y%m%d-%H%M%S"))
    out.mkdir(parents=True, exist_ok=True)

    bench_txt = out / "handoff_bench.txt"
    print(f"[handoff_bench] {args.bench_exe}", flush=True)
    with open(bench_txt, "w") as txt:
        result = subprocess.run([args.bench_exe], stdout=txt,
                                stderr=subprocess.STDOUT, timeout=RUN_TIMEOUT_S)
    if result.returncode != 0:
        sys.exit(f"FATAL: handoff_bench exited {result.returncode}; see {bench_txt}")

    for backend in BACKENDS:
        for hz in POLL_RATES:
            name = f"handoff-{backend}-{hz}"
            csv_path = out / f"{name}.csv"
            txt_path = out / f"{name}.txt"
            cmd = [args.exe, f"--input={backend}", "--fps=144",
                   "--pace=timer_spin", f"--bench-frames={args.frames}",
                   f"--poll-hz={hz}", f"--log={csv_path}"]
            print(f"[{name}] {' '.join(cmd)}", flush=True)
            with open(txt_path, "w") as txt:
                result = subprocess.run(cmd, stdout=txt,
                                        stderr=subprocess.STDOUT,
                                        timeout=RUN_TIMEOUT_S)
            if result.returncode != 0:
                sys.exit(f"FATAL: {name} exited {result.returncode}; see {txt_path}")

    print(f"raw results in {out}", flush=True)
    subprocess.run([sys.executable,
                    str(Path(__file__).with_name("summarize_handoff.py")),
                    str(out)], check=True)


if __name__ == "__main__":
    main()
