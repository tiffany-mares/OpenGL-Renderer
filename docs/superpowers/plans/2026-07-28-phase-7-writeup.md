# Phase 7: The Writeup — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Restructure the README so the repo is the artifact — one-sentence description, an in-app-captured GIF of the cube, the two figures, headline results tables, a five-entry decision log, known limitations, and clean-machine build instructions — plus the capture tooling (`--capture` + `bench/make_gif.py`) that produces the GIF reproducibly.

**Architecture:** One new C++ capability (raw frame capture: `glReadPixels` pre-swap into a preallocated `CaptureBuffer`, written only on exit — the same discipline as `FrameLog`), one new dev-only Python script (Pillow GIF assembly), then a full README rewrite that folds the existing deep-dive prose into a decision log and limitations section without losing either honesty story or any measured number.

**Tech Stack:** C++20, OpenGL 3.3 core, GLFW 3.4, CMake ≥3.21, Python (stdlib + Pillow dev-only).

## Context

Phases 0–6d are complete: the renderer, three input backends, the frame pacer, and the full benchmark suite with committed results (`bench/results/2026-07-27-pacing-matrix.md`, `2026-07-27-handoff.md`, `2026-07-28-plots.md`) and two README figures. Phase 7 turns the README into the deliverable. User's spec: (1) one-sentence description + GIF; (2) the two plots immediately; (3) results tables from 6b and 6c; (4) decision log — five entries, each *decision / alternatives considered / evidence / what would change my mind*; (5) known limitations — the seqlock data race, macOS lacking the time-constraint policy, no GPU-side timing; (6) build instructions that actually work on a clean machine.

Decisions locked in (user AFK, recommended defaults): GIF via **in-app capture + committed assembly script** (reproducible, fits the repo ethos); old README sections **folded** into the new outline (nothing measured lost — full detail stays in `bench/results/`); **headline tables + links**, not all seven tables.

## Global Constraints

- Every number in the README must appear verbatim in a committed results doc (`bench/results/*.md`) — no uncommitted or remembered measurements.
- The two honesty stories must survive recognizably: the seqlock UB admission with the Boehm citation (currently README 61–70, mirrored at `src/input_state.h:102-108`) and the 6c measured-zeros/crossover story (currently README 192–203).
- CLI flags accept both `--flag=value` and `--flag value`; bad values print `bad --<flag> value ...` + usage to stderr and exit 1, **before `glfwInit`** (headless ctest rejection suites depend on this).
- No allocation or file IO inside a timed frame; buffers preallocated before the loop; everything written on exit; parseable exit lines (`bench:`, `handoff:`, now `capture:`).
- Python under `bench/` is stdlib-only except `plot_frames.py` (matplotlib) and the new `make_gif.py` (Pillow) — both dev-only, never in CI. Missing-dependency failures are a clear `FATAL:` message via `sys.exit`.
- CI (windows-latest + ubuntu-latest, `.github/workflows/build.yml`) must stay green with zero new CI dependencies; nothing in ctest invokes Python.
- Committed GIF hard ceiling 3 MB (script-enforced FATAL), target ≤1.5 MB; commit the binary once — never iterate GIF blobs in history.
- CLAUDE.md must be updated when the phase lands (its own instruction, line 7).
- Commit message style: `feat:` / `docs:` / `fix:` prefixes, matching the existing log.

## Design decisions (rationale the tasks implement)

- **Capture flags:** `--capture PATH` (raw dump path) + `--capture-frames N` (1..500), both requiring `--fps`. The 500 cap bounds worst-case preallocation at 500 × 1280×720×3 ≈ 1.38 GB.
- **Seamless loop by construction:** in capture mode the render loop uses a synthetic per-frame `dt = 2π / (0.9 × capture_frames)` instead of wall-clock `now − prev` (0.9 rad/s is the auto-spin rate at `renderer.cpp:226`). N captured frames then span **exactly one full rotation** — the GIF loops seamlessly for any `--stride` that divides N, and capture is deterministic run-to-run. Run of record: **N=350** (divisible by 2, 5, 7) at `--fps=50` (50 fps = exactly 2 cs GIF frame delay — whole-centisecond delays are required; browsers clamp anything below 2 cs to 10 cs). At 50 fps the synthetic dt is 0.019952 s vs real 0.02 s — 0.24% off wall time, invisible.
- **glReadPixels placement:** after `glDrawElements` (renderer.cpp:247) and the workload line (249), **before `glfwSwapBuffers`** (251) — back-buffer contents are undefined after swap. `glPixelStorei(GL_PACK_ALIGNMENT, 1)` + `glReadBuffer(GL_BACK)` once at init.
- **Resize mid-capture:** `CaptureBuffer` latches the construction-time framebuffer size (from the `FramebufferSize` side channel, populated by main before the thread starts — main.cpp:153-158); on mismatch `next_frame` returns nullptr, `resized_` latches, capture stops with what it has; reported in the `capture:` line and JSON meta; `make_gif.py` refuses truncated captures.
- **Raw format:** headerless tightly-packed frames (`GL_RGB`/`GL_UNSIGNED_BYTE`, rows **bottom-up** exactly as glReadPixels delivers — no per-frame CPU flip), frame i at offset `i*w*h*3`, plus a human-inspectable JSON sidecar at `PATH + ".json"`.
- **Honesty:** the `capture:` exit line reports `missed=` (pacer misses) — the ~2.6 MB/frame readback stall is reported, never hidden. Capture runs are not benchmark runs; no AC/idle protocol required.
- **Corrected claim:** the decision-log entry on the spin margin cites only committed facts (sleep_for wakes 2–3 ms late on this machine per the pacing-matrix machine notes; timer-only start-to-start p99 8.874 ms per the plots doc) — an earlier draft's "~24 µs mean overshoot" figure appears in no committed doc and is excluded.

## File map

- Create: `src/capture.h` (CaptureBuffer, modeled on `src/frame_log.h`), `tests/capture_test.cpp`, `bench/make_gif.py`, `docs/cube.gif`, `bench/results/2026-07-28-gif.md`, `docs/superpowers/plans/2026-07-28-phase-7-writeup.md` (copy of this plan, Task 1).
- Modify: `src/main.cpp` (flags, validation, usage, provenance prints, RenderConfig), `src/renderer.h` (two RenderConfig fields), `src/renderer.cpp` (capture init, synthetic dt, pre-swap read, self-terminate, exit write + `capture:` line), `CMakeLists.txt` (capture_tests + 4 rejection suites), `README.md` (full rewrite), `CLAUDE.md` (Phase 7 record + usage line).

---

### Task 1: Capture mode (C++ + tests)

