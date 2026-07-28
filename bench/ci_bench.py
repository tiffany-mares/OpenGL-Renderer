#!/usr/bin/env python3
"""Phase 8c CI benchmark orchestrator: one runner -> bench/results/ci/<platform>/.

Runs the 13-cell pacing matrix (via run_matrix.py) and the handoff
micro-bench (handoff_bench + summarize_handoff.py --micro-only -- the six
GL app cells are the desktop protocol; the dashboard reads only cost+sweep
rows), exports the histogram JSON, and assembles the four committed files:
pacing-summary.csv, handoff-summary.csv, frametime-hist.json,
provenance.json. Stdlib-only, like every runner in bench/.

These numbers are a DIFFERENT MEASUREMENT CLASS from the desktop run of
record: shared virtualized runner, Mesa llvmpipe software GL, no AC/idle
control. They make the dashboard reproducible and dated; they do not
replace the desktop tables.

Display handling lives in the workflow, not here: on ubuntu-latest this
script is wrapped in `xvfb-run -a` with LIBGL_ALWAYS_SOFTWARE=1; on
windows-latest mesa-dist-win's opengl32.dll sits beside cube.exe before
this runs. The script itself is display-agnostic.
"""
import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import time
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
GL_RE = re.compile(r"GL_VERSION:\s*(.+?)\r?\nGL_RENDERER:\s*(.+?)\r?\n")
SMOKE_FRAMES = 600  # must exceed the 500-frame warmup; uncapped, so seconds

MEASUREMENT_CLASS = (
    "GitHub Actions shared virtualized runner with a software rasterizer "
    "(Mesa llvmpipe) and no AC/idle control -- a different measurement class "
    "from the desktop run of record; compare CI platforms to each other and "
    "across weeks, not to the desktop tables")

PLATFORM_LABELS = {
    "windows-latest":
        "GitHub Actions · windows-latest · Mesa llvmpipe (software GL)",
    "ubuntu-latest":
        "GitHub Actions · ubuntu-latest · Mesa llvmpipe under Xvfb (software GL)",
}


def default_exe() -> Path:
    return (ROOT / "build" / "Release" / "cube.exe" if sys.platform == "win32"
            else ROOT / "build" / "cube")


def default_bench_exe() -> Path:
    return (ROOT / "build" / "Release" / "handoff_bench.exe"
            if sys.platform == "win32" else ROOT / "build" / "handoff_bench")


def cpu_model() -> str:
    if sys.platform == "win32":
        return os.environ.get("PROCESSOR_IDENTIFIER", "unknown")
    try:
        for line in Path("/proc/cpuinfo").read_text().splitlines():
            if line.lower().startswith("model name"):
                return line.split(":", 1)[1].strip()
    except OSError:
        pass
    return "unknown"


def gl_smoke(exe: str):
    """Short uncapped bench run: fails fast if the runner has no usable GL
    context, and yields the GL_VERSION/GL_RENDERER strings for provenance."""
    cmd = [exe, f"--bench-frames={SMOKE_FRAMES}"]
    print(f"[gl-smoke] {' '.join(cmd)}", flush=True)
    result = subprocess.run(cmd, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, timeout=300, text=True)
    if result.returncode != 0:
        sys.exit(f"FATAL: GL smoke run exited {result.returncode} -- no "
                 f"usable GL context on this runner?\n--- captured output ---\n"
                 f"{result.stdout}")
    m = GL_RE.search(result.stdout)
    if not m:
        sys.exit("FATAL: GL smoke output has no GL_VERSION:/GL_RENDERER: "
                 f"lines\n--- captured output ---\n{result.stdout}")
    return m.group(1).strip(), m.group(2).strip()


