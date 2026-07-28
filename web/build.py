#!/usr/bin/env python3
"""Phase 8 web-build driver.

Compiles the whole app (src/*.cpp) to WebAssembly with Emscripten and stages
a servable site in --out: cube.js + cube.wasm + the page, dashboard, vendored
Chart.js, and committed benchmark data. The same three translation units as
the native build; __EMSCRIPTEN__ guards select the single-threaded browser
path (no render thread, no pacer in the loop -- requestAnimationFrame owns
the frame clock). Stdlib-only, like every runner in bench/.

Requires an activated emsdk: emcc must be on PATH (run emsdk_env first).
"""
import argparse
import shutil
import subprocess
import sys
from pathlib import Path

# The Phase 8 build flags, verbatim from the spec (only -o moves to --out).
# EXPORTED_RUNTIME_METHODS=ccall is spec-mandated; nothing calls ccall today.
EMCC_FLAGS = [
    "-std=c++20",
    "-O3",
    "-sUSE_GLFW=3",
    "-sMIN_WEBGL_VERSION=2",
    "-sMAX_WEBGL_VERSION=2",
    "-sALLOW_MEMORY_GROWTH=1",
    "-sEXPORTED_RUNTIME_METHODS=ccall",
]


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--out", default="dist",
                    help="output directory for the servable site (git-ignored)")
    args = ap.parse_args()

    root = Path(__file__).resolve().parent.parent

    # shutil.which honors PATHEXT, so this resolves emcc.bat on Windows.
    emcc = shutil.which("emcc")
    if emcc is None:
        sys.exit("FATAL: emcc not found on PATH -- activate emsdk first "
                 "(emsdk_env.bat on Windows, `source emsdk_env.sh` on POSIX)")

    sources = sorted((root / "src").glob("*.cpp"))
    if not sources:
        sys.exit(f"FATAL: no sources matched {root / 'src'}/*.cpp")

    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    js = out / "cube.js"
    wasm = out / "cube.wasm"

    cmd = ([emcc] + [str(s) for s in sources] + ["-o", str(js)] + EMCC_FLAGS +
           [f"-I{root / 'extern'}", f"-I{root / 'src'}"])
    print(" ".join(cmd), flush=True)
    result = subprocess.run(cmd)
    if result.returncode != 0:
        sys.exit(f"FATAL: emcc exited {result.returncode}")
    for artifact in (js, wasm):
        if not artifact.is_file():
            sys.exit(f"FATAL: emcc exited 0 but {artifact} was not produced")

    # Phase 8b staging: page, dashboard, vendored Chart.js, committed data.
    # The two results CSVs stage under stable dashboard names; they stay
    # committed once under bench/results/, never duplicated in git.
    stage = [
        (root / "web" / "index.html", out / "index.html"),
        (root / "web" / "dashboard.js", out / "dashboard.js"),
        (root / "web" / "vendor" / "chart.umd.min.js",
         out / "vendor" / "chart.umd.min.js"),
        (root / "web" / "data" / "frametime-hist.json",
         out / "data" / "frametime-hist.json"),
        (root / "bench" / "results" / "2026-07-27-summary.csv",
         out / "data" / "pacing-summary.csv"),
        (root / "bench" / "results" / "2026-07-27-handoff-summary.csv",
         out / "data" / "handoff-summary.csv"),
    ]
    for src, dst in stage:
        if not src.is_file():
            sys.exit(f"FATAL: {src} is missing -- the site would deploy incomplete")
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(src, dst)

    print(f"wasm: js_bytes={js.stat().st_size} wasm_bytes={wasm.stat().st_size} "
          f"staged={len(stage)} out={out}", flush=True)


if __name__ == "__main__":
    main()
