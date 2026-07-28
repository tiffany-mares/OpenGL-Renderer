# Phase 6c: Input-Handoff Benchmark — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Price the three input-handoff backends (mutex / bitmask / seqlock) — publish cost, read cost, in-app input-latency p99, seqlock retries/sec — at 1 kHz and 10 kHz poll rates, and measure the publish rate where the mutex stops being sufficient.

**Architecture:** Four surfaces, mirroring 6b's shape (C++ instrumentation, stdlib-Python orchestration, committed summarized results, final task is the experiment): (1) a reader-retry counter on `SeqlockChannel` behind a defaulted base virtual — the only shipping-backend change; (2) the main thread's poll loop paced by `FramePacer` with a `--poll-hz` flag and a parseable `handoff:` exit line; (3) a new headless `handoff_bench` executable measuring batched uncontended per-op costs plus a contended rate sweep that locates the crossover; (4) `bench/run_handoff.py` + `bench/summarize_handoff.py` producing three tables: (A) uncontended cost, (B) the spec's app table, (C) the crossover sweep.

**Tech Stack:** C++20 (existing pacer/input machinery, no new deps), Python 3 stdlib.

## Context

Phases 0–6b are complete (`main` at d45c4a0, CI green). Phase 4 built three interchangeable input backends behind `InputChannel`; 6b built the measurement discipline (bench mode, parseable stdout lines, committed provenance docs). 6c runs the handoff experiment. The expected headline is deliberately anticlimactic and the README must say it plainly: an uncontended `std::mutex` costs ~20 ns, and at 1,000 publishes/s against 144 reads/s there is essentially no contention to remove — "I built the lock-free version, measured it, and the mutex was sufficient at this scale; here is the measured crossover where it stops being sufficient" is a stronger claim than any speedup you could fake.

Two decisions locked in by the controller (user question timed out; recommended options taken — vetoable at plan approval):
1. **Crossover is measured, not argued**: the micro-bench sweeps publish rate across decades (1 k, 10 k, 100 k, 1 M/s, unthrottled) and the crossover is where the mutex reader's p99 diverges from the seqlock's.
2. **Paced polling becomes the app's default**: the poll loop's `sleep_for(1ms)` really wakes at the ~15.6 ms scheduler tick (measured in 6a — the app publishes at ~64 Hz today, not the intended ~1 kHz). A main-thread `FramePacer` at 1 kHz becomes the default; `--poll-hz N` (1..10000) overrides. This fulfills main.cpp's own comment ("the honest pacer is Phase 5") and cuts real input latency ~8×.