**Files:**
- Create: `src/capture.h`, `tests/capture_test.cpp`, `docs/superpowers/plans/2026-07-28-phase-7-writeup.md` (copy this plan file there verbatim)
- Modify: `src/renderer.h:13-19` (RenderConfig), `src/main.cpp` (parse loop 30–92, validation 93–107, usage 85–89, provenance prints 113–128, cfg 163–168), `src/renderer.cpp` (init before loop ~171–206, dt at 224–230, pre-swap hook at 249–251, terminate at 276–285, exit write ~304–317), `CMakeLists.txt` (after line 76)

**Interfaces:**
- Consumes: `FramebufferSize::load(int&,int&)` (src/input_state.h), `RenderConfig`, `pacer->missed()`, the FrameLog preallocation pattern (src/frame_log.h).
- Produces: `class CaptureBuffer` (see Step 3 for exact signatures); `RenderConfig.capture_path` (`const char*`, null = off) and `RenderConfig.capture_frames` (`uint32_t`); flags `--capture PATH` / `--capture-frames N`; raw+JSON file contract and the `capture:` exit line (Task 2 consumes all three):
  `capture: path=<s> frames=<u> requested=<u> width=<d> height=<d> fps=<u> bytes=<llu> resized=<0|1> missed=<llu>`

- [ ] **Step 1: Write the failing test** — create `tests/capture_test.cpp` (dependency-free, same self-checking style as the other tests: a `check` helper, `main` returns nonzero on failure; no GL, no GLFW):

```cpp
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "capture.h"

static int failures = 0;
static void check(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

// Read a whole file into a string; empty on failure.
static std::string slurp(const char* path) {
    std::FILE* f = std::fopen(path, "rb");
    if (!f) return {};
    std::string out;
    char buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
    std::fclose(f);
    return out;
}

int main() {
    // Plane geometry: pointers spaced exactly w*h*3 apart, frames counted.
    {
        CaptureBuffer cb(3, 4, 2);  // 3 frames of 4x2 RGB = 24 bytes/frame
        check(cb.frame_bytes() == 24, "frame_bytes = w*h*3");
        uint8_t* p0 = cb.next_frame(4, 2);
        check(p0 != nullptr, "first plane available");
        cb.commit();
        uint8_t* p1 = cb.next_frame(4, 2);
        check(p1 == p0 + 24, "planes tightly packed");
        cb.commit();
        check(cb.frames() == 2 && !cb.done(), "two committed, not done");
        cb.next_frame(4, 2);
        cb.commit();
        check(cb.done() && cb.frames() == 3, "full after max_frames");
        check(cb.next_frame(4, 2) == nullptr, "full buffer returns null");
        check(!cb.resized(), "no resize seen");
    }
    // Resize mid-capture: null plane, resized latched, done.
    {
        CaptureBuffer cb(4, 4, 2);
        cb.next_frame(4, 2);
        cb.commit();
        check(cb.next_frame(5, 2) == nullptr, "size mismatch returns null");
        check(cb.resized() && cb.done(), "resize latches and stops capture");
        check(cb.frames() == 1, "frames unchanged by rejected plane");
    }
    // write(): raw file is exactly frames*w*h*3 bytes; sidecar carries meta.
    {
        CaptureBuffer cb(2, 4, 2);
        uint8_t* p = cb.next_frame(4, 2);
        std::memset(p, 0xAB, cb.frame_bytes());
        cb.commit();
        p = cb.next_frame(4, 2);
        std::memset(p, 0xCD, cb.frame_bytes());
        cb.commit();
        const char* path = "capture_test_tmp.raw";
        check(cb.write(path, 50), "write succeeds");
        std::string raw = slurp(path);
        check(raw.size() == 48, "raw file is frames*w*h*3 bytes");
        check((uint8_t)raw[0] == 0xAB && (uint8_t)raw[24] == 0xCD,
              "frames written in order");
        std::string meta = slurp("capture_test_tmp.raw.json");
        check(meta.find("\"width\":4") != std::string::npos, "meta width");
        check(meta.find("\"height\":2") != std::string::npos, "meta height");
        check(meta.find("\"frames\":2") != std::string::npos, "meta frames");
        check(meta.find("\"requested\":2") != std::string::npos, "meta requested");
        check(meta.find("\"fps\":50") != std::string::npos, "meta fps");
        check(meta.find("\"row_order\":\"bottom-up\"") != std::string::npos,
              "meta row order");
        check(meta.find("\"resized\":false") != std::string::npos, "meta resized");
        std::remove(path);
        std::remove("capture_test_tmp.raw.json");
    }
    if (failures) {
        std::fprintf(stderr, "%d capture test(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("capture tests passed\n");
    return EXIT_SUCCESS;
}
```

- [ ] **Step 2: Add the test target and run it to verify it fails to compile** — append to `CMakeLists.txt` after line 76:

```cmake
add_executable(capture_tests tests/capture_test.cpp)
target_include_directories(capture_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME capture_tests COMMAND capture_tests)
```

Run: `cmake -B build && cmake --build build --config Release --target capture_tests`
Expected: FAIL — `capture.h: No such file or directory`.

- [ ] **Step 3: Implement `src/capture.h`**

```cpp
#pragma once
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

// Phase 7: raw frame capture for the README GIF. Same discipline as the
// frame log (frame_log.h): one preallocation before the first frame, no
// allocation or file IO inside the render loop, everything written on exit.
// Frames are stored exactly as glReadPixels delivers them — tightly packed
// GL_RGB / GL_UNSIGNED_BYTE, rows bottom-up; bench/make_gif.py flips them.

class CaptureBuffer {
public:
    // One reserve for the whole run. Size comes from the FramebufferSize
    // side channel, which main populated before the render thread started.
    CaptureBuffer(uint32_t max_frames, int w, int h)
        : max_frames_(max_frames), width_(w), height_(h) {
        data_.resize(static_cast<size_t>(max_frames) * frame_bytes());
    }

    size_t frame_bytes() const {
        return static_cast<size_t>(width_) * static_cast<size_t>(height_) * 3;
    }

    // Hot path. The plane for the next frame, or nullptr when the buffer is
    // full or the framebuffer no longer matches the latched size — a
    // mid-capture resize stops the capture rather than corrupting it.
    uint8_t* next_frame(int w, int h) {
        if (frames_ == max_frames_) return nullptr;
        if (w != width_ || h != height_) {
            resized_ = true;
            return nullptr;
        }
        return data_.data() + static_cast<size_t>(frames_) * frame_bytes();
    }
    void commit() { ++frames_; }

    bool done() const { return frames_ == max_frames_ || resized_; }
    uint32_t frames() const { return frames_; }
    bool resized() const { return resized_; }
    int width() const { return width_; }
    int height() const { return height_; }

    // Exit-time only: raw planes to `path`, sidecar meta to `path` + ".json".
    bool write(const char* path, uint32_t fps) const {
        std::FILE* f = std::fopen(path, "wb");
        if (!f) {
            std::fprintf(stderr, "capture: cannot open '%s'\n", path);
            return false;
        }
        const size_t bytes = static_cast<size_t>(frames_) * frame_bytes();
        const bool data_ok =
            bytes == 0 || std::fwrite(data_.data(), 1, bytes, f) == bytes;
        std::fclose(f);
        if (!data_ok) {
            std::fprintf(stderr, "capture: write error on '%s'\n", path);
            return false;
        }
        const std::string meta_path = std::string(path) + ".json";
        std::FILE* m = std::fopen(meta_path.c_str(), "w");
        if (!m) {
            std::fprintf(stderr, "capture: cannot open '%s'\n", meta_path.c_str());
            return false;
        }
        std::fprintf(m,
                     "{\"width\":%d,\"height\":%d,\"channels\":3,\"frames\":%u,"
                     "\"requested\":%u,\"fps\":%u,\"pixel_format\":\"RGB8\","
                     "\"row_order\":\"bottom-up\",\"resized\":%s}\n",
                     width_, height_, frames_, max_frames_, fps,
                     resized_ ? "true" : "false");
        const bool meta_ok = std::ferror(m) == 0;
        std::fclose(m);
        if (!meta_ok)
            std::fprintf(stderr, "capture: write error on '%s'\n", meta_path.c_str());
        return meta_ok;
    }

private:
    std::vector<uint8_t> data_;
    uint32_t max_frames_;
    uint32_t frames_ = 0;
    int width_, height_;
    bool resized_ = false;
};
```

