#!/usr/bin/env python3
"""Cube wasm builder (Phase 8; slimmed in Phase 9b).

Compiles the whole app (src/*.cpp) to WebAssembly with Emscripten and writes
cube.js + cube.wasm into --out. The page that serves them is lab/ -- its
build stages these artifacts via lab/scripts/stage-cube.mjs and deploys a
static prerender (see .github/workflows/pages.yml). The same three
translation units as the native build; __EMSCRIPTEN__ guards select the
single-threaded browser path (no render thread, no pacer in the loop --
requestAnimationFrame owns the frame clock). Stdlib-only, like every runner
in bench/.

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
                    help="output directory for cube.js + cube.wasm (git-ignored)")
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

    print(f"wasm: js_bytes={js.stat().st_size} wasm_bytes={wasm.stat().st_size} "
          f"out={out}", flush=True)


if __name__ == "__main__":
    main()