Known constraints from prior phases: the bitmask backend cannot carry `publish_ns` (32-bit word, keys only) so its latency cells are **n/a by design, never 0**; benchmark runs need AC power + idle machine (battery collapses GL benches — see 6b's machine notes); `thread_cpu_now_ns()`'s first Windows call burns ~24 ms of calibration (the micro-bench simply never calls it — no CPU column).

Repo: `C:\Users\tiffm\Desktop\OpenGL Renderer\OpenGL-Renderer` (parent folder has a space — always quote paths). Remote `github.com/tiffany-mares/OpenGL-Renderer`; work directly on `main`, commit per task, push at the end, verify CI both legs via the GitHub REST API (`gh` not installed). cmake full path if needed: `"C:\Program Files\CMake\bin\cmake.exe"` (generator VS 16 2019). Python 3.13 as `python`.

**Execution note for Task 5:** the full run is ~10 minutes of measurement (micro-bench < 2 min + six ~70 s app cells) and needs the machine idle on AC — coordinate with the human partner; check `(Get-CimInstance -ClassName BatteryStatus -Namespace root\wmi).PowerOnline` is `True` first.

## Global Constraints

- Backends by their `make_input_channel` names: `mutex`, `bitmask`, `seqlock`. App cells: 3 backends × `--poll-hz` ∈ {1000, 10000}. Sweep rates: {1000, 10000, 100000, 1000000, 0 (=unthrottled)} Hz.
- `--poll-hz N`: both `=` and space forms; valid range 1..10000; default 1000; rejection (exit 1, stderr) BEFORE `glfwInit`, like every other flag.
- The poll loop is paced by `FramePacer(1'000'000'000ull / poll_hz, PaceStrategy::TimerSpin)` constructed on the main thread. **TimerSpin is binding** — do not switch the poll pacer to Spin even for 10 kHz; a rate shortfall is reported honestly via the `handoff:` line (`achieved_hz`, `missed`), never hidden.
- `SeqlockChannel`'s clean-read fast path must not touch the retry counter — increments happen only on the two retry branches. Payload semantics and the documented deliberate racy read are unchanged. Counter is `mutable std::atomic<uint64_t>`, relaxed everywhere; accessor name is `read_retries()` on the `InputChannel` base with `{ return 0; }` default.
- Exact stdout line formats (Python regexes must match character-for-character):
  - `handoff: backend=%s poll_hz=%u publishes=%llu wall_ns=%llu achieved_hz=%.1f missed=%llu retries=%llu\n` (main.cpp, every run, printed after render-thread join)
  - `handoff_clock: pair_ns=%.1f\n`
  - `handoff_cost: backend=%s op=%s iters=%llu reps=%d ns_per_op=%.1f\n`
  - `handoff_sweep: backend=%s target_hz=%llu achieved_hz=%.1f reads=%llu read_p50_ns=%llu read_p99_ns=%llu read_max_ns=%llu write_p99_ns=%llu retries=%llu retries_per_sec=%.1f samples=%llu torn=%llu window_s=%.2f\n`
- Bitmask latency cells render as `n/a` (it cannot carry `publish_ns`); its sweep consistency check is skipped (keys-only payload is unverifiable) and its retries column is 0 by the base default.
- `handoff_bench` is GL/GLFW-free: sources `bench/handoff_bench.cpp` + `src/pacer.cpp`, links `Threads::Threads` (+ `winmm` on WIN32), modeled on the `pacer_smoke` target. Full mode finishes in < 2 minutes; `--quick` (< 5 s) is the ONLY mode ctest/CI runs. The binary exits nonzero if any `torn > 0` or any batched `ns_per_op < 1.0` (elision tripwire).
- Nearest-rank percentiles everywhere, matching `summarize.py`: `sorted[round(q * (n - 1))]`.
- Python: stdlib only; raw outputs under git-ignored `bench/results/raw/`; committed outputs are `bench/results/<date>-handoff.md` + `bench/results/<date>-handoff-summary.csv`.
- C++20, no new dependencies. Build/test: `cmake --build build --config Release`, `ctest --test-dir build -C Release --output-on-failure`.
- Render-thread behavior (TimerSpin pacing, CSV format, bench: line) is untouched by this phase.

## File Structure

- `src/input_state.h` — modify: `read_retries()` base virtual + seqlock counter (only backend change).
- `tests/input_test.cpp` — modify: retry-counter tests; stress-test retry sampling.
- `src/main.cpp` — modify: `--poll-hz` parsing/usage/startup print; `FramePacer` poll pacing; `handoff:` exit line.
- `CMakeLists.txt` — modify: `handoff_bench` target + smoke test; two cube flag-rejection tests.
- `bench/handoff_bench.cpp` — **new**: headless micro-bench (uncontended cost + contended sweep).
- `bench/run_handoff.py` — **new**: runs micro-bench + 6 app cells, invokes summarizer.
- `bench/summarize_handoff.py` — **new**: raw dir → three tables in summary.md + summary.csv.
- `bench/results/` — Task 5 commits `<date>-handoff.md` + `<date>-handoff-summary.csv`.
- `README.md`, `CLAUDE.md` — modify: handoff benchmark section + honesty claim; Phase 6c record.
- `docs/superpowers/plans/2026-07-27-phase-6c-handoff-benchmark.md` — copy of this plan (Task 1).

---

### Task 1: Seqlock retry counter

**Files:**
- Modify: `src/input_state.h`
- Modify: `tests/input_test.cpp`
- Create: `docs/superpowers/plans/2026-07-27-phase-6c-handoff-benchmark.md` (copy of this plan)

**Interfaces:**
- Consumes: existing `InputChannel`, `SeqlockChannel`, `InputSnapshot`, test harness (`expect`, `g_failures`) in `tests/input_test.cpp`.
- Produces (later tasks rely on these exact names): `virtual uint64_t InputChannel::read_retries() const { return 0; }`; `SeqlockChannel::read_retries()` override returning the relaxed load of `mutable std::atomic<uint64_t> retries_{0}`.

- [ ] **Step 1: Write the failing tests**

Add to `tests/input_test.cpp` (following the file's existing `expect(...)` style), a new test function called from `main` alongside the existing ones:

```cpp
static void retry_counter() {
    // Base default: backends without retries report 0 through the interface.
    for (const char* name : {"mutex", "bitmask"}) {
        auto ch = make_input_channel(name);
        InputSnapshot s;
        s.keys = 7u;
        ch->publish(s);
        (void)ch->read();
        expect(ch->read_retries() == 0, "non-seqlock backend reports read_retries 0");
    }

    // Seqlock: zero on construction, and — load-bearing — zero after many
    // uncontended reads: the clean-read fast path must never touch the counter.
    SeqlockChannel ch;
    expect(ch.read_retries() == 0, "fresh seqlock has 0 retries");
    InputSnapshot s;
    s.keys = 42u;
    s.publish_ns = 1000003ull;
    ch.publish(s);
    for (int i = 0; i < 1000; ++i) (void)ch.read();
    expect(ch.read_retries() == 0, "uncontended reads never increment retries");
}
```

And in `seqlock_stress()` (tests/input_test.cpp:68-92), sample the counter: capture `const uint64_t r0 = ch.read_retries();` before the reader loop, assert monotonicity inside the loop every 1000 reads (`expect(ch.read_retries() >= last, "retry counter is monotone"); last = ch.read_retries();`), and after `writer.join()` print (do NOT assert nonzero — a fast machine can legitimately finish 200k publishes with zero observed retries):

```cpp
    std::printf("seqlock stress: %llu reader retries observed\n",
                static_cast<unsigned long long>(ch.read_retries() - r0));
```

- [ ] **Step 2: Run to verify failure**

Run: `cmake --build build --config Release` — Expected: FAIL to compile, `read_retries` is not a member of `InputChannel`/`SeqlockChannel`.

- [ ] **Step 3: Implement**

In `src/input_state.h`, add to the `InputChannel` interface (lines 31-37), after `name()`:

```cpp
    // Reader-side retry count since construction. A retry is a read attempt
    // the backend had to discard and repeat; backends that never retry
    // return 0. Relaxed counter — a diagnostic, not a synchronization edge.
    virtual uint64_t read_retries() const { return 0; }
```

In `SeqlockChannel`, add the member and override, and instrument BOTH retry branches of `read()` (the clean path stays untouched):

```cpp
    InputSnapshot read() const override {
        for (;;) {
            const uint32_t s0 = seq_.load(std::memory_order_acquire);
            if (s0 & 1u) {                            // writer mid-update
                retries_.fetch_add(1, std::memory_order_relaxed);
                continue;
            }
            InputSnapshot s = payload_;               // the racy read
            std::atomic_thread_fence(std::memory_order_acquire);  // copy before recheck
            if (seq_.load(std::memory_order_relaxed) == s0) return s;
            retries_.fetch_add(1, std::memory_order_relaxed);  // torn copy discarded
        }
    }

    uint64_t read_retries() const override {
        return retries_.load(std::memory_order_relaxed);
    }
```

Member (next to `seq_`): `mutable std::atomic<uint64_t> retries_{0};` with a one-line comment: `// reader-written only; incremented only on the retry paths, never on a clean read`. Keep every existing comment (including the racy-read documentation) verbatim.

- [ ] **Step 4: Run tests to verify pass**

Run: `cmake --build build --config Release && ctest --test-dir build -C Release -R input_tests --output-on-failure`
Expected: PASS; stress output includes the informational `seqlock stress: N reader retries observed` line.

- [ ] **Step 5: Copy this plan into the repo and commit**

Copy this plan file to `docs/superpowers/plans/2026-07-27-phase-6c-handoff-benchmark.md`, then:

```powershell
git add src/input_state.h tests/input_test.cpp docs/superpowers/plans/2026-07-27-phase-6c-handoff-benchmark.md
git commit -m "feat: observable seqlock reader retries (read_retries)"
```

---

### Task 2: `--poll-hz`, paced poll loop, `handoff:` exit line

**Files:**
- Modify: `src/main.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `FramePacer(period_ns, PaceStrategy)` / `wait()` / `missed()` and `pacer_now_ns()` from `src/pacer.h` (construct-on-calling-thread — the main thread gets its own instance, independent of the render thread's); `InputChannel::read_retries()` from Task 1.
- Produces: the `handoff:` stdout line format (Global Constraints, verbatim) that Task 4's `HANDOFF_RE` parses; `--poll-hz` flag consumed by Task 4's runner.

- [ ] **Step 1: Write the failing ctest cases**

In `CMakeLists.txt`, after the existing `add_test` entries. `PASS_REGULAR_EXPRESSION` decides pass/fail by output alone (exit code becomes irrelevant); do NOT also set `WILL_FAIL` — the two combined would invert a working rejection into a test failure. `TIMEOUT` guards the red state: before the flag exists, cube may open a window and run indefinitely.

```cmake
# --poll-hz rejection happens before glfwInit, so these run headless (CI-safe).
add_test(NAME cube_rejects_poll_hz_zero COMMAND cube --poll-hz=0)
add_test(NAME cube_rejects_poll_hz_high COMMAND cube --poll-hz=10001)
set_tests_properties(cube_rejects_poll_hz_zero cube_rejects_poll_hz_high PROPERTIES
    PASS_REGULAR_EXPRESSION "bad --poll-hz value"
    TIMEOUT 15)
```

- [ ] **Step 2: Run to verify failure**

Run: `cmake -B build && ctest --test-dir build -C Release -R cube_rejects --output-on-failure`
Expected: FAIL both (either timeout, or exit without the message — depends on current unknown-flag handling; either way the regex does not match).

- [ ] **Step 3: Implement in `src/main.cpp`**

Declare with the other flag variables: `uint32_t poll_hz = 1000;  // Phase 6c: the poll loop is properly paced by default`. Add to the argv loop (same shape as the `--fps` block, lines 28-72):

```cpp
        } else if (arg.rfind("--poll-hz=", 0) == 0 || (arg == "--poll-hz" && i + 1 < argc)) {
            const char* v = (arg == "--poll-hz") ? argv[++i] : argv[i] + 10;
            char* end = nullptr;
            const unsigned long parsed = std::strtoul(v, &end, 10);
            if (end == v || *end != '\0' || parsed < 1 || parsed > 10000) {
                std::fprintf(stderr, "bad --poll-hz value '%s' (want 1..10000)\n", v);
                return EXIT_FAILURE;
            }
            poll_hz = static_cast<uint32_t>(parsed);
```

Add `--poll-hz N` to the usage string (lines 67-70) and a startup print beside the existing ones: `std::printf("poll rate: %u Hz\n", poll_hz);`. No cross-validation with `--fps`/`--log`/`--pace` — those govern the render thread; this governs the main thread.

Replace the poll loop's pacing (line 179, `std::this_thread::sleep_for(std::chrono::milliseconds(1));`). Before the `while` loop (after the initial `glfwGetCursorPos`):

```cpp
    // Phase 6c: pace the poll loop with the platform timer. sleep_for(1ms)
    // really wakes at the ~15.6 ms scheduler tick on stock Windows (measured
    // in 6a) — the app published at ~64 Hz, not the intended ~1 kHz.
    FramePacer poll_pacer(1'000'000'000ull / poll_hz, PaceStrategy::TimerSpin);
    uint64_t publishes = 0;
    const uint64_t poll_start_ns = pacer_now_ns();
```

At the end of the loop body, replacing the sleep: `++publishes;` then `poll_pacer.wait();`. After `render_thread.join()` (so the render thread's `bench:`/frame lines print first and the reader-side retry counter is quiescent), before `glfwDestroyWindow`:

```cpp
    const uint64_t poll_wall_ns = pacer_now_ns() - poll_start_ns;
    std::printf("handoff: backend=%s poll_hz=%u publishes=%llu wall_ns=%llu "
                "achieved_hz=%.1f missed=%llu retries=%llu\n",
                input->name(), poll_hz,
                static_cast<unsigned long long>(publishes),
                static_cast<unsigned long long>(poll_wall_ns),
                poll_wall_ns ? 1e9 * static_cast<double>(publishes) /
                               static_cast<double>(poll_wall_ns) : 0.0,
                static_cast<unsigned long long>(poll_pacer.missed()),
                static_cast<unsigned long long>(input->read_retries()));
```

Include `"pacer.h"` if not already included in main.cpp.

- [ ] **Step 4: Run tests + manual verification**

Run: `cmake --build build --config Release && ctest --test-dir build -C Release --output-on-failure`
Expected: all suites pass including the two new rejection tests (instant, message matched).

Manual (scripted; per CLAUDE.md, wait for the window to exist before sending WM_CLOSE): one run `build/Release/cube.exe --input=mutex --fps=144 --pace=timer_spin --bench-frames=600 --log="$env:TEMP\p6c-t2.csv"` — expected stdout ends with a `handoff:` line where `achieved_hz` is within ~1% of 1000.0 and `retries=0`; one run adding `--poll-hz=10000` — expected `handoff:` line prints `poll_hz=10000` with honest `missed`/`achieved_hz` (a shortfall is acceptable and reported, not hidden); one plain interactive run to confirm normal behavior (window, cube spins, ESC exits, `handoff:` line prints).

- [ ] **Step 5: Commit**

```powershell
git add src/main.cpp CMakeLists.txt
git commit -m "feat: --poll-hz paced input polling and handoff: exit line"
```

---

### Task 3: `handoff_bench` micro-benchmark

**Files:**
- Create: `bench/handoff_bench.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `InputChannel` + `make_input_channel` + `read_retries()` (Task 1), `pacer_now_ns()` (`src/pacer.cpp`).
- Produces: the `handoff_clock:` / `handoff_cost:` / `handoff_sweep:` stdout lines (Global Constraints, verbatim) that Task 4's regexes parse; exe `handoff_bench` with `--quick` mode.

- [ ] **Step 1: Register the target and the failing smoke test**

In `CMakeLists.txt` (pacer_smoke is the model — GL-free):

```cmake
add_executable(handoff_bench bench/handoff_bench.cpp src/pacer.cpp)
target_include_directories(handoff_bench PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(handoff_bench PRIVATE Threads::Threads)
if(WIN32)
  target_link_libraries(handoff_bench PRIVATE winmm)
endif()
add_test(NAME handoff_bench_smoke COMMAND handoff_bench --quick)
```

Run: `cmake -B build` — Expected: FAIL, `bench/handoff_bench.cpp` does not exist. (That is this task's red state.)

- [ ] **Step 2: Write `bench/handoff_bench.cpp`**

```cpp
// Phase 6c: prices the three input-handoff backends. Headless — no GL/GLFW.
//
// Section 1 (uncontended cost): batched means — N ops between two
// pacer_now_ns() calls, so clock overhead amortizes to ~nothing. Median of
// R batches. All ops go through InputChannel& (virtual dispatch is what the
// app pays, and it blocks hoisting).
// Section 2 (contended sweep): a writer thread publishes at a target rate
// (absolute-deadline spin — one mechanism, exact from 1 kHz to unthrottled),
// the reader loops. One read in every kSampleEvery is timed; each sample
// carries one clock-pair (~20-30 ns, printed as handoff_clock:) which is
// identical across backends, so it cancels in the mutex-vs-seqlock
// comparison that defines the crossover. The reader validates payload
// consistency on every read (mutex/seqlock; bitmask is keys-only and
// unverifiable). Exit code is nonzero on any torn read or if a batch mean
// lands below 1 ns/op (elision tripwire).
#include "input_state.h"
#include "pacer.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>  // braced range-fors; GCC 13 rejects without it (Phase 5 lesson)
#include <thread>
#include <utility>
#include <vector>

namespace {

constexpr unsigned kSampleEvery = 32;

struct Params {
    uint64_t cost_warmup = 100'000;
    uint64_t cost_iters = 1'000'000;
    int      cost_reps = 5;
    double   sweep_warmup_s = 0.5;
    double   sweep_window_s = 2.0;
    std::vector<uint64_t> sweep_rates{1'000, 10'000, 100'000, 1'000'000, 0};
};

volatile uint64_t g_sink = 0;  // checksum sink: defeats dead-code elimination

InputSnapshot payload_for(uint64_t i) {
    InputSnapshot s;
    s.keys = static_cast<uint32_t>(i);
    s.mouse_dx = static_cast<float>(i & 0xFFFFu);
    s.mouse_dy = -s.mouse_dx;
    s.publish_ns = i * 1000003ull;
    return s;
}

// Every field re-derives from publish_ns; a torn snapshot fails. The zero
// snapshot (nothing published yet) is consistent by construction.
bool is_consistent(const InputSnapshot& r) {
    if (r.publish_ns % 1000003ull != 0) return false;
    const uint64_t i = r.publish_ns / 1000003ull;
    if (r.keys != static_cast<uint32_t>(i)) return false;
    const float dx = static_cast<float>(i & 0xFFFFu);
    return r.mouse_dx == dx && r.mouse_dy == -dx;
}

uint64_t percentile(const std::vector<uint64_t>& sorted, double q) {
    if (sorted.empty()) return 0;
    return sorted[static_cast<size_t>(
        std::llround(q * static_cast<double>(sorted.size() - 1)))];
}

double median_of(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

double time_publish_batch(InputChannel& ch, const Params& p) {
    std::vector<double> reps;
    for (int r = 0; r < p.cost_reps; ++r) {
        for (uint64_t i = 0; i < p.cost_warmup; ++i) ch.publish(payload_for(i));
        const uint64_t t0 = pacer_now_ns();
        for (uint64_t i = 0; i < p.cost_iters; ++i) ch.publish(payload_for(i));
        const uint64_t t1 = pacer_now_ns();
        reps.push_back(static_cast<double>(t1 - t0) /
                       static_cast<double>(p.cost_iters));
    }
    return median_of(reps);
}

double time_read_batch(const InputChannel& ch, const Params& p) {
    std::vector<double> reps;
    uint64_t sink = 0;
    for (int r = 0; r < p.cost_reps; ++r) {
        for (uint64_t i = 0; i < p.cost_warmup; ++i) sink += ch.read().keys;
        const uint64_t t0 = pacer_now_ns();
        for (uint64_t i = 0; i < p.cost_iters; ++i) {
            const InputSnapshot s = ch.read();
            sink += s.keys + s.publish_ns;
        }
        const uint64_t t1 = pacer_now_ns();
        reps.push_back(static_cast<double>(t1 - t0) /
                       static_cast<double>(p.cost_iters));
    }
    g_sink += sink;
    return median_of(reps);
}

double time_clock_pair(uint64_t iters) {
    uint64_t sink = 0;
    const uint64_t t0 = pacer_now_ns();
    for (uint64_t i = 0; i < iters; ++i) sink += pacer_now_ns();
    const uint64_t t1 = pacer_now_ns();
    g_sink += sink;
    // One timed sample brackets the op with two calls; report the pair.
    return 2.0 * static_cast<double>(t1 - t0) / static_cast<double>(iters);
}

struct SweepCell {
    double   achieved_hz = 0;
    uint64_t reads = 0;
    uint64_t read_p50_ns = 0, read_p99_ns = 0, read_max_ns = 0;
    uint64_t write_p99_ns = 0;
    uint64_t retries = 0;
    double   retries_per_sec = 0;
    uint64_t samples = 0;
    uint64_t torn = 0;
    double   window_s = 0;
};

SweepCell run_sweep_cell(InputChannel& ch, bool validate, uint64_t target_hz,
                         const Params& p) {
    std::atomic<bool> stop{false};
    std::atomic<uint64_t> published{0};
    std::vector<uint64_t> wsamples;
    wsamples.reserve(1'000'000);

    std::thread writer([&] {
        const uint64_t period = target_hz ? 1'000'000'000ull / target_hz : 0;
        uint64_t next = pacer_now_ns() + period;
        uint64_t i = 0;
        while (!stop.load(std::memory_order_relaxed)) {
            ++i;
            if (i % kSampleEvery == 0) {
                const uint64_t a = pacer_now_ns();
                ch.publish(payload_for(i));
                const uint64_t b = pacer_now_ns();
                if (wsamples.size() < wsamples.capacity()) wsamples.push_back(b - a);
            } else {
                ch.publish(payload_for(i));
            }
            published.store(i, std::memory_order_relaxed);
            if (period) {
                // Absolute deadlines: exact at every rate, owns a core (a
                // bench harness may). Late deadlines publish back-to-back to
                // catch up; achieved_hz reports the truth either way.
                while (pacer_now_ns() < next) { }
                next += period;
            }
        }
    });

    // Warmup: run the reader unmeasured while the writer settles.
    const uint64_t warm_end =
        pacer_now_ns() + static_cast<uint64_t>(p.sweep_warmup_s * 1e9);
    while (pacer_now_ns() < warm_end) g_sink += ch.read().keys;

    SweepCell cell;
    std::vector<uint64_t> samples;
    samples.reserve(4'000'000);  // ~32 MB cap; stop sampling when full
    const uint64_t retries0 = ch.read_retries();
    const uint64_t pub0 = published.load(std::memory_order_relaxed);
    const uint64_t w0 = pacer_now_ns();
    const uint64_t w_end = w0 + static_cast<uint64_t>(p.sweep_window_s * 1e9);
    uint64_t sink = 0;
    for (;;) {
        // kSampleEvery-1 untimed reads, then one timed read that also
        // checks the window clock (so the hot loop reads no extra clocks).
        for (unsigned k = 1; k < kSampleEvery; ++k) {
            const InputSnapshot r = ch.read();
            sink += r.keys + r.publish_ns;
            if (validate && !is_consistent(r)) ++cell.torn;
            ++cell.reads;
        }
        const uint64_t a = pacer_now_ns();
        const InputSnapshot r = ch.read();
        const uint64_t b = pacer_now_ns();
        sink += r.keys + r.publish_ns;
        if (validate && !is_consistent(r)) ++cell.torn;
        ++cell.reads;
        if (samples.size() < samples.capacity()) samples.push_back(b - a);
        if (b >= w_end) break;
    }
    const uint64_t w1 = pacer_now_ns();
    const uint64_t pub1 = published.load(std::memory_order_relaxed);
    const uint64_t retries1 = ch.read_retries();
    stop.store(true, std::memory_order_relaxed);
    writer.join();
    g_sink += sink;

    cell.window_s = static_cast<double>(w1 - w0) / 1e9;
    cell.achieved_hz = static_cast<double>(pub1 - pub0) / cell.window_s;
    cell.retries = retries1 - retries0;
    cell.retries_per_sec = static_cast<double>(cell.retries) / cell.window_s;
    cell.samples = samples.size();
    std::sort(samples.begin(), samples.end());
    cell.read_p50_ns = percentile(samples, 0.50);
    cell.read_p99_ns = percentile(samples, 0.99);
    cell.read_max_ns = samples.empty() ? 0 : samples.back();
    std::sort(wsamples.begin(), wsamples.end());
    cell.write_p99_ns = percentile(wsamples, 0.99);
    return cell;
}

}  // namespace

int main(int argc, char** argv) {
    Params p;
    if (argc > 1 && std::strcmp(argv[1], "--quick") == 0) {
        p.cost_warmup = 2'000;
        p.cost_iters = 20'000;
        p.cost_reps = 2;
        p.sweep_warmup_s = 0.05;
        p.sweep_window_s = 0.2;
        p.sweep_rates = {1'000, 0};
    }

    bool failed = false;
    std::printf("handoff_clock: pair_ns=%.1f\n", time_clock_pair(p.cost_iters));

    for (const char* name : {"mutex", "bitmask", "seqlock"}) {
        auto ch = make_input_channel(name);
        const double pub_ns = time_publish_batch(*ch, p);
        const double read_ns = time_read_batch(*ch, p);
        for (const auto& [op, ns] : {std::pair{"publish", pub_ns},
                                     std::pair{"read", read_ns}}) {
            std::printf("handoff_cost: backend=%s op=%s iters=%llu reps=%d "
                        "ns_per_op=%.1f\n",
                        name, op,
                        static_cast<unsigned long long>(p.cost_iters),
                        p.cost_reps, ns);
            if (ns < 1.0) {  // no real publish/read is sub-nanosecond
                std::fprintf(stderr, "FATAL: %s %s ns_per_op=%.3f < 1 — "
                             "compiler elided the op\n", name, op, ns);
                failed = true;
            }
        }
    }

    for (const char* name : {"mutex", "bitmask", "seqlock"}) {
        auto ch = make_input_channel(name);
        const bool validate = std::strcmp(name, "bitmask") != 0;
        for (const uint64_t hz : p.sweep_rates) {
            const SweepCell c = run_sweep_cell(*ch, validate, hz, p);
            std::printf("handoff_sweep: backend=%s target_hz=%llu "
                        "achieved_hz=%.1f reads=%llu read_p50_ns=%llu "
                        "read_p99_ns=%llu read_max_ns=%llu write_p99_ns=%llu "
                        "retries=%llu retries_per_sec=%.1f samples=%llu "
                        "torn=%llu window_s=%.2f\n",
                        name, static_cast<unsigned long long>(hz),
                        c.achieved_hz,
                        static_cast<unsigned long long>(c.reads),
                        static_cast<unsigned long long>(c.read_p50_ns),
                        static_cast<unsigned long long>(c.read_p99_ns),
                        static_cast<unsigned long long>(c.read_max_ns),
                        static_cast<unsigned long long>(c.write_p99_ns),
                        static_cast<unsigned long long>(c.retries),
                        c.retries_per_sec,
                        static_cast<unsigned long long>(c.samples),
                        static_cast<unsigned long long>(c.torn),
                        c.window_s);
            if (c.torn != 0) {
                std::fprintf(stderr, "FATAL: %s at %llu Hz: %llu torn reads\n",
                             name, static_cast<unsigned long long>(hz),
                             static_cast<unsigned long long>(c.torn));
                failed = true;
            }
            if (c.samples == 0 || c.achieved_hz <= 0.0) {
                std::fprintf(stderr, "FATAL: %s at %llu Hz: empty cell\n",
                             name, static_cast<unsigned long long>(hz));
                failed = true;
            }
        }
    }

    std::printf("handoff_bench: %s sink=%llu\n", failed ? "FAILED" : "ok",
                static_cast<unsigned long long>(g_sink));
    return failed ? 1 : 0;
}
```

- [ ] **Step 3: Run the smoke to verify pass**

Run: `cmake -B build && cmake --build build --config Release && ctest --test-dir build -C Release -R handoff_bench_smoke --output-on-failure`
Expected: PASS in < 5 s (2 threads; safe on 2-core CI runners).

- [ ] **Step 4: One full local run**

Run: `build/Release/handoff_bench.exe`
Expected: finishes in < 2 minutes; eyeball sanity — mutex publish/read ~15–40 ns, seqlock read ~5–20 ns, bitmask read ~2–10 ns, `pair_ns` ~15–30, unthrottled `achieved_hz` in the millions, `torn=0` everywhere, `retries_per_sec` growing with rate for seqlock. If any batch mean is < 1 ns the binary exits 1 (elision) — that is a bug to fix, not a number to commit.

- [ ] **Step 5: Commit**

```powershell
git add bench/handoff_bench.cpp CMakeLists.txt
git commit -m "feat: handoff_bench micro-benchmark (uncontended cost + contended sweep)"
```

---

### Task 4: Python harness — runner + summarizer

**Files:**
- Create: `bench/run_handoff.py`
- Create: `bench/summarize_handoff.py`

**Interfaces:**
- Consumes: `handoff_bench` stdout lines (Task 3), `cube.exe` CLI incl. `--poll-hz` and the `handoff:`/`bench:` lines (Task 2), the Phase 6a CSV columns.
- Produces: `python bench/run_handoff.py [--exe PATH] [--bench-exe PATH] [--frames N] [--out DIR]` → raw files + `summary.md`/`summary.csv` in the out dir. Run names: `handoff-{backend}-{hz}`.

- [ ] **Step 1: Write `bench/run_handoff.py`**

```python
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
```

- [ ] **Step 2: Write `bench/summarize_handoff.py`**

```python
#!/usr/bin/env python3
"""Summarize a raw handoff-benchmark directory into summary.md + summary.csv.

Three tables: (A) uncontended per-op cost, (B) the app table -- input-latency
p50/p99 from the frame CSVs (post-warmup rows per each run's bench: line) and
retries/sec from the handoff: lines, (C) the contended sweep. The bitmask
backend cannot carry publish_ns, so its latency cells are n/a by design.
percentile() and BENCH_RE are copied from bench/summarize.py (kept standalone
on purpose -- coupling would put 6b's committed results at risk).
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
    if len(sys.argv) != 2:
        sys.exit("usage: summarize_handoff.py RAW_DIR")
    raw = Path(sys.argv[1])
    bench_txt = (raw / "handoff_bench.txt").read_text()

    mclock = CLOCK_RE.search(bench_txt)
    if not mclock:
        sys.exit("FATAL: no handoff_clock: line in handoff_bench.txt")
    costs = {(m.group(1), m.group(2)): float(m.group(5))
             for m in COST_RE.finditer(bench_txt)}
    sweeps = [m.groups() for m in SWEEP_RE.finditer(bench_txt)]
    if len(costs) != 6 or not sweeps:
        sys.exit("FATAL: incomplete handoff_cost:/handoff_sweep: lines")

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
```

- [ ] **Step 3: Validate the plumbing with a reduced pass**

Run: `python bench/run_handoff.py --frames 600` (from repo root)
Expected: handoff_bench full run (~2 min) then 6 short app cells (~4 s each); summary prints with every cell populated, bitmask latency cells `n/a`, sweep table complete. Then corrupt a COPY of one cell's `.txt` in a scratch dir (delete its `handoff:` line) and run `python bench/summarize_handoff.py <scratch>` — Expected: `FATAL: missing bench:/handoff: line`, nonzero exit.

- [ ] **Step 4: Commit**

```powershell
git add bench/run_handoff.py bench/summarize_handoff.py
git commit -m "feat: handoff benchmark harness (runner + summarizer)"
```

---

### Task 5: The experiment, committed results, docs, push, CI

**Files:**
- Create: `bench/results/<actual-run-date>-handoff.md`
- Create: `bench/results/<actual-run-date>-handoff-summary.csv`
- Modify: `README.md`
- Modify: `CLAUDE.md`

- [ ] **Step 1: Environment gate.** `(Get-CimInstance -ClassName BatteryStatus -Namespace root\wmi).PowerOnline` must be `True`; machine otherwise idle (~10 min); coordinate with the human partner. If `False`: STOP (BLOCKED).

- [ ] **Step 2: Full suite green.** `cmake --build build --config Release && ctest --test-dir build -C Release --output-on-failure` — all suites incl. `handoff_bench_smoke` and the two flag-rejection tests.

- [ ] **Step 3: Full run.** `python bench/run_handoff.py` (~10 min; run in background if the tool timeout requires, and do nothing else meanwhile).

- [ ] **Step 4: Shape check before committing.** (a) app latency p50 ≈ half the poll period, p99 ≈ 0.99× period (≈990 µs @1 kHz, ≈99 µs @10 kHz) for mutex and seqlock — if p99 > 1.5× the achieved period, the publisher didn't hold rate (check `achieved_hz`/`missed`) — investigate before committing; (b) a 1 kHz p99 near ~15.6 ms means the paced poll loop regressed to scheduler-tick behavior — BLOCKED; (c) a material mutex-vs-seqlock p99 difference at the same rate is itself a finding — investigate, don't average away; (d) in-app retries expected ≈ 0 (that IS the headline — present the zero as the result); (e) sweep: torn=0 everywhere, seqlock retries/sec growing with rate, mutex read p99 vs seqlock read p99 diverging somewhere in the swept decades (the crossover) or demonstrably not diverging even unthrottled (also a finding — state it); (f) achieved_hz at 10 kHz app cells reported as-measured, shortfall documented not hidden.

- [ ] **Step 5: Assemble committed results.** Copy `summary.md` → `bench/results/<date>-handoff.md`, prepend a provenance header in the shape of `bench/results/2026-07-27-pacing-matrix.md`: date, built-from commit, build config, CPU/GPU/driver/OS, power state, protocol (cells, frames, warmup, poll rates, sweep rates, sampling every 32, clock-pair figure), machine notes (the 10 kHz achieved rate, sleep/scheduler caveats, no thread pinning). Add a **Crossover** paragraph stating the measured rate where mutex read p99 departs from seqlock's, from Table C. Copy `summary.csv` → `bench/results/<date>-handoff-summary.csv`.

- [ ] **Step 6: Docs.** `README.md`: new "Input-handoff benchmark" section — how to reproduce, the three tables' meaning, that latency p99 measures **sampling cadence, not backend cost** at app rates, and the honesty claim (fill measured numbers): *"An uncontended `std::mutex` round-trip costs ~[measured] ns here. At this app's real rates — 1,000 publishes/s against 144 reads/s — the writer holds the lock ~[publish_ns × 1000] ns of every second, so the expected number of reads that ever meet a held lock is ≈ 144 × [that fraction] ≈ [tiny] per second. I built the lock-free backends anyway and measured where that stops being true: the mutex reader's p99 stays indistinguishable from the seqlock's until ≈ [measured crossover] publishes/s. Below that, choosing the seqlock is a design statement, not a performance win."* Also add `--poll-hz N` to the usage line. `CLAUDE.md`: Phase 6c record — `read_retries()`, `--poll-hz` (1..10000, default 1000, paced by main-thread FramePacer), the `handoff:` line format, `handoff_bench` (+ `--quick` in ctest), the two Python scripts, results location; update the phases-complete sentence.

- [ ] **Step 7: Commit, push, CI.**

```powershell
git add bench/results/<date>-handoff.md bench/results/<date>-handoff-summary.csv README.md CLAUDE.md
git commit -m "docs: input-handoff benchmark results and honesty story"
git push
```

Verify CI green on BOTH legs via `Invoke-RestMethod "https://api.github.com/repos/tiffany-mares/OpenGL-Renderer/actions/runs?per_page=1"` (poll until `status=completed`, `conclusion=success`, `head_sha` matches; check the `/jobs` endpoint shows both legs).

---

## Verification (end-to-end)

1. `ctest --test-dir build -C Release --output-on-failure` — all suites pass, including `input_tests` retry assertions, `handoff_bench_smoke` (< 5 s), and both `cube_rejects_poll_hz_*` cases.
2. `python bench/run_handoff.py --frames 600` completes unattended; every table cell populated; bitmask latency cells render `n/a`.
3. Committed `bench/results/<date>-handoff.md` has provenance + machine notes + three tables + an explicit measured-crossover statement; in-app retries/sec ≈ 0 presented as the finding, not a failure.
4. `cube --poll-hz=0` and `--poll-hz=10001` exit 1 with `bad --poll-hz value` before any window; interactive default run unchanged except the `handoff:` exit line and truly-1 kHz polling.
5. CI green on both legs; one commit per task plus the results commit, all pushed.

## Self-review notes

- Spec coverage: publish cost ✅ (Table A/B), read cost ✅ (A/B), input latency p99 ✅ (B, from existing CSV column), retries/sec ✅ (B in-app via `handoff:` line; C in-bench via counter delta), 1 kHz + 10 kHz ✅ (app cells), README claim ✅ (Task 5 Step 6 with measured numbers), crossover ✅ (sweep + Crossover paragraph).
- CTest gotcha handled: `PASS_REGULAR_EXPRESSION` alone (never combined with `WILL_FAIL`), plus `TIMEOUT 15` so the red state fails instead of hanging.
- Type/name consistency: `read_retries()` (T1) used by main.cpp (T2), handoff_bench (T3); stdout formats in Global Constraints match the printf in T3 and regexes in T4 field-for-field.