- [ ] **Step 4: Run the unit test to verify it passes**

Run: `cmake --build build --config Release --target capture_tests && ctest --test-dir build -C Release -R capture_tests --output-on-failure`
Expected: PASS, `capture tests passed`.

- [ ] **Step 5: Wire the flags into `src/main.cpp`.** Add locals after line 29 (`uint32_t poll_hz = 1000;`):

```cpp
    const char* capture_path = nullptr;  // Phase 7: raw frame dump for the GIF
    uint32_t capture_frames = 0;
```

Add parse branches before the final `else` (usage) branch, following the `--log` / `--poll-hz` patterns exactly:

```cpp
        } else if (arg.rfind("--capture=", 0) == 0 || (arg == "--capture" && i + 1 < argc)) {
            capture_path = (arg == "--capture") ? argv[++i] : argv[i] + 10;
            if (*capture_path == '\0') {
                std::fprintf(stderr, "bad --capture value: empty path\n");
                return EXIT_FAILURE;
            }
        } else if (arg.rfind("--capture-frames=", 0) == 0 ||
                   (arg == "--capture-frames" && i + 1 < argc)) {
            const char* v = (arg == "--capture-frames") ? argv[++i] : argv[i] + 17;
            char* end = nullptr;
            const unsigned long parsed = std::strtoul(v, &end, 10);
            if (end == v || *end != '\0' || parsed < 1 || parsed > 500) {
                std::fprintf(stderr, "bad --capture-frames value '%s' (want 1..500)\n", v);
                return EXIT_FAILURE;
            }
            capture_frames = static_cast<uint32_t>(parsed);
```

Extend the usage string (lines 85–89) with ` [--capture PATH --capture-frames N]` before the `\n`. Add cross-flag validation after the `--resched` check (line 107), still pre-`glfwInit`:

```cpp
    if (capture_path && fps == 0) {
        std::fprintf(stderr,
                     "--capture requires --fps (capture needs a frame clock for GIF timing)\n");
        return EXIT_FAILURE;
    }
    if (capture_path && capture_frames == 0) {
        std::fprintf(stderr, "--capture requires --capture-frames\n");
        return EXIT_FAILURE;
    }
    if (capture_frames > 0 && !capture_path) {
        std::fprintf(stderr, "--capture-frames requires --capture\n");
        return EXIT_FAILURE;
    }
```

Add a provenance print with the others (after line 128):

```cpp
    if (capture_path)
        std::printf("capture: %s (%u frames)\n", capture_path, capture_frames);
```

Set the config fields (with the other `cfg.` assignments, lines 163–168):

```cpp
    cfg.capture_path = capture_path;
    cfg.capture_frames = capture_frames;
```

- [ ] **Step 6: Extend `RenderConfig` in `src/renderer.h`** — add to the struct (after `bench_frames`, line 18):

```cpp
    const char* capture_path = nullptr;              // Phase 7: raw RGB frames; null = no capture
    uint32_t capture_frames = 0;                     // >0: capture exactly N frames, then exit
```

- [ ] **Step 7: Implement capture in `src/renderer.cpp`.** Add `#include "capture.h"` with the other project includes (after line 12). Before the loop (next to the FrameLog setup, after line 190):

```cpp
    // Phase 7: GIF capture. Preallocated before the first frame (FrameLog
    // discipline); read back pre-swap; file written after the loop. The
    // framebuffer size comes from the side channel main populated before
    // this thread started.
    const bool capturing = cfg.capture_path != nullptr;
    std::unique_ptr<CaptureBuffer> capture;
    if (capturing) {
        int cw = 0, ch = 0;
        fb.load(cw, ch);
        capture = std::make_unique<CaptureBuffer>(cfg.capture_frames, cw, ch);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);  // tightly packed rows
        glReadBuffer(GL_BACK);
    }
```

Replace the dt line (225) so a capture run spans exactly one rotation — N synthetic steps of `dt * 0.9` sum to 2π, which is what makes the GIF loop seamless for any stride dividing N:

```cpp
        constexpr double kTwoPi = 6.283185307179586;
        const double dt = capturing ? kTwoPi / (0.9 * cfg.capture_frames)
                                    : now - prev;
```

Insert the readback between the workload line (249) and `glfwSwapBuffers` (251):

```cpp
        if (capturing) {
            if (uint8_t* dst = capture->next_frame(fb_w, fb_h)) {
                glReadPixels(0, 0, fb_w, fb_h, GL_RGB, GL_UNSIGNED_BYTE, dst);
                capture->commit();
            }
        }
```

Add self-termination next to the bench-mode close (after the `cfg.bench_frames` block, line 285):

```cpp
        if (capturing && capture->done()) {
            glfwSetWindowShouldClose(window, GLFW_TRUE);  // documented any-thread
            break;
        }
```

After the loop, with the other exit-time writes (after the frame-log write, line 317):

```cpp
    if (capturing) {
        const bool ok = capture->write(cfg.capture_path, cfg.fps_cap);
        std::printf("capture: path=%s frames=%u requested=%u width=%d height=%d "
                    "fps=%u bytes=%llu resized=%d missed=%llu\n",
                    cfg.capture_path, capture->frames(), cfg.capture_frames,
                    capture->width(), capture->height(), cfg.fps_cap,
                    static_cast<unsigned long long>(
                        static_cast<uint64_t>(capture->frames()) * capture->frame_bytes()),
                    capture->resized() ? 1 : 0,
                    static_cast<unsigned long long>(pacer ? pacer->missed() : 0));
        if (!ok) failed.store(true);
    }
```

