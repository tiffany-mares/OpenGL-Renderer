# OpenGL-Renderer

Live at: **https://opengl-renderer.pages.dev/**

A C++20 threaded OpenGL renderer whose real subject is three systems
problems: thread-affine graphics contexts, a lock-free input handoff, and
precise frame pacing on general-purpose OSes. The rotating cube is the demo,
not the point.

**[Live demo + frame pacer lab](https://opengl-renderer.pages.dev/)**, the
cube compiled to WebAssembly as the hook, above an interactive writeup of the
native build's committed benchmark results. The pacer and the threaded render
deliberately do not port; every number on the page is generated at build time
from files committed in this repo, none re-measured in the browser.

## Contents

- [The demo](#the-demo)
- [Quick start](#quick-start)
- [System architecture diagram](#system-architecture-diagram)
- [Measured results](#measured-results): [figures](#the-pacer-visualized),
  [pacing](#pacing-at-144-hz), [input handoff](#input-handoff)
- [Decision log](#decision-log)
- [Known limitations](#known-limitations)
- [Reference](#reference): [CLI flags](#cli-flags),
  [instrumentation](#instrumentation-format),
  [reproduction](#reproducing-the-results),
  [browser build](#the-browser-build-and-deployment)

## The demo

![The demo: a flat-shaded cube rotating one full turn, captured in-app at a paced 50 fps](docs/cube.gif)

*Captured by the app itself (`--capture`: glReadPixels into a preallocated
buffer, written only on exit, the same no-IO-in-timed-frames rule as the
instrumentation) and assembled by `bench/make_gif.py`. Provenance:
[bench/results/2026-07-28-gif.md](bench/results/2026-07-28-gif.md).*

Arrow keys rotate the cube, SPACE pauses the spin, ESC exits.

## Quick start

Prerequisites: CMake ≥ 3.21, a C++20 compiler (Windows: MSVC 19.29+ /
Visual Studio 2019 or newer; Linux: GCC 10+ or Clang), and network access on
the first configure, since GLFW 3.4 is pulled by FetchContent. No other C++
dependencies; the GL loader (glad) is vendored in `extern/`.

Linux needs the X11/Wayland dev packages GLFW builds against (the same list
CI installs):

    sudo apt-get install -y xorg-dev libgl1-mesa-dev libwayland-dev libxkbcommon-dev wayland-protocols

Windows (Visual Studio generator, multi-config, pick Release at build time):

    cmake -B build
    cmake --build build --config Release
    build\Release\cube.exe

Linux (single-config generators need the build type at configure time, or
you get an unoptimized binary):

    cmake -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build
    build/cube

Tests (unit, smoke, and headless flag-rejection suites; no display needed):

    ctest --test-dir build -C Release --output-on-failure

Python is **not** required to build or run anything. Two scripts have
dev-only dependencies, never installed in CI: `bench/plot_frames.py`
(matplotlib) and `bench/make_gif.py` (Pillow). Everything else under
`bench/` is stdlib-only.

## System architecture diagram

Two threads, one channel between them, and a pacer that owns the frame
clock instead of vsync:

[![Architecture: the main thread publishes input snapshots through a swappable InputChannel to the render thread, whose loop consumes, draws, swaps, and waits on the FramePacer; a shared monotonic clock timestamps both sides, and shutdown follows a fixed order](docs/architecture.png)](docs/architecture.png)

Color key: orange is the main thread, teal is the input handoff (the
three `InputChannel` backends), yellow is the render thread, magenta is
the shared monotonic clock, violet is the shutdown sequence, and the
standalone blue nodes are instrumentation and platform infrastructure
(FrameLog, the per-OS timer, the web build). Click the diagram for the
full-size version. The single `pacer_now_ns()` clock is what makes
`input_latency_ns = consume - publish` a same-clock subtraction; flag
semantics live in the [CLI reference](#cli-flags). The diagram is
committed at `docs/architecture.png`.

## Measured results

### The pacer, visualized

![Histogram of 144 Hz frame start-to-start intervals, log count axis: the shipping timer-plus-spin pacer is a single spike at the 6.944 ms period, a bare high-res timer smears slightly around it, and naive sleep_for never lands near the period at all](docs/plots/frametime-hist-144.png)

*Start-to-start frame intervals at 144 Hz (9,499 measured intervals per
strategy, log count axis). The shipping `timer_spin` pacer puts ≈90% of
frames in a single 50 µs bin at the 6.944 ms period (the next-largest bin is
far smaller). A bare high-res timer holds the schedule on average but hands
its OS wake jitter straight to the frame times (p99 8.874 ms). Naive
`sleep_for` never delivers a frame near the period: on this machine its ~7 ms
sleep requests wake on the ~15.6 ms scheduler tick, so it alternates ~15 ms
frames with immediate post-miss frames, the two clusters. (That same
mechanism is what produces the classic comb at 15.6 ms multiples on stock
Windows.)*

![Schedule drift over 60 seconds at 144 Hz: absolute-deadline rescheduling stays flat at zero while relative rescheduling falls linearly behind](docs/plots/drift-60s.png)

*Why the pacer schedules `next += period` and never `now + period`: identical
runs (high-res timer, 144 Hz, 8,640 measured frames), only the rescheduling
rule differs. The absolute schedule ends 0.3 ms from ideal after 60 seconds;
the relative one leaks every frame's work time and wake latency into the
schedule permanently, about 0.66 ms per frame, 5.7 s behind after a minute,
while reporting zero missed deadlines the whole way. The relative run's
schedule stretched enough that delivering those 60 seconds' worth of frames
took ~65.7 s of wall time.*

Regenerate both: `python bench/run_plots.py` (~7 minutes of measured runs;
AC power, machine otherwise idle), which chains into `bench/plot_frames.py`,
the only script here that needs `pip install matplotlib`.

### Pacing at 144 Hz

The full matrix (60/144/240 Hz plus uncapped, four strategies, 10,000 frames
per cell, machine notes and provenance) is
[bench/results/2026-07-27-pacing-matrix.md](bench/results/2026-07-27-pacing-matrix.md);
this is the rate the histogram above shows. Frame time is deadline-to-deadline,
so zero-miss cells sit at the period by construction. The wake jitter each
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
seqlock carrying the full payload. Full results (in-app cells, achieved
rates, methodology) are in
[bench/results/2026-07-27-handoff.md](bench/results/2026-07-27-handoff.md).

| backend | publish ns/op | read ns/op | in-app reader retries |
|---|---|---|---|
| mutex (default) | 16.6 | 15.9 | 0 in all cells |
| bitmask | 8.8 | 1.5 | 0 in all cells |
| seqlock | 2.3 | 2.1 | 0 in 5 of 6 cells |

Uncontended per-op cost is amortized throughput from 1 M-iteration batches,
comparable across backends, not single-call latency. Where the contended
sweep says the difference starts to matter (reader p99):

| publishes/s | mutex p99 ns | seqlock p99 ns |
|---|---|---|
| 1,000 (this app's real rate) | 100 | 100 |
| 10,000 | 200 | 100 |
| 100,000 (the crossover) | 300 | 100 |
| 1,000,000 | 1,300 | 200 |
| unthrottled | 2,600 | 600 |

The honest result: an uncontended `std::mutex` round-trip costs ~16.6 ns to
publish and ~15.9 ns to read here. At this app's real rates, 1,000
publishes/s against 144 reads/s, the writer holds the lock ~16,600 ns of
every second, so the expected number of reads that ever meet a held lock is
≈0.002 per second: about one contended read every seven minutes. The measured
zeros agree: five of six app cells logged exactly zero reader retries (the
sixth caught a single writer-preemption event in an overloaded 10 kHz cell).
I built the lock-free backends anyway and measured where that stops being
true: the mutex reader's p99 stays within one 100 ns clock quantum of the
seqlock's until ≈100,000 publishes/s, two orders of magnitude above this
app. Below that, choosing the seqlock is a design statement, not a
performance win.

## Decision log

Each entry: the decision, what else was on the table, the evidence, and what
would change my mind.

### Render on a worker thread, input on main

**Decision.** The main thread owns the window and the event queue only: it
polls keys and publishes a snapshot at ~1 kHz and never touches GL. A
dedicated render thread makes the context current, owns every GL object,
draws, and swaps.

**Alternatives considered.** The classic single-threaded loop (poll, draw,
swap) where the frame cap is also the input sampling clock. The inverse
split (render on main, poll on a worker) is not actually available: GLFW
requires event processing on the main thread, so input is the thing that
cannot move. An event queue shipping every edge across instead of snapshot
publishing.

**Evidence.** The decoupled rates are the payoff: against a 144 Hz consumer
the app publishes at ~968 to 971 Hz achieved, and measured end-to-end input
latency runs p50 ≈0.64 ms against a 6.9 ms frame period, so a frame cap no
longer sets input latency. The cost was paid once, in shutdown ordering:
signal stop → render thread deletes its GL objects and detaches the context
→ join → destroy the window on main.

**What would change my mind.** A windowing layer without the main-thread
event constraint, or a target where second-thread contexts don't exist, like
the Emscripten build, which already collapses to single-threaded because
threaded WebGL through GLFW isn't a thing.

### Absolute deadlines, never relative

**Decision.** The pacer schedules `next += period`, never `now + period`. A
missed deadline is counted and the schedule re-anchored at the current time,
so the debt is dropped, never repaid with a burst of short frames.

**Alternatives considered.** Relative rescheduling (`now + period`), the
default-looking choice and the classic bug. Catch-up policies that repay
schedule debt with short frames. Vsync as the frame clock (rejected at
context creation: `glfwSwapInterval(0)`, the pacer owns the clock).

**Evidence.** The drift figure above is two otherwise-identical runs
differing only in this rule: absolute ends **0.345 ms** from ideal after 60
seconds; relative leaks every frame's work time and wake overshoot into the
schedule permanently, **+0.664 ms per frame, 5.7 s behind after a minute**,
taking 65.7 s of wall time to deliver 60 s of frames, while reporting zero
missed deadlines the whole way, by construction: a schedule restarted from
`now` cannot observe itself being late. `--resched=relative` exists solely
to measure this.

**What would change my mind.** A workload whose frames routinely exceed the
period, but an absolute schedule fails that loudly (counted misses), and
the fix is lowering the target rate, not switching to a policy that hides
the same failure as silent drift.

### An adaptive spin margin, not a constant

**Decision.** The pacer sleeps short of the deadline by a margin estimated
online from the measured overshoot of every sleep (Welford mean + 3σ,
clamped to half the period, 1.5 ms bootstrap until 16 samples), then spins
the remainder on the monotonic clock.

**Alternatives considered.** A hardcoded margin (2 ms was the obvious
candidate). No margin at all, the bare `timer` strategy. Pure spin. A
quantile estimator (P²) instead of mean + 3σ.

**Evidence.** The quantity the margin must cover is machine- and power-plan-
dependent: this machine's naive sleep wakes 2 to 3 ms late, not the full
15.6 ms scheduler tick the classic Windows numbers assume, so a constant
tuned on either machine is wrong on the other, in either direction. The
price and payoff are in the tables above: at 144 Hz the bare timer costs
6.8% CPU but hands its wake jitter straight to frame starts (start-to-start
p99 8.874 ms); `timer_spin` costs 12.0% and puts ≈90% of intervals in a
single 50 µs bin (p99 7.670 ms); pure spin costs 99.4% of a core.

**What would change my mind.** A measured overshoot distribution
heavy-tailed enough that mean + 3σ under-covers (it would show up as rising
missed-deadline counts in the matrix) would argue for a quantile tracker; a
hard-real-time platform with genuinely bounded overshoot would argue for a
small constant.

### Mutex by default, lock-free behind a flag

**Decision.** `std::mutex` around a small POD is the shipping input handoff.
The lock-free backends (`--input=bitmask|seqlock`) exist, are tested, and
are not the default.

**Alternatives considered.** Shipping the seqlock as default, since it wins
every per-op number. The bitmask alone (genuinely lock-free, no torn reads,
but 32 bits cannot carry the `publish_ns` timestamp that end-to-end latency
measurement needs). Triple buffering with an atomic pointer swap.

**Evidence.** The handoff tables above: at this app's rates the contention
the lock-free backends exist to avoid essentially never happens (expected
≈0.002 contended reads/s; measured zero retries in five of six cells), and
the crossover where the seqlock first measurably wins is ≈100,000
publishes/s, two orders of magnitude away. The seqlock also carries a real
cost the mutex does not: a formal data race (see Known limitations).

**What would change my mind.** Publish rates approaching the measured 10⁵/s
crossover, a writer that must never block (an audio callback), or multiple
readers, none of which this app has.

### Hand-rolled mat4, no glm

**Decision.** `src/mat4.h`, header-only, column-major, exactly the four
operations the demo needs (multiply, axis-angle rotate, lookAt, perspective),
is the project's entire math library.

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
or more than a handful of ops. The moment the matrix code stops being
trivially reviewable, it stops paying for itself and glm goes in.

## Known limitations

**The seqlock's data race.** The seqlock reader copies the payload without
synchronization while the writer may be mid-store. Under the C++ memory
model that is formally a data race, undefined behavior. The sequence-number
retry discards every torn copy on real hardware (torn=0 in all 15
contended-sweep rows), the fences around the copy are the standard practical
construction (Boehm, *Can seqlocks get along with programming language
memory models?*), and every shipping engine contains something shaped like
this. It works on every real CPU; it is still bending a rule of the abstract
machine. Naming the rule being bent is the signal; pretending there isn't
one would be the bug. (Also documented at the code: `src/input_state.h`.)

**macOS timer precision is untuned.** The macOS sleep path uses
`mach_wait_until`, which wakes with ordinary-thread scheduling latency; real
precision there also wants `THREAD_TIME_CONSTRAINT_POLICY` on the render
thread, which is left unset. macOS is not a CI platform here, and the
Welford margin absorbs the observed overshoot either way. The numbers in
this README are from Windows; the Linux path is CI-built and smoke-tested;
the macOS path is compiled-only. (Windows has its own footnote: the
pre-1803 fallback uses `timeBeginPeriod(1)`, which raises the timer
interrupt rate for the entire machine, and the code says so.)

**No GPU-side timing.** Every number here is CPU-side: the pacer's monotonic
clock and per-thread CPU time. Instrumented frames deliberately run a fixed
~100 µs synthetic CPU workload so the tables characterize the pacer rather
than GPU or driver variance, which also means the GPU cost of a frame is
never measured. There are no GL timer queries, so driver behavior is visible
only by its side effects: on battery, this machine's Arc driver frame-limits
GL inside SwapBuffers, which is why the benchmark protocol demands AC power.
GPU timestamps would have made that directly observable instead of
inferrable.

## Reference

### CLI flags

Every value-taking flag except `--input` accepts both `--flag=value` and
`--flag value` (`--input` takes only the `=` form). Bad values print an
error to stderr (`bad --<flag> value ...`; for `--input`, `unknown input
backend`) and exit 1, before any window exists.

| flag | values (default) | requires | what it does |
|---|---|---|---|
| `--input` | `mutex`\|`bitmask`\|`seqlock` (`mutex`) | - | input-handoff backend |
| `--fps` | 0..1000 (0 = uncapped) | - | frame cap; the pacer owns the frame clock, not vsync |
| `--pace` | `sleep`\|`timer`\|`timer_spin`\|`spin` (`timer_spin`) | `--fps` | pacing strategy |
| `--resched` | `absolute`\|`relative` (`absolute`) | `--fps` | rescheduling rule; `relative` exists to measure the drift bug |
| `--log` | PATH | `--fps` or `--bench-frames` | per-frame CSV, preallocated, written on exit |
| `--bench-frames` | 501..10000000 | - | self-terminating bench run; prints a `bench:` line |
| `--poll-hz` | 1..10000 (1000) | - | input publish rate (its own paced loop; prints a `handoff:` line) |
| `--capture` | PATH | `--fps`, `--capture-frames` | raw RGB frame dump for the GIF, written on exit; prints a `capture:` line |
| `--capture-frames` | 1..500 | `--capture` | frames to capture (exactly one cube rotation), then exit |

Each platform gets its sharpest timer: `CreateWaitableTimerExW` with
`CREATE_WAITABLE_TIMER_HIGH_RESOLUTION` on Windows,
`clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME)` retried on `EINTR` on
Linux, `mach_wait_until` on macOS. All timestamps come from
`pacer_now_ns()`, one timeline for deadlines, sleeps, publishes, and
consumes.

### Instrumentation format

`--log PATH` writes one CSV of per-frame records:

    frame,frame_start_ns,frame_end_ns,frame_time_ns,sleep_requested_ns,sleep_actual_ns,input_latency_ns,missed

`frame_time_ns` is deadline-to-deadline, the cadence the pacer delivered,
not how long the work took. `input_latency_ns` is consume time minus the
payload's `publish_ns`, same monotonic clock (the bitmask backend logs 0:
32 bits cannot carry a timestamp). The buffer is preallocated and the file
written once, on exit, so no IO or reallocation ever happens inside a timed
frame. Instrumented frames run a fixed synthetic CPU workload so the numbers
characterize the pacer, not GPU or driver variance.

### Reproducing the results

Reproduce the committed results (AC power, machine otherwise idle; on
battery this machine's GPU driver frame-limits GL and the numbers are
garbage):

    python bench/run_matrix.py    # ~19 min: the 13-cell pacing matrix
    python bench/run_handoff.py   # ~10 min: handoff micro-bench + app cells
    python bench/run_plots.py     # ~7 min:  the two README figures
    build/Release/cube.exe --fps=50 --capture <dir>/cube.raw --capture-frames=350
    python bench/make_gif.py <dir>/cube.raw --out docs/cube.gif

Beyond the hand-run desktop protocol above, `bench/ci_bench.py` runs the same
13-cell pacing matrix and handoff micro-bench unattended on windows-latest
and ubuntu-latest via `.github/workflows/bench.yml` (weekly, plus
`workflow_dispatch`), committing `bench/results/ci/<platform>/` from the CI
job itself rather than a hand copy-paste. Those runs are a different
measurement class (a shared virtualized runner with software GL and no
AC/idle control), documented in
[bench/results/2026-07-28-ci-pipeline.md](bench/results/2026-07-28-ci-pipeline.md);
the desktop tables above remain the run of record.

### The browser build and deployment

The live demo is the same three translation units compiled with Emscripten
(`__EMSCRIPTEN__` guards select a single-threaded path: no render thread, no
pacer, `requestAnimationFrame` owns the frame clock, and the page explains
why that boundary exists). Build the wasm cube locally with an activated
emsdk (the lab build stages it from `dist/`):

    python web/build.py --out dist

The page below the cube is the frame pacer lab (`lab/`, a TanStack
Start/React app): backstory, how the pacer works, the tuned-vs-naive
histograms and tables, the input-handoff benchmarks, and a decision log.
It is fully static: `lab/scripts/gen-lab-data.mjs` bakes the committed
bench CSVs/JSONs into the page at build time (FATAL on any missing input),
and `npm run build` prerenders it into `lab/dist/client`:

    cd lab
    npm ci
    npm run gen-data
    npm run build
    python -m http.server 8000 -d dist/client

Deployed automatically to Cloudflare Pages (wrangler Direct Upload of
`lab/dist/client` from `.github/workflows/pages.yml`) on every push to
main, with emsdk pinned to the version recorded there. The original GitHub
Pages URL serves a permanent redirect here.

The lab also carries weekly CI results from windows-latest and
ubuntu-latest (both Mesa llvmpipe software GL), bot-committed under
`bench/results/ci/<platform>/` by `bench.yml`, which then re-dispatches
the deploy so `gen-data` bakes the fresh numbers in. They are honestly
labeled as a different measurement class from the desktop numbers, a
shared virtualized runner with no AC/idle control. The desktop run of
record stays the default view; the CI platforms are a switchable
comparison, not a replacement. Pipeline details:
[bench/results/2026-07-28-ci-pipeline.md](bench/results/2026-07-28-ci-pipeline.md).