def run(cmd):
    print("[run] " + " ".join(str(c) for c in cmd), flush=True)
    result = subprocess.run(cmd)
    if result.returncode != 0:
        sys.exit(f"FATAL: {cmd[0]} exited {result.returncode}")


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--platform", required=True,
                    choices=["windows-latest", "ubuntu-latest"])
    ap.add_argument("--frames", type=int, default=10000,
                    help="frames per pacing cell (must exceed the 500 warmup)")
    ap.add_argument("--exe", default=str(default_exe()),
                    help="path to the cube binary")
    ap.add_argument("--bench-exe", default=str(default_bench_exe()),
                    help="path to the handoff_bench binary")
    ap.add_argument("--out-dir", default=None,
                    help="default bench/results/ci/<platform>")
    args = ap.parse_args()
    if args.frames <= 500:
        sys.exit("FATAL: --frames must exceed the 500-frame warmup")

    out_dir = Path(args.out_dir) if args.out_dir else (
        ROOT / "bench" / "results" / "ci" / args.platform)
    out_dir.mkdir(parents=True, exist_ok=True)
    stamp = time.strftime("%Y%m%d-%H%M%S")
    raw_pacing = ROOT / "bench" / "results" / "raw" / f"ci-pacing-{stamp}"
    raw_handoff = ROOT / "bench" / "results" / "raw" / f"ci-handoff-{stamp}"

    # (a) GL smoke -- fail fast, capture provenance strings.
    gl_version, gl_renderer = gl_smoke(args.exe)
    print(f"[gl-smoke] GL_VERSION={gl_version} GL_RENDERER={gl_renderer}",
          flush=True)

    # (b) pacing matrix: 13 cells + summary.csv via the existing runner.
    run([sys.executable, str(ROOT / "bench" / "run_matrix.py"),
         "--exe", args.exe, "--frames", str(args.frames),
         "--out", str(raw_pacing)])

    # (c) handoff micro-bench (fully headless), summarized micro-only.
    raw_handoff.mkdir(parents=True, exist_ok=True)
    bench_txt = raw_handoff / "handoff_bench.txt"
    print(f"[handoff_bench] {args.bench_exe}", flush=True)
    with open(bench_txt, "w") as txt:
        result = subprocess.run([args.bench_exe], stdout=txt,
                                stderr=subprocess.STDOUT, timeout=600)
    if result.returncode != 0:
        sys.exit(f"FATAL: handoff_bench exited {result.returncode}; "
                 f"see {bench_txt}")
    run([sys.executable, str(ROOT / "bench" / "summarize_handoff.py"),
         "--micro-only", str(raw_handoff)])

    # (d) histogram JSON with CI provenance.
    commit = os.environ.get("GITHUB_SHA", "uncommitted")[:7]
    run_date = datetime.now(timezone.utc).strftime("%Y-%m-%d")
    machine = f"{PLATFORM_LABELS[args.platform]}; {cpu_model()}; {gl_renderer}"
    run([sys.executable, str(ROOT / "bench" / "export_hist.py"),
         "--raw", str(raw_pacing),
         "--summary", str(raw_pacing / "summary.csv"),
         "--out", str(out_dir / "frametime-hist.json"),
         "--commit", commit, "--run-date", run_date, "--machine", machine,
         "--results-doc", "bench/results/2026-07-28-ci-pipeline.md"])

    # (e) assemble the committed four.
    shutil.copyfile(raw_pacing / "summary.csv", out_dir / "pacing-summary.csv")
    shutil.copyfile(raw_handoff / "summary.csv",
                    out_dir / "handoff-summary.csv")
    repo = os.environ.get("GITHUB_REPOSITORY", "tiffany-mares/OpenGL-Renderer")
    run_id = os.environ.get("GITHUB_RUN_ID", "")
    provenance = {
        "platform": args.platform,
        "label": PLATFORM_LABELS[args.platform],
        "runner_image": "{} {}".format(
            os.environ.get("ImageOS", "unknown"),
            os.environ.get("ImageVersion", "")).strip(),
        "commit": commit,
        "run_date": run_date,
        "run_id": run_id,
        "run_url": (f"https://github.com/{repo}/actions/runs/{run_id}"
                    if run_id else ""),
        "cpu": cpu_model(),
        "gl_version": gl_version,
        "gl_renderer": gl_renderer,
        "frames_per_cell": args.frames,
        "measurement_class": MEASUREMENT_CLASS,
        "method": ("bench/ci_bench.py: run_matrix.py 13-cell pacing matrix + "
                   "handoff_bench micro-bench (summarize_handoff.py "
                   "--micro-only) + export_hist.py; protocol otherwise "
                   "identical to the desktop runs"),
        "generated_by": "bench/ci_bench.py",
    }
    (out_dir / "provenance.json").write_text(
        json.dumps(provenance, indent=2) + "\n", newline="\n")

    print(f"ci_bench: platform={args.platform} frames={args.frames} "
          f"commit={commit} run_date={run_date} out={out_dir}", flush=True)


if __name__ == "__main__":
    main()