- [ ] **Step 8: Add the four headless rejection suites** to `CMakeLists.txt` (after the capture_tests block from Step 2; all validations fire pre-glfwInit, matching the `--poll-hz`/`--resched` pattern at lines 63–76):

```cmake
# --capture validation happens before glfwInit, so these run headless (CI-safe).
add_test(NAME cube_rejects_capture_frames_zero
         COMMAND cube --fps=50 --capture=x.raw --capture-frames=0)
add_test(NAME cube_rejects_capture_frames_high
         COMMAND cube --fps=50 --capture=x.raw --capture-frames=501)
set_tests_properties(cube_rejects_capture_frames_zero cube_rejects_capture_frames_high
    PROPERTIES PASS_REGULAR_EXPRESSION "bad --capture-frames value" TIMEOUT 15)
add_test(NAME cube_rejects_capture_uncapped
         COMMAND cube --capture=x.raw --capture-frames=100)
set_tests_properties(cube_rejects_capture_uncapped PROPERTIES
    PASS_REGULAR_EXPRESSION "--capture requires --fps" TIMEOUT 15)
add_test(NAME cube_rejects_capture_orphan_frames
         COMMAND cube --fps=50 --capture-frames=100)
set_tests_properties(cube_rejects_capture_orphan_frames PROPERTIES
    PASS_REGULAR_EXPRESSION "--capture-frames requires --capture" TIMEOUT 15)
```

- [ ] **Step 9: Build and run the full test suite**

Run: `cmake --build build --config Release && ctest --test-dir build -C Release --output-on-failure`
Expected: all 15 tests PASS (the 10 existing + capture_tests + 4 capture rejection suites).

- [ ] **Step 10: Manual smoke run** (real window opens for ~1 s):

Run: `build/Release/cube.exe --fps=50 --capture smoke.raw --capture-frames=50`
Expected: a `capture: path=smoke.raw frames=50 requested=50 width=1280 height=720 fps=50 bytes=138240000 resized=0 missed=<small>` line (width/height/bytes scale if the OS opens the window at another size — verify `bytes == frames*width*height*3`); `smoke.raw` exists at exactly that byte size; `smoke.raw.json` matches. Delete both files after checking.

- [ ] **Step 11: Copy this plan** to `docs/superpowers/plans/2026-07-28-phase-7-writeup.md` (verbatim).

- [ ] **Step 12: Commit**

```bash
git add src/capture.h tests/capture_test.cpp src/main.cpp src/renderer.h src/renderer.cpp CMakeLists.txt docs/superpowers/plans/2026-07-28-phase-7-writeup.md
git commit -m "feat: --capture raw RGB frame dump (glReadPixels pre-swap, preallocated, written on exit)"
```

---

### Task 2: make_gif.py, the committed GIF, and its provenance

**Files:**
- Create: `bench/make_gif.py`, `docs/cube.gif`, `bench/results/2026-07-28-gif.md`

**Interfaces:**
- Consumes: Task 1's raw+JSON contract and `capture:` line.
- Produces: `docs/cube.gif` (Task 3 embeds it) and its provenance note.

- [ ] **Step 1: Write `bench/make_gif.py`** (style matches `run_plots.py`: docstring, argparse, `sys.exit("FATAL: ...")` guards, printed stats line):

```python
"""Assemble the README cube GIF from a --capture raw frame dump.

Usage: python bench/make_gif.py CAPTURE_RAW [--out docs/cube.gif]
       [--width 512] [--colors 128] [--stride 1]

CAPTURE_RAW is the path given to cube's --capture flag; the sidecar meta at
CAPTURE_RAW + ".json" must sit next to it. Frames in the raw dump are
bottom-up (glReadPixels row order) and are flipped here. Pillow is a
dev-only dependency, same status as plot_frames.py's matplotlib: never
needed to build, never installed in CI.
"""
import argparse
import json
import os
import sys

try:
    from PIL import Image
except ImportError:
    sys.exit("FATAL: Pillow is required for GIF assembly (pip install Pillow) -- "
             "a dev-only dependency like matplotlib; never needed to build, never in CI")

HARD_CEILING_BYTES = 3_000_000  # a committed GIF larger than this is a bug


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("raw", help="raw dump written by cube --capture")
    ap.add_argument("--out", default="docs/cube.gif")
    ap.add_argument("--width", type=int, default=512, help="output width, px")
    ap.add_argument("--colors", type=int, default=128, help="palette size")
    ap.add_argument("--stride", type=int, default=1,
                    help="keep every Nth frame; must divide the frame count "
                         "or the rotation loop would seam")
    args = ap.parse_args()

    meta_path = args.raw + ".json"
    if not os.path.exists(meta_path):
        sys.exit(f"FATAL: missing sidecar meta {meta_path}")
    with open(meta_path) as f:
        meta = json.load(f)
    if meta.get("resized"):
        sys.exit("FATAL: capture was cut short by a mid-run window resize; re-capture")
    w, h, ch = meta["width"], meta["height"], meta["channels"]
    frames, fps = meta["frames"], meta["fps"]
    if frames != meta["requested"]:
        sys.exit(f"FATAL: capture incomplete: frames={frames} "
                 f"requested={meta['requested']}")
    if frames == 0 or frames % args.stride != 0:
        sys.exit(f"FATAL: --stride {args.stride} does not divide {frames} frames "
                 "-- the rotation loop would seam")
    expected = frames * w * h * ch
    actual = os.path.getsize(args.raw)
    if actual != expected:
        sys.exit(f"FATAL: raw size {actual} != frames*w*h*channels = {expected}")

    out_w = args.width
    out_h = round(h * out_w / w)
    frame_bytes = w * h * ch
    out_frames = []
    with open(args.raw, "rb") as f:
        for i in range(0, frames, args.stride):
            f.seek(i * frame_bytes)
            img = Image.frombytes("RGB", (w, h), f.read(frame_bytes))
            # glReadPixels rows run bottom-up.
            img = img.transpose(Image.Transpose.FLIP_TOP_BOTTOM)
            out_frames.append(img.resize((out_w, out_h), Image.Resampling.LANCZOS))

    # One global palette from the first frame, no dither: the cube is
    # flat-shaded, so a per-frame palette flickers and dithering only
    # bloats the LZW stream and fuzzes the face edges.
    palette = out_frames[0].quantize(colors=args.colors)
    out_frames = [fr.quantize(colors=args.colors, palette=palette,
                              dither=Image.Dither.NONE)
                  for fr in out_frames]

    duration_ms = round(1000 * args.stride / fps)
    out_frames[0].save(args.out, save_all=True, append_images=out_frames[1:],
                       duration=duration_ms, loop=0, optimize=True, disposal=1)
    size = os.path.getsize(args.out)
    print(f"gif: frames={len(out_frames)} size={out_w}x{out_h} "
          f"colors={args.colors} duration_ms={duration_ms} bytes={size}")
    if size > HARD_CEILING_BYTES:
        sys.exit(f"FATAL: {args.out} is {size} bytes (> {HARD_CEILING_BYTES}); "
                 "shrink with --colors 64, --width 384, or --stride 2 and re-run")


if __name__ == "__main__":
    main()
```

- [ ] **Step 2: Verify the script's guards fail loudly.** Run `python bench/make_gif.py does-not-exist.raw` → expect `FATAL: missing sidecar meta does-not-exist.raw.json`, exit 1. (If Pillow is not installed yet: `pip install Pillow`, and record the version for the provenance note.)

- [ ] **Step 3: The run of record.** Interactive window opens for ~7 s — do not touch the keyboard (SPACE would pause the spin and break the exactly-one-rotation loop) and do not resize the window:

```
build/Release/cube.exe --fps=50 --capture bench/results/raw/<YYYYMMDD-HHMMSS>/cube-50fps.raw --capture-frames=350
```

(Create the timestamped dir first; `bench/results/raw/` is already git-ignored.) Record the verbatim `capture:` line. Expect `frames=350 requested=350 resized=0`.

- [ ] **Step 4: Assemble, iterating knobs only in the git-ignored dir.** Run:

```
python bench/make_gif.py bench/results/raw/<ts>/cube-50fps.raw --out docs/cube.gif
```

Record the verbatim `gif:` line. If `bytes` exceeds ~1.5 M, re-run with `--stride 2` (175 frames at 4 cs — same wall-clock speed, seamless since 2 divides 350), then `--colors 64`, then `--width 384`, in that order, until ≤1.5 MB or accept up to the 3 MB ceiling with a note. **Only the final GIF is ever committed — no blob churn in history.** Open the GIF in a browser and check: cube colors right, rotation smooth, loop seam invisible, not upside-down (the flip worked).

- [ ] **Step 5: Write `bench/results/2026-07-28-gif.md`** in the house provenance format (mirror the header style of `2026-07-28-plots.md`):

```markdown
# README cube GIF — 2026-07-28

## Provenance

- **Date:** 2026-07-28 (raw dir `bench/results/raw/<ts>/`, git-ignored)
- **Binary:** built from commit `<Task 1 commit sha>`, Release, MSVC (Visual Studio 16 2019 generator, MSVC 19.29)
- **Machine:** Intel(R) Core(TM) Ultra 7 155H; Intel(R) Arc(TM) Graphics; Windows 11 Home. Capture is not a measurement run — no AC/idle protocol; pacing misses during capture are reported in the `capture:` line below, not hidden.
- **Capture:** `cube.exe --fps=50 --capture bench/results/raw/<ts>/cube-50fps.raw --capture-frames=350`. In capture mode the per-frame dt is synthetic (2π / (0.9 × 350)), so the 350 frames span exactly one rotation and the GIF loops seamlessly; capture is deterministic run-to-run.
- **Assembly:** `python bench/make_gif.py bench/results/raw/<ts>/cube-50fps.raw --out docs/cube.gif` (Pillow <version>, dev-only).

## Stats lines (verbatim)

```
capture: <verbatim line from Step 3>
gif: <verbatim line from Step 4>
```

## Committed artifact

`docs/cube.gif`: <bytes> bytes, <W>x<H>, <N> frames at <D> cs/frame, <colors>-color global palette, no dither.

Regenerate: the two commands above (any timestamped raw dir works).
```

Fill every `<placeholder>` with the real values from Steps 3–4.

- [ ] **Step 6: Commit** (script first, then the artifacts, so the tool exists at the artifact's commit):

```bash
git add bench/make_gif.py
git commit -m "feat: make_gif.py -- capture-to-GIF assembly (Pillow, dev-only)"
git add docs/cube.gif bench/results/2026-07-28-gif.md
git commit -m "docs: the README cube GIF and capture provenance"
```

---

### Task 3: README rewrite + CLAUDE.md

**Files:**
- Modify: `README.md` (full rewrite below), `CLAUDE.md` (line 7 phase record + line 16 usage)

**Interfaces:**
- Consumes: `docs/cube.gif` (Task 2), the committed results docs, current README 7–33 (figures, moved verbatim).
- Produces: the final README.

- [ ] **Step 1: Replace `README.md` with the following complete content.** Rules applied: current lines 7–33 (both figures + captions + regenerate note) move verbatim below the GIF; every number below is verbatim from a committed doc (pacing-matrix.md 144 Hz table lines 54–59; handoff.md Tables A lines 55–59 and C lines 89–105 + crossover lines 112–120; plots.md stats lines; handoff.md Table B line 69 for the p50 latency); the seqlock story is near-verbatim from current 61–70; the crossover story is condensed-but-recognizable from current 192–203.

````markdown
# OpenGL-Renderer

A C++20 threaded OpenGL renderer whose real subject is three systems problems
— thread-affine graphics contexts, a lock-free input handoff, and precise
frame pacing on general-purpose OSes; the rotating cube is the demo, not the
point.

![The demo: a flat-shaded cube rotating one full turn, captured in-app at a paced 50 fps](docs/cube.gif)

*Captured by the app itself (`--capture`: glReadPixels into a preallocated
buffer, written only on exit — the same no-IO-in-timed-frames rule as the
instrumentation) and assembled by `bench/make_gif.py`. Provenance:
[bench/results/2026-07-28-gif.md](bench/results/2026-07-28-gif.md).*

<CURRENT README LINES 7–33 VERBATIM: the frametime-hist-144.png embed +
caption, the drift-60s.png embed + caption, and the "Regenerate both" note>

## Headline numbers

### Pacing at 144 Hz

The full matrix (60/144/240 Hz plus uncapped, four strategies, 10,000 frames
per cell, machine notes and provenance) is
[bench/results/2026-07-27-pacing-matrix.md](bench/results/2026-07-27-pacing-matrix.md);
this is the rate the histogram above shows. Frame time is deadline-to-deadline,
so zero-miss cells sit at the period by construction — the wake jitter each
strategy absorbs is what the histogram plots (start-to-start).

| strategy | p50 ms | p99 ms | max ms | missed (of 9,500) | cpu % |
|---|---|---|---|---|---|
| sleep_for (naive) | 7.153 | 9.792 | 11.048 | 4,750 | 6.3 |
| high-res timer only | 6.944 | 6.944 | 6.944 | 0 | 6.8 |
| timer + spin (shipping default) | 6.944 | 6.944 | 6.944 | 0 | 12.0 |
| pure spin | 6.944 | 6.944 | 6.944 | 0 | 99.4 |

Naive sleep misses half of all deadlines at 144 Hz on Windows. The shipping
default holds every one for 12% of a core; pure spin buys the last sliver of
tail for an entire core.

### Input handoff

Three backends behind one interface (`--input=mutex|bitmask|seqlock`,
default `mutex`): a mutex around a small POD, an atomic key bitmask, and a
seqlock carrying the full payload. Full results — in-app cells, achieved
rates, methodology — in
[bench/results/2026-07-27-handoff.md](bench/results/2026-07-27-handoff.md).

| backend | publish ns/op | read ns/op | in-app reader retries |
|---|---|---|---|
| mutex (default) | 16.6 | 15.9 | 0 in all cells |
| bitmask | 8.8 | 1.5 | 0 in all cells |
| seqlock | 2.3 | 2.1 | 0 in 5 of 6 cells |

Uncontended per-op cost is amortized throughput from 1 M-iteration batches —
comparable across backends, not single-call latency. Where the contended
sweep says the difference starts to matter (reader p99):

| publishes/s | mutex p99 ns | seqlock p99 ns |
|---|---|---|
| 1,000 — this app's real rate | 100 | 100 |
| 10,000 | 200 | 100 |
| 100,000 — the crossover | 300 | 100 |
| 1,000,000 | 1,300 | 200 |
| unthrottled | 2,600 | 600 |

The honest result: an uncontended `std::mutex` round-trip costs ~16.6 ns to
publish and ~15.9 ns to read here. At this app's real rates — 1,000
publishes/s against 144 reads/s — the writer holds the lock ~16,600 ns of
every second, so the expected number of reads that ever meet a held lock is
≈0.002 per second: about one contended read every seven minutes. The measured
zeros agree: five of six app cells logged exactly zero reader retries (the
sixth caught a single writer-preemption event in an overloaded 10 kHz cell).
I built the lock-free backends anyway and measured where that stops being
true: the mutex reader's p99 stays within one 100 ns clock quantum of the
seqlock's until ≈100,000 publishes/s — two orders of magnitude above this
app. Below that, choosing the seqlock is a design statement, not a
performance win.

## Decision log

Each entry: the decision, what else was on the table, the evidence, and what
would change my mind.

### Render on a worker thread, input on main

**Decision.** The main thread owns the window and the event queue only — it
polls keys and publishes a snapshot at ~1 kHz and never touches GL. A
dedicated render thread makes the context current, owns every GL object,
draws, and swaps.

**Alternatives considered.** The classic single-threaded loop — poll, draw,
swap — where the frame cap is also the input sampling clock. The inverse
split (render on main, poll on a worker) is not actually available: GLFW
requires event processing on the main thread, so input is the thing that
cannot move. An event queue shipping every edge across instead of snapshot
publishing.

**Evidence.** The decoupled rates are the payoff: against a 144 Hz consumer
the app publishes at ~968–971 Hz achieved, and measured end-to-end input
latency runs p50 ≈0.64 ms against a 6.9 ms frame period — a frame cap no
longer sets input latency. The cost was paid once, in shutdown ordering:
signal stop → render thread deletes its GL objects and detaches the context
→ join → destroy the window on main.

**What would change my mind.** A windowing layer without the main-thread
event constraint, or a target where second-thread contexts don't exist — the
planned Emscripten build already collapses to single-threaded because
threaded WebGL through GLFW isn't a thing.

### Absolute deadlines, never relative

**Decision.** The pacer schedules `next += period`, never `now + period`. A
missed deadline is counted and the schedule re-anchored at the current time —
the debt is dropped, never repaid with a burst of short frames.

**Alternatives considered.** Relative rescheduling (`now + period`) — the
default-looking choice and the classic bug. Catch-up policies that repay
schedule debt with short frames. Vsync as the frame clock (rejected at
context creation: `glfwSwapInterval(0)` — the pacer owns the clock).

**Evidence.** The drift figure above is two otherwise-identical runs
differing only in this rule: absolute ends **0.345 ms** from ideal after 60
seconds; relative leaks every frame's work time and wake overshoot into the
schedule permanently — **+0.664 ms per frame, 5.7 s behind after a minute**,
taking 65.7 s of wall time to deliver 60 s of frames — while reporting zero
missed deadlines the whole way, by construction: a schedule restarted from
`now` cannot observe itself being late. `--resched=relative` exists solely
to measure this.

**What would change my mind.** A workload whose frames routinely exceed the
period — but an absolute schedule fails that loudly (counted misses), and
the fix is lowering the target rate, not switching to a policy that hides
the same failure as silent drift.

### An adaptive spin margin, not a constant

**Decision.** The pacer sleeps short of the deadline by a margin estimated
online from the measured overshoot of every sleep — Welford mean + 3σ,
clamped to half the period, 1.5 ms bootstrap until 16 samples — then spins
the remainder on the monotonic clock.

**Alternatives considered.** A hardcoded margin (2 ms was the obvious
candidate). No margin at all — the bare `timer` strategy. Pure spin. A
quantile estimator (P²) instead of mean + 3σ.

**Evidence.** The quantity the margin must cover is machine- and power-plan-
dependent: this machine's naive sleep wakes 2–3 ms late, not the full
15.6 ms scheduler tick the classic Windows numbers assume — a constant tuned
on either machine is wrong on the other, in either direction. The price and
payoff are in the tables above: at 144 Hz the bare timer costs 6.8% CPU but
hands its wake jitter straight to frame starts (start-to-start p99
8.874 ms); `timer_spin` costs 12.0% and puts ≈90% of intervals in a single
50 µs bin (p99 7.670 ms); pure spin costs 99.4% of a core.

**What would change my mind.** A measured overshoot distribution
heavy-tailed enough that mean + 3σ under-covers — it would show up as rising
missed-deadline counts in the matrix — would argue for a quantile tracker; a
hard-real-time platform with genuinely bounded overshoot would argue for a
small constant.

### Mutex by default, lock-free behind a flag

**Decision.** `std::mutex` around a small POD is the shipping input handoff.
The lock-free backends (`--input=bitmask|seqlock`) exist, are tested, and
are not the default.

**Alternatives considered.** Shipping the seqlock as default — it wins every
per-op number. The bitmask alone — genuinely lock-free, no torn reads, but
32 bits cannot carry the `publish_ns` timestamp that end-to-end latency
measurement needs. Triple buffering with an atomic pointer swap.

**Evidence.** The handoff tables above: at this app's rates the contention
the lock-free backends exist to avoid essentially never happens (expected
≈0.002 contended reads/s; measured zero retries in five of six cells), and
the crossover where the seqlock first measurably wins is ≈100,000
publishes/s — two orders of magnitude away. The seqlock also carries a real
cost the mutex does not: a formal data race (see Known limitations).

**What would change my mind.** Publish rates approaching the measured 10⁵/s
crossover, a writer that must never block (an audio callback), or multiple
readers — none of which this app has.

### Hand-rolled mat4, no glm

**Decision.** `src/mat4.h` — header-only, column-major, exactly the four
operations the demo needs (multiply, axis-angle rotate, lookAt, perspective)
— is the project's entire math library.

**Alternatives considered.** glm (the default answer, and deliberately on
the project's exclusion list alongside SDL and engines). DirectXMath. Eigen.

**Evidence.** The project's subject is threads, handoff, and pacing; the
matrix code exists to put a cube on screen and to be read. It is one small
header with a dependency-free ctest suite (including a 16-element
perspective reference check), FetchContent stays GLFW-only, and the two real
sharp edges are documented where they cut: column-major layout uploaded with
`transpose=GL_FALSE`, and `zNear`/`zFar` parameter names because Windows
headers `#define near` and `far`.

**What would change my mind.** The first feature needing quaternions, SIMD,
or more than a handful of ops — the moment the matrix code stops being
trivially reviewable, it stops paying for itself and glm goes in.

## Known limitations

**The seqlock's data race.** The seqlock reader copies the payload without
synchronization while the writer may be mid-store. Under the C++ memory
model that is formally a data race — undefined behavior. The sequence-number
retry discards every torn copy on real hardware (torn=0 in all 15
contended-sweep rows), the fences around the copy are the standard practical
construction (Boehm, *Can seqlocks get along with programming language
memory models?*), and every shipping engine contains something shaped like
this. It works on every real CPU; it is still bending a rule of the abstract
machine. Naming the rule being bent is the signal — pretending there isn't
one would be the bug. (Also documented at the code: `src/input_state.h`.)

**macOS timer precision is untuned.** The macOS sleep path uses
`mach_wait_until`, which wakes with ordinary-thread scheduling latency; real
precision there also wants `THREAD_TIME_CONSTRAINT_POLICY` on the render
thread, which is left unset — macOS is not a CI platform here, and the
Welford margin absorbs the observed overshoot either way. The numbers in
this README are from Windows; the Linux path is CI-built and smoke-tested;
the macOS path is compiled-only. (Windows has its own footnote: the
pre-1803 fallback uses `timeBeginPeriod(1)`, which raises the timer
interrupt rate for the entire machine — the code says so.)

**No GPU-side timing.** Every number here is CPU-side: the pacer's monotonic
clock and per-thread CPU time. Instrumented frames deliberately run a fixed
~100 µs synthetic CPU workload so the tables characterize the pacer rather
than GPU or driver variance — which also means the GPU cost of a frame is
never measured. There are no GL timer queries, so driver behavior is visible
only by its side effects: on battery, this machine's Arc driver frame-limits
GL inside SwapBuffers, which is why the benchmark protocol demands AC power.
GPU timestamps would have made that directly observable instead of
inferrable.

## Build & run on a clean machine

Prerequisites: CMake ≥ 3.21, a C++20 compiler (Windows: MSVC 19.29+ /
Visual Studio 2019 or newer; Linux: GCC 10+ or Clang), and network access on
the first configure — GLFW 3.4 is pulled by FetchContent. No other C++
dependencies; the GL loader (glad) is vendored in `extern/`.

Linux needs the X11/Wayland dev packages GLFW builds against (the same list
CI installs):

    sudo apt-get install -y xorg-dev libgl1-mesa-dev libwayland-dev libxkbcommon-dev wayland-protocols

Windows (Visual Studio generator — multi-config, pick Release at build
time):

    cmake -B build
    cmake --build build --config Release
    build\Release\cube.exe

Linux (single-config generators need the build type at configure time, or
you get an unoptimized binary):

    cmake -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build
    build/cube

Tests — unit, smoke, and headless flag-rejection suites; no display needed:

    ctest --test-dir build -C Release --output-on-failure

Arrow keys rotate the cube, SPACE pauses the spin, ESC exits.

Python is **not** required to build or run anything. Two scripts have
dev-only dependencies, never installed in CI: `bench/plot_frames.py`
(matplotlib) and `bench/make_gif.py` (Pillow). Everything else under
`bench/` is stdlib-only.

### CLI reference

Every flag accepts `--flag=value` and `--flag value`. Bad values print
`bad --<flag> value` plus usage to stderr and exit 1, before any window
exists.

| flag | values (default) | requires | what it does |
|---|---|---|---|
| `--input` | `mutex`\|`bitmask`\|`seqlock` (`mutex`) | — | input-handoff backend |
| `--fps` | 0..1000 (0 = uncapped) | — | frame cap; the pacer owns the frame clock, not vsync |
| `--pace` | `sleep`\|`timer`\|`timer_spin`\|`spin` (`timer_spin`) | `--fps` | pacing strategy |
| `--resched` | `absolute`\|`relative` (`absolute`) | `--fps` | rescheduling rule; `relative` exists to measure the drift bug |
| `--log` | PATH | `--fps` or `--bench-frames` | per-frame CSV, preallocated, written on exit |
| `--bench-frames` | 501..10000000 | — | self-terminating bench run; prints a `bench:` line |
| `--poll-hz` | 1..10000 (1000) | — | input publish rate (its own paced loop; prints a `handoff:` line) |
| `--capture` | PATH | `--fps`, `--capture-frames` | raw RGB frame dump for the GIF, written on exit; prints a `capture:` line |
| `--capture-frames` | 1..500 | `--capture` | frames to capture (exactly one cube rotation), then exit |

Each platform gets its sharpest timer: `CreateWaitableTimerExW` with
`CREATE_WAITABLE_TIMER_HIGH_RESOLUTION` on Windows,
`clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME)` retried on `EINTR` on
Linux, `mach_wait_until` on macOS. All timestamps come from
`pacer_now_ns()` — one timeline for deadlines, sleeps, publishes, and
consumes.

### Instrumentation & reproduction

`--log PATH` writes one CSV of per-frame records:

    frame,frame_start_ns,frame_end_ns,frame_time_ns,sleep_requested_ns,sleep_actual_ns,input_latency_ns,missed

`frame_time_ns` is deadline-to-deadline — the cadence the pacer delivered,
not how long the work took. `input_latency_ns` is consume time minus the
payload's `publish_ns`, same monotonic clock (the bitmask backend logs 0:
32 bits cannot carry a timestamp). The buffer is preallocated and the file
written once, on exit — no IO or reallocation ever happens inside a timed
frame. Instrumented frames run a fixed synthetic CPU workload so the numbers
characterize the pacer, not GPU or driver variance.

Reproduce the committed results (AC power, machine otherwise idle — on
battery this machine's GPU driver frame-limits GL and the numbers are
garbage):

    python bench/run_matrix.py    # ~19 min: the 13-cell pacing matrix
    python bench/run_handoff.py   # ~10 min: handoff micro-bench + app cells
    python bench/run_plots.py     # ~7 min:  the two README figures
    build/Release/cube.exe --fps=50 --capture <dir>/cube.raw --capture-frames=350
    python bench/make_gif.py <dir>/cube.raw --out docs/cube.gif
````

Note for the implementer: the `<CURRENT README LINES 7–33 VERBATIM: ...>` placeholder marks where to paste the two figure embeds + captions + regenerate note from the pre-rewrite README, byte-identical (git show HEAD:README.md lines 7–33). It is the only non-literal region in this block.

- [ ] **Step 2: Gate the rewrite.** Verify mechanically:
  - Every number greps against a committed doc: `7.153 9.792 11.048 4,750→4750 6.3 6.8 12.0 99.4` vs pacing-matrix.md; `16.6 15.9 8.8 1.5 2.3 2.1 100 200 300 1,300→1300 2,600→2600 600` vs handoff.md; `0.345 0.664→0.6637 5.7 65.7 8.874 7.670 ≈90% 50 µs` vs plots.md + existing captions; `≈0.002 / 968–971 / 0.64` vs handoff.md (line 75–80: achieved 967.9–970.7 — round honestly as ~968–971; Table B mutex p50 639.5 µs).
  - Both honesty stories present (Boehm citation; "design statement, not a performance win").
  - All relative links resolve (`docs/cube.gif`, both PNGs, three results docs, gif provenance doc).
  - The figures block is byte-identical to pre-rewrite lines 7–33.

- [ ] **Step 3: Update `CLAUDE.md`.** In the line-7 phase paragraph: replace the final sentence `Phases 7+ do not exist yet — update this file as they land.` with:

```
Phase 7 (complete 2026-07-28): the writeup — README restructured as the artifact: one-sentence description + in-app-captured GIF (`docs/cube.gif`), the two Phase 6d figures, headline tables transcribed verbatim from the committed 6b/6c results, a five-entry decision log (decision/alternatives/evidence/what-would-change-my-mind), known limitations (seqlock UB, macOS `THREAD_TIME_CONSTRAINT_POLICY` untuned, no GPU-side timing), and clean-machine build instructions (Linux needs `-DCMAKE_BUILD_TYPE=Release` at configure plus the CI apt list). Capture tooling: `--capture PATH` + `--capture-frames N` (1..500, requires `--fps`, validated pre-glfwInit; four `cube_rejects_capture_*` ctest suites), `src/capture.h` `CaptureBuffer` (FrameLog discipline — one preallocation, no IO in frames, raw bottom-up RGB + `.json` sidecar written on exit, mid-capture resize stops capture), capture-mode dt is synthetic (2π/(0.9·N): N frames span exactly one rotation, so the GIF loops seamlessly for any stride dividing N — run of record N=350 at `--fps=50`), parseable `capture:` exit line includes `missed=` so readback stalls are reported. `bench/make_gif.py` (Pillow, dev-only like matplotlib, never in CI) verifies meta/byte-size, flips rows, global-palette quantize, FATALs above 3 MB; provenance in `bench/results/2026-07-28-gif.md`. Phase 8 does not exist yet — update this file as it lands.
```

In the Build section (line 16), extend the usage line's flag list with `[--capture PATH --capture-frames N]`.

- [ ] **Step 4: Commit**

```bash
git add README.md CLAUDE.md
git commit -m "docs: README rewrite -- GIF, figures, headline tables, decision log, limitations, clean-machine build (Phase 7)"
```

---

### Task 4: Verification pass

**Files:** none expected (fix commits only if something fails).

- [ ] **Step 1: Clean-configure build** — prove the instructions as written from a fresh build dir:

Run: `cmake -B build-clean && cmake --build build-clean --config Release`
Expected: configures (FetchContent cache may be re-fetched), builds with zero warnings-as-errors regressions.

- [ ] **Step 2: Full test suite on the clean build**

Run: `ctest --test-dir build-clean -C Release --output-on-failure`
Expected: all 15 tests PASS. Then delete `build-clean`.

- [ ] **Step 3: README render check** — view the README (e.g. `git show HEAD:README.md`, or push first and check on GitHub): GIF animates and loops cleanly, both PNGs render, tables render, every link resolves.

- [ ] **Step 4: Push and verify CI** — `git push`, then confirm the `build` workflow passes on **both** windows-latest and ubuntu-latest via the GitHub REST API (no `gh` on this machine):

Run: `Invoke-RestMethod https://api.github.com/repos/<owner>/OpenGL-Renderer/actions/runs?per_page=1` and check `conclusion: success` for the head SHA (poll until complete).

- [ ] **Step 5: If anything failed** — fix with a `fix:`-prefixed commit and re-run from the failing step. Otherwise Phase 7 is done: done-condition is the restructured README live at HEAD with the committed GIF, all tables/claims verbatim-traceable to committed results docs, 15/15 ctest, CI green both legs.

---

## Verification (end-to-end)

1. `ctest --test-dir build -C Release --output-on-failure` → 15/15 (10 existing + capture_tests + 4 capture rejection suites).
2. `build/Release/cube.exe --fps=50 --capture <tmp>/s.raw --capture-frames=50` → `capture:` line with `frames=50 resized=0`; raw file byte-exact; sidecar valid JSON.
3. `python bench/make_gif.py <tmp>/s.raw --out <tmp>/s.gif` → `gif:` line; GIF opens, loops, right-side-up.
4. README: numbers grep against `bench/results/*.md`; links resolve; figures block byte-identical to pre-rewrite lines 7–33; both honesty stories present.
5. CI green on windows-latest + ubuntu-latest at the pushed HEAD.

## Self-review notes

- Spec coverage: (1) one-sentence description + GIF → T3 Step 1 header + T1/T2; (2) plots immediately → figures block moved verbatim below GIF; (3) 6b/6c tables → Headline numbers section, verbatim transcriptions; (4) five decision-log entries in the required form → drafted in full; (5) three known limitations → drafted in full; (6) clean-machine build → new build section incl. the `-DCMAKE_BUILD_TYPE=Release` Linux fix CI already uses.
- Type consistency: `CaptureBuffer` signatures in Task 1 Step 3 match the test (Step 1), the renderer hook (Step 7), and the exit line (Step 7). `RenderConfig.capture_path/capture_frames` names match main.cpp (Step 5) and renderer.h (Step 6).
- Deliberate deviations from the Plan-agent draft, both fact-checks: dropped the uncommitted "~24 µs overshoot" claim; replaced N=349 (prime — no valid stride) with N=350 + exact-rotation dt so the loop is seamless by construction at stride 1, 2, 5, or 7.
