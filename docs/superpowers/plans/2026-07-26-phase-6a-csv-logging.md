# Phase 6a: Per-Run CSV Logging — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** `--log PATH` writes one CSV per paced run — preallocated buffer, appended in the loop, flushed on exit, never file IO inside a timed frame — with the exact columns `frame,frame_start_ns,frame_end_ns,frame_time_ns,sleep_requested_ns,sleep_actual_ns,input_latency_ns,missed`.

**Architecture:** Three small pieces. (1) `src/frame_log.h`: `FrameRecord` + `FrameLog` — a `std::vector` reserved once at construction that drops-and-counts when full (no reallocation mid-run) and writes the CSV in one pass after the loop exits. (2) `src/workload.h`: `synthetic_workload(iterations)` — a deterministic xorshift64 spin so instrumented frames carry a fixed CPU cost and the measurements characterize the pacer, not GPU/driver variance. (3) `FramePacer::wait()` now returns a `WaitStats{deadline_ns, sleep_requested_ns, sleep_actual_ns, missed}` so the renderer can log deadline-to-deadline frame time and sleep behavior without reaching into pacer internals. `main.cpp` switches `publish_ns` to `pacer_now_ns()` so publish and consume timestamps share one monotonic timeline — `input_latency_ns = consume − publish` is then a valid subtraction by construction, not by platform accident.

**Tech Stack:** C++20, existing `pacer_now_ns()` timeline, `std::vector` + `cstdio`, ctest.

## Context

Phases 0–5 are complete: threaded renderer, three input backends (`--input=`), frame pacer (`--fps`). The pacer's one-line exit summary just proved its own inadequacy in live testing: an interactive run showed `avg fps: 121.97, missed: 8` — numbers that cannot distinguish "cap doesn't hold" from "eight window-drag stalls in an otherwise perfect run." Phase 6a builds the instrument that can: per-frame records with percentile-ready fields. CLAUDE.md's architecture constraints already bind the design: "per-run CSV with per-frame timing fields; buffer is preallocated and flushed only on exit — never do file IO inside a timed frame. `frame_time_ns` is deadline-to-deadline; `input_latency_ns` = consume time − payload `publish_ns`." Phase 6b (benchmark harness + plots) consumes these CSVs; the `publish_ns` timestamp carried since Phase 4 finally gets used — including the demonstration that the bitmask backend structurally cannot measure latency (its rows log 0).

Repo: `C:\Users\tiffm\Desktop\OpenGL Renderer\OpenGL-Renderer` (parent folder has a space — always quote paths). Remote: `github.com/tiffany-mares/OpenGL-Renderer`. Work directly on `main` (project convention), commit per task, push at the end, verify CI via the GitHub REST API (`gh` is not installed).

## Global Constraints

- CSV header exactly: `frame,frame_start_ns,frame_end_ns,frame_time_ns,sleep_requested_ns,sleep_actual_ns,input_latency_ns,missed` — one row per frame, plain integers.
- Preallocate the buffer, append in the loop, flush on exit. **Never file IO — and never a reallocation — inside the frame being timed.** When the buffer fills, drop and count; do not grow.
- `frame_time_ns` is **deadline to deadline** (this frame's pacer deadline minus the previous frame's), not work duration. First record logs 0 (no previous deadline).
- `input_latency_ns` = frame consume time − the payload's `publish_ns`, on one shared timeline (`pacer_now_ns()`); log 0 when `publish_ns` is 0 (the bitmask backend cannot carry it — that asymmetry is the Phase 4 point, not a bug) or when the subtraction would go negative.
- Fixed synthetic CPU workload per instrumented frame — measure the pacer, not the GPU. Deterministic, optimizer-proof (result consumed), and only run when logging is active so plain interactive runs stay untouched.
- `--log PATH` and `--log=PATH` both accepted; `--log` requires `--fps > 0` (deadline-to-deadline needs deadlines) — reject the combination with a clear message before `glfwInit`.
- No behavior change for runs without `--log`; `--fps` semantics (0..1000, 0 = explicit uncapped) unchanged; thread split, shutdown ordering, `glfwSwapInterval(0)` untouched.
- C++20; no new dependencies.
- Build/test commands (cmake may need full path `"C:\Program Files\CMake\bin\cmake.exe"`; generator VS 16 2019):

```powershell
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

- Windowed-app scripting caveat: wait ~2 s after launch before `CloseMainWindow()`.

## File Structure

- `src/frame_log.h` — **new.** `FrameRecord`, `FrameLog` (reserve-once, drop-when-full, `write_csv`). Header-only, no platform code, fully unit-testable.
- `src/workload.h` — **new.** `synthetic_workload(uint64_t iterations)` xorshift64 spin. Header-only, deterministic, unit-testable.
- `tests/frame_log_test.cpp` — **new.** FrameLog append/capacity/CSV-format tests + workload determinism tests. ctest target `frame_log_tests`.
- `src/pacer.h` / `src/pacer.cpp` — modify: `WaitStats` struct; `wait()` returns it.
- `tests/pacer_smoke.cpp` — modify: consume the returned stats; assert the per-wait `missed` flags sum to `pacer.missed()`.
- `src/main.cpp` — modify: `--log` parsing + validation; `publish_ns` from `pacer_now_ns()`.
- `src/renderer.h` / `src/renderer.cpp` — modify: `const char* log_path` parameter; per-frame instrumentation + workload; CSV flush after the loop.
- `CMakeLists.txt` — modify: `frame_log_tests` target.
- `README.md`, `CLAUDE.md` — modify: instrumentation section; record Phase 6a.
- `docs/superpowers/plans/2026-07-26-phase-6a-csv-logging.md` — copy of this plan.

---

### Task 1: FrameLog + synthetic workload (pure, unit-tested)

**Files:**
- Create: `src/frame_log.h`
- Create: `src/workload.h`
- Create: `tests/frame_log_test.cpp`
- Modify: `CMakeLists.txt`
- Create: `docs/superpowers/plans/2026-07-26-phase-6a-csv-logging.md` (copy of this plan)

**Interfaces:**
- Produces (later tasks rely on these exact names):
  - `struct FrameRecord { uint64_t frame, frame_start_ns, frame_end_ns, frame_time_ns, sleep_requested_ns, sleep_actual_ns, input_latency_ns; uint8_t missed; }`
  - `class FrameLog { explicit FrameLog(size_t capacity); void append(const FrameRecord&); size_t size() const; uint64_t dropped() const; bool write_csv(const char* path) const; }`
  - `uint64_t synthetic_workload(uint64_t iterations)` — returns the xorshift end state; `synthetic_workload(0)` returns the seed `0x9E3779B97F4A7C15ull`.

- [ ] **Step 1: Write the failing tests**

Create `tests/frame_log_test.cpp`:

```cpp
#include <cstdio>
#include <cstring>
#include <string>

#include "frame_log.h"
#include "workload.h"

static int g_failures = 0;

static void expect(bool cond, const char* what) {
    if (!cond) {
        std::fprintf(stderr, "FAIL %s\n", what);
        ++g_failures;
    }
}

static FrameRecord make_record(uint64_t frame) {
    FrameRecord r{};
    r.frame = frame;
    r.frame_start_ns = 1000 + frame;
    r.frame_end_ns = 2000 + frame;
    r.frame_time_ns = 6944444;
    r.sleep_requested_ns = 5000000;
    r.sleep_actual_ns = 5100000;
    r.input_latency_ns = 450000;
    r.missed = frame == 2 ? 1 : 0;
    return r;
}

static void log_suite() {
    std::fprintf(stderr, "-- frame_log\n");
    FrameLog log(2);
    expect(log.size() == 0 && log.dropped() == 0, "empty log");

    log.append(make_record(0));
    log.append(make_record(1));
    expect(log.size() == 2, "two records stored");

    // Full: drop and count, never reallocate mid-run.
    log.append(make_record(2));
    expect(log.size() == 2, "append past capacity drops");
    expect(log.dropped() == 1, "dropped counted");
}

static void csv_suite() {
    std::fprintf(stderr, "-- csv\n");
    const char* path = "frame_log_test_out.csv";
    {
        FrameLog log(4);
        log.append(make_record(0));
        log.append(make_record(1));
        log.append(make_record(2));
        expect(log.write_csv(path), "write_csv succeeds");
    }

    std::FILE* f = std::fopen(path, "r");
    expect(f != nullptr, "csv file exists");
    if (!f) return;
    char line[512];

    expect(std::fgets(line, sizeof line, f) != nullptr, "header line present");
    expect(std::strcmp(line,
        "frame,frame_start_ns,frame_end_ns,frame_time_ns,"
        "sleep_requested_ns,sleep_actual_ns,input_latency_ns,missed\n") == 0,
        "header exact");

    expect(std::fgets(line, sizeof line, f) != nullptr, "row 0 present");
    expect(std::strcmp(line,
        "0,1000,2000,6944444,5000000,5100000,450000,0\n") == 0,
        "row 0 exact");

    expect(std::fgets(line, sizeof line, f) != nullptr, "row 1 present");
    expect(std::fgets(line, sizeof line, f) != nullptr, "row 2 present");
    expect(std::strcmp(line,
        "2,1002,2002,6944444,5000000,5100000,450000,1\n") == 0,
        "row 2 exact (missed flag)");

    expect(std::fgets(line, sizeof line, f) == nullptr, "exactly 3 rows");
    std::fclose(f);
    std::remove(path);

    // Failure path: unopenable target reports false, no crash.
    FrameLog empty(1);
    expect(!empty.write_csv("no_such_dir_xyz/out.csv"), "bad path returns false");
}

static void workload_suite() {
    std::fprintf(stderr, "-- workload\n");
    expect(synthetic_workload(0) == 0x9E3779B97F4A7C15ull, "zero iterations returns seed");
    expect(synthetic_workload(1000) == synthetic_workload(1000), "deterministic");
    expect(synthetic_workload(1000) != synthetic_workload(1001), "iteration-sensitive");
    expect(synthetic_workload(1000) != 0, "nonzero result");
}

int main() {
    log_suite();
    csv_suite();
    workload_suite();
    if (g_failures) {
        std::fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("all frame log tests passed\n");
    return 0;
}
```

- [ ] **Step 2: Add the ctest target and verify the test fails**

Append to `CMakeLists.txt` after the `pacer_smoke` block:

```cmake
add_executable(frame_log_tests tests/frame_log_test.cpp)
target_include_directories(frame_log_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME frame_log_tests COMMAND frame_log_tests)
```

Run:

```powershell
cmake --build build --config Release --target frame_log_tests
```

Expected: **compile error** — `frame_log.h` / `workload.h` not found. Failing state confirmed.

- [ ] **Step 3: Write `src/workload.h`**

```cpp
#pragma once
#include <cstdint>

// Phase 6a: fixed synthetic CPU workload — xorshift64 spun a fixed number of
// iterations. Instrumented runs execute this every frame so frame cost is a
// deterministic CPU quantity and the CSV measures the pacer, not GPU/driver
// variance. The end state is returned and must be consumed by the caller so
// the optimizer cannot delete the loop; it is also what makes the function
// testable (deterministic in, deterministic out).
inline uint64_t synthetic_workload(uint64_t iterations) {
    uint64_t x = 0x9E3779B97F4A7C15ull;  // non-zero seed (golden-ratio constant)
    for (uint64_t i = 0; i < iterations; ++i) {
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
    }
    return x;
}
```

- [ ] **Step 4: Write `src/frame_log.h`**

```cpp
#pragma once
#include <cstdint>
#include <cstdio>
#include <vector>

// Phase 6a: per-run frame log. The buffer is preallocated once and appends
// never reallocate — a mid-run malloc or file write would show up in the very
// frame times this exists to measure. The CSV is written in one pass after
// the loop exits.

struct FrameRecord {
    uint64_t frame;
    uint64_t frame_start_ns;
    uint64_t frame_end_ns;
    uint64_t frame_time_ns;       // deadline to deadline, not work duration
    uint64_t sleep_requested_ns;  // duration handed to the OS sleep (0 = none)
    uint64_t sleep_actual_ns;     // duration the OS sleep actually took (0 = none)
    uint64_t input_latency_ns;    // consume time - payload publish_ns (0 = unknown)
    uint8_t missed;               // 1 if this frame's deadline was missed/resynced
};

class FrameLog {
public:
    explicit FrameLog(size_t capacity) : capacity_(capacity) {
        records_.reserve(capacity);
    }

    // Hot path: no allocation, no IO. Once full, drop and count — growing
    // would reallocate inside a timed frame.
    void append(const FrameRecord& r) {
        if (records_.size() < capacity_) records_.push_back(r);
        else ++dropped_;
    }

    size_t size() const { return records_.size(); }
    uint64_t dropped() const { return dropped_; }

    // Exit-time only. Returns false (with a stderr note) if the file cannot
    // be opened or a write fails.
    bool write_csv(const char* path) const {
        std::FILE* f = std::fopen(path, "w");
        if (!f) {
            std::fprintf(stderr, "frame log: cannot open '%s'\n", path);
            return false;
        }
        std::fprintf(f,
            "frame,frame_start_ns,frame_end_ns,frame_time_ns,"
            "sleep_requested_ns,sleep_actual_ns,input_latency_ns,missed\n");
        for (const FrameRecord& r : records_) {
            std::fprintf(f, "%llu,%llu,%llu,%llu,%llu,%llu,%llu,%u\n",
                         static_cast<unsigned long long>(r.frame),
                         static_cast<unsigned long long>(r.frame_start_ns),
                         static_cast<unsigned long long>(r.frame_end_ns),
                         static_cast<unsigned long long>(r.frame_time_ns),
                         static_cast<unsigned long long>(r.sleep_requested_ns),
                         static_cast<unsigned long long>(r.sleep_actual_ns),
                         static_cast<unsigned long long>(r.input_latency_ns),
                         static_cast<unsigned>(r.missed));
        }
        const bool ok = std::ferror(f) == 0;
        std::fclose(f);
        if (!ok) std::fprintf(stderr, "frame log: write error on '%s'\n", path);
        return ok;
    }

private:
    std::vector<FrameRecord> records_;
    size_t capacity_;
    uint64_t dropped_ = 0;
};
```

- [ ] **Step 5: Run the tests**

```powershell
cmake --build build --config Release --target frame_log_tests
ctest --test-dir build -C Release -R frame_log_tests --output-on-failure
```

Expected: PASS, `all frame log tests passed`.

- [ ] **Step 6: Copy this plan into the repo and commit**

Copy this plan file (source: `C:\Users\tiffm\.claude\plans\fluttering-watching-cake.md`) to `docs/superpowers/plans/2026-07-26-phase-6a-csv-logging.md`, then:

```powershell
git add src/frame_log.h src/workload.h tests/frame_log_test.cpp CMakeLists.txt docs/
git commit -m "feat: preallocated FrameLog with exit-time CSV writer and synthetic workload"
```

---

### Task 2: FramePacer::wait() returns WaitStats

**Files:**
- Modify: `src/pacer.h`
- Modify: `src/pacer.cpp`
- Modify: `tests/pacer_smoke.cpp`

**Interfaces:**
- Consumes: existing `FramePacer`, `PaceDecision`, `pacer_now_ns()` (Phase 5).
- Produces: `struct WaitStats { uint64_t deadline_ns; uint64_t sleep_requested_ns; uint64_t sleep_actual_ns; bool missed; }` and `WaitStats FramePacer::wait()`. Existing callers that ignore the return value still compile.

- [ ] **Step 1: Make the smoke test consume the stats (failing first)**

In `tests/pacer_smoke.cpp`, replace the pacing loop:

```cpp
    for (int i = 0; i < kFrames; ++i) pacer.wait();
```

with:

```cpp
    uint64_t missed_flags = 0;
    for (int i = 0; i < kFrames; ++i) {
        const WaitStats ws = pacer.wait();
        if (ws.missed) ++missed_flags;
    }
```

and after the elapsed-bounds checks, before the final `printf`, add:

```cpp
    if (missed_flags != pacer.missed()) {
        std::fprintf(stderr, "FAIL: per-wait missed flags (%llu) != missed() (%llu)\n",
                     static_cast<unsigned long long>(missed_flags),
                     static_cast<unsigned long long>(pacer.missed()));
        return 1;
    }
```

- [ ] **Step 2: Build to verify it fails**

```powershell
cmake --build build --config Release --target pacer_smoke
```

Expected: **compile error** — `WaitStats` not declared / `wait()` returns void. Failing state confirmed.

- [ ] **Step 3: Add WaitStats to `src/pacer.h`**

Insert after the `FrameSchedule` class (before the `pacer_now_ns` declaration):

```cpp
// What one wait() did — the raw material for the Phase 6 frame log. The
// renderer computes deadline-to-deadline frame time from consecutive
// deadline_ns values; sleep_requested vs sleep_actual is the OS overshoot
// the Welford margin exists to absorb.
struct WaitStats {
    uint64_t deadline_ns;         // the frame boundary this wait targeted (resync target on a miss)
    uint64_t sleep_requested_ns;  // duration handed to the OS sleep (0 = no sleep issued)
    uint64_t sleep_actual_ns;     // duration the OS sleep actually took (0 = no sleep issued)
    bool missed;                  // deadline had already passed: no sleep, no spin
};
```

and change the `wait()` declaration in `FramePacer`:

```cpp
    // Blocks until the next frame deadline and reports what happened. Call
    // once per frame, after the frame's work (right after swap).
    WaitStats wait();
```

- [ ] **Step 4: Update `FramePacer::wait()` in `src/pacer.cpp`**

Replace the whole function with (the spin comment stays verbatim):

```cpp
WaitStats FramePacer::wait() {
    const uint64_t now = pacer_now_ns();
    const PaceDecision d = sched_.advance(now);
    WaitStats stats{d.deadline_ns, 0, 0, d.missed};
    if (d.missed) return stats;  // already late: start the frame immediately, schedule resynced

    const uint64_t margin = margin_ns();
    const uint64_t sleep_target = d.deadline_ns - margin;
    if (sleep_target > now) {
        stats.sleep_requested_ns = sleep_target - now;
        sleep_until_ns(sleep_target);
        const uint64_t wake = pacer_now_ns();
        stats.sleep_actual_ns = wake - now;
        if (wake >= sleep_target)
            overshoot_.add(static_cast<double>(wake - sleep_target));
    }
    // The spin is not optional: the OS wake above lands anywhere inside a
    // scheduler-jitter window (tens of microseconds to milliseconds,
    // machine- and load-dependent). Sleeping all the way to the deadline
    // hands that jitter directly to the frame time; spinning the last
    // measured-margin stretch trades a sliver of CPU for a deadline hit
    // accurate to the clock read.
    while (pacer_now_ns() < d.deadline_ns) {
        // tight clock re-read; each call is itself a brief pause
    }
    return stats;
}
```

- [ ] **Step 5: Run pacer tests and smoke**

```powershell
cmake --build build --config Release --target pacer_smoke
cmake --build build --config Release --target pacer_tests
ctest --test-dir build -C Release -R "pacer" --output-on-failure
```

Expected: both PASS; smoke still reports elapsed ≈ 500 ms and now also validates the missed-flag/counter agreement.

- [ ] **Step 6: Commit**

```powershell
git add src/pacer.h src/pacer.cpp tests/pacer_smoke.cpp
git commit -m "feat: FramePacer::wait reports WaitStats (deadline, sleep requested/actual, missed)"
```

---

### Task 3: `--log` flag + renderer instrumentation

**Files:**
- Modify: `src/main.cpp`
- Modify: `src/renderer.h`
- Modify: `src/renderer.cpp`

**Interfaces:**
- Consumes: `FrameLog`/`FrameRecord` (Task 1), `synthetic_workload` (Task 1), `WaitStats wait()` (Task 2), `pacer_now_ns()` (Phase 5).
- Produces: `render_thread_main(GLFWwindow*, const InputChannel&, const FramebufferSize&, uint32_t fps_cap, const char* log_path, const std::atomic<bool>& stop, std::atomic<bool>& failed)` — `log_path == nullptr` means no logging. Exit line when logging: `frame log: N records -> PATH (D dropped)`.

- [ ] **Step 1: Parse `--log` in `src/main.cpp` and switch `publish_ns` to `pacer_now_ns()`**

Add `#include "pacer.h"` next to the other project includes. Declare alongside the existing option variables:

```cpp
    const char* log_path = nullptr;  // Phase 6a: per-run CSV (requires a paced run)
```

Add a parsing branch after the `--fps` branch, and update the usage message:

```cpp
        } else if (arg.rfind("--log=", 0) == 0 || (arg == "--log" && i + 1 < argc)) {
            log_path = (arg == "--log") ? argv[++i] : argv[i] + 6;
            if (*log_path == '\0') {
                std::fprintf(stderr, "bad --log value: empty path\n");
                return EXIT_FAILURE;
            }
        } else {
            std::fprintf(stderr,
                         "usage: cube [--input=mutex|bitmask|seqlock] [--fps N] [--log PATH]\n");
            return EXIT_FAILURE;
        }
```

After the parse loop (before `make_input_channel`), validate the combination:

```cpp
    if (log_path && fps == 0) {
        std::fprintf(stderr, "--log requires --fps: frame_time_ns is deadline-to-deadline\n");
        return EXIT_FAILURE;
    }
```

After the `fps cap:` printf add:

```cpp
    if (log_path) std::printf("frame log: %s\n", log_path);
```

Replace the `publish_ns` assignment in the poll loop (currently the `std::chrono` expression):

```cpp
        // Same monotonic timeline as the render thread's consume timestamp —
        // input_latency_ns is publish-to-consume on ONE clock, or it is noise.
        s.publish_ns = pacer_now_ns();
```

(`<chrono>` stays included — the 1 ms `sleep_for` still uses it.) Pass `log_path` to the thread:

```cpp
    std::thread render_thread(render_thread_main, window, std::cref(*input),
                              std::cref(fb), fps, log_path, std::cref(stop),
                              std::ref(render_failed));
```

- [ ] **Step 2: Extend `src/renderer.h`**

The declaration becomes (comment block updated):

```cpp
// Body of the render thread. Owns the GL context and every GL object:
// makes the context current, loads GLAD, sets swap interval 0, builds
// shaders/buffers, draws until `stop` is set, then deletes all GL objects
// and detaches the context before returning (shutdown ordering requires
// GL teardown to happen on this thread, before the join).
// fps_cap > 0 paces the loop with FramePacer; 0 leaves it uncapped.
// log_path != nullptr records per-frame timing to a preallocated buffer and
// writes it as CSV on exit (callers guarantee log_path implies fps_cap > 0).
// On init failure: sets `failed`, requests window close, returns early.
void render_thread_main(GLFWwindow* window, const InputChannel& input,
                        const FramebufferSize& fb, uint32_t fps_cap,
                        const char* log_path,
                        const std::atomic<bool>& stop, std::atomic<bool>& failed);
```

- [ ] **Step 3: Instrument `src/renderer.cpp`**

Add includes `"frame_log.h"` and `"workload.h"` next to `"pacer.h"`; update the signature to match Step 2. Add file-statics near the top (after `kIndices`):

```cpp
// Fixed per-frame CPU cost for instrumented runs (~100 µs): the CSV then
// measures the pacer against a constant workload, not GPU/driver variance.
// The volatile sink is what stops the optimizer deleting the call.
static constexpr uint64_t kWorkloadIters = 100'000;
static volatile uint64_t g_workload_sink = 0;
```

Replace the loop region (from the `std::unique_ptr<FramePacer> pacer;` declaration through the closing `}` of the `while` loop) with:

```cpp
    std::unique_ptr<FramePacer> pacer;
    if (fps_cap > 0)
        pacer = std::make_unique<FramePacer>(1'000'000'000ull / fps_cap);
    uint64_t frames = 0;
    const double t_start = glfwGetTime();

    // Phase 6a instrumentation. The log buffer is preallocated here, before
    // the first frame — append never allocates, and the CSV write happens
    // after the loop. Ten minutes of frames is plenty; past that we drop
    // and count rather than reallocate mid-run.
    const bool logging = (log_path != nullptr) && pacer;
    FrameLog log(logging ? static_cast<size_t>(fps_cap) * 600 : 0);
    uint64_t prev_deadline_ns = 0;

    while (!stop.load(std::memory_order_relaxed)) {
        const uint64_t frame_start_ns = logging ? pacer_now_ns() : 0;

        InputSnapshot in = input.read();
        uint64_t input_latency_ns = 0;
        if (logging) {
            // Consume-minus-publish on the shared pacer_now_ns timeline.
            // publish_ns == 0 means the backend cannot carry it (bitmask) —
            // logged as 0, which is the honest answer.
            const uint64_t consume_ns = pacer_now_ns();
            if (in.publish_ns != 0 && consume_ns > in.publish_ns)
                input_latency_ns = consume_ns - in.publish_ns;
        }

        int fb_w = 0, fb_h = 0;
        fb.load(fb_w, fb_h);

        double now = glfwGetTime();
        const double dt = now - prev;
        if (!(in.keys & kKeySpace)) angle += dt * 0.9;
        constexpr double kManualRate = 2.2;  // rad/s while an arrow is held
        yaw   += dt * kManualRate * (((in.keys & kKeyRight) ? 1 : 0) - ((in.keys & kKeyLeft) ? 1 : 0));
        pitch += dt * kManualRate * (((in.keys & kKeyDown) ? 1 : 0) - ((in.keys & kKeyUp) ? 1 : 0));
        prev = now;

        glViewport(0, 0, fb_w, fb_h);
        glClearColor(0.05f, 0.05f, 0.06f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        mat4 model = rotate({1.f, 0.f, 0.f}, static_cast<float>(pitch)) *
                     rotate({0.f, 1.f, 0.f}, static_cast<float>(yaw)) *
                     rotate({0.5f, 1.f, 0.25f}, static_cast<float>(angle));
        mat4 view = lookAt({2.2f, 1.6f, 2.6f}, {0.f, 0.f, 0.f}, {0.f, 1.f, 0.f});
        mat4 proj = perspective(1.0471976f,
                                static_cast<float>(fb_w) / static_cast<float>(fb_h),
                                0.1f, 100.f);
        mat4 mvp = proj * view * model;
        glUseProgram(program);
        glUniformMatrix4fv(mvpLoc, 1, GL_FALSE, mvp.m);  // column-major: no transpose
        glBindVertexArray(vao);
        glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, nullptr);

        if (logging) g_workload_sink = synthetic_workload(kWorkloadIters);

        glfwSwapBuffers(window);  // safe: this thread holds the context

        if (pacer) {
            const WaitStats ws = pacer->wait();
            if (logging) {
                FrameRecord r{};
                r.frame = frames;
                r.frame_start_ns = frame_start_ns;
                r.frame_end_ns = pacer_now_ns();
                // Deadline to deadline — the frame cadence, not the work
                // duration. Frame 0 has no previous deadline: logged as 0.
                r.frame_time_ns = prev_deadline_ns ? ws.deadline_ns - prev_deadline_ns : 0;
                r.sleep_requested_ns = ws.sleep_requested_ns;
                r.sleep_actual_ns = ws.sleep_actual_ns;
                r.input_latency_ns = input_latency_ns;
                r.missed = ws.missed ? 1 : 0;
                log.append(r);
                prev_deadline_ns = ws.deadline_ns;
            }
        }
        ++frames;
    }
```

After the loop, extend the existing `if (pacer)` stats block — after the `frames / avg fps / missed` printf, add:

```cpp
        if (logging) {
            if (log.write_csv(log_path))
                std::printf("frame log: %zu records -> %s (%llu dropped)\n",
                            log.size(), log_path,
                            static_cast<unsigned long long>(log.dropped()));
        }
```

(GL teardown below stays untouched.)

- [ ] **Step 4: Full build and all tests**

```powershell
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Expected: clean build; mat4, input, pacer, pacer_smoke, frame_log tests all pass.

- [ ] **Step 5: Verify a real logged run**

```powershell
$p = Start-Process -PassThru .\build\Release\cube.exe -ArgumentList '--fps=144','--log=run.csv' -RedirectStandardOutput log_check.txt
Start-Sleep -Seconds 10
$p.CloseMainWindow() | Out-Null
$p.WaitForExit(5000)
Get-Content log_check.txt
$rows = Import-Csv run.csv
"rows: $($rows.Count)"
$ft = $rows | Select-Object -Skip 1 | ForEach-Object {[double]$_.frame_time_ns} | Sort-Object
"frame_time p50: $($ft[[int]($ft.Count*0.50)])  p99: $($ft[[int]($ft.Count*0.99)])"
$lat = $rows | ForEach-Object {[double]$_.input_latency_ns} | Sort-Object
"input_latency p50: $($lat[[int]($lat.Count*0.50)])"
"missed rows: $(($rows | Where-Object {$_.missed -eq 1}).Count)"
Remove-Item run.csv, log_check.txt
```

Expected: stdout shows `frame log: run.csv` at startup and `frame log: ~1400 records -> run.csv (0 dropped)` at exit; CSV has ~1400 rows; `frame_time p50` ≈ 6,944,444 ns (the 144 Hz period); `input_latency p50` in the hundreds of thousands of ns (the ~1 kHz publish cadence plus handoff); missed rows near 0 on a quiet machine.

Then the two guardrails:

```powershell
.\build\Release\cube.exe --log=x.csv; "no-fps -> exit $LASTEXITCODE"
.\build\Release\cube.exe --fps=144 --log=; "empty path -> exit $LASTEXITCODE"
```

Expected: both exit 1 (`--log requires --fps…` and `bad --log value: empty path`); no window appears. And a bitmask spot-check:

```powershell
$p = Start-Process -PassThru .\build\Release\cube.exe -ArgumentList '--input=bitmask','--fps=144','--log=bm.csv'
Start-Sleep -Seconds 4
$p.CloseMainWindow() | Out-Null
$p.WaitForExit(5000)
$nonzero = (Import-Csv bm.csv | Where-Object {[double]$_.input_latency_ns -ne 0}).Count
"bitmask nonzero-latency rows: $nonzero"
Remove-Item bm.csv
```

Expected: `bitmask nonzero-latency rows: 0` — the backend that cannot carry `publish_ns` logs latency 0, exactly the Phase 4 asymmetry made visible.

- [ ] **Step 6: Commit**

```powershell
git add src/main.cpp src/renderer.h src/renderer.cpp
git commit -m "feat: --log writes per-frame CSV (deadline-to-deadline timing, input latency, sleep stats)"
```

---

### Task 4: Docs, push, CI

**Files:**
- Modify: `README.md`
- Modify: `CLAUDE.md`

**Interfaces:** none — documentation and delivery.

- [ ] **Step 1: Add the instrumentation section to `README.md`**

Update the Build & run line to:

```
    build/Release/cube.exe [--input=mutex|bitmask|seqlock] [--fps N] [--log PATH]   # build/cube on Linux
```

Append after the "Why the spin loop is not optional" section:

```markdown
## Instrumentation (`--log PATH`, requires `--fps`)

Each paced run can write one CSV of per-frame records:

    frame,frame_start_ns,frame_end_ns,frame_time_ns,sleep_requested_ns,sleep_actual_ns,input_latency_ns,missed

- `frame_time_ns` is **deadline to deadline** — the frame cadence the pacer
  actually delivered, not how long the work took. Frame 0 logs 0.
- `input_latency_ns` is consume time minus the payload's `publish_ns`, both
  on the same monotonic clock. The bitmask backend logs 0 here: 32 bits
  cannot carry a timestamp, which is precisely why it was never the final
  answer among the input backends.
- The log buffer is preallocated before the first frame and the file is
  written once, on exit. No file IO — and no reallocation — ever happens
  inside a frame being timed; if the buffer fills, records are dropped and
  counted instead.
- Instrumented frames run a fixed synthetic CPU workload so the numbers
  characterize the pacer, not GPU or driver variance.

All timestamps come from `pacer_now_ns()` — one timeline for deadlines,
sleeps, publishes, and consumes.
```

- [ ] **Step 2: Update `CLAUDE.md`**

In the Project paragraph: change "**Phases 0–5 are complete as of 2026-07-26.**" to "**Phases 0–5 and 6a are complete as of 2026-07-26.**", and insert before the trailing "Phases 6+ do not exist yet" sentence:

> Phase 6a: per-run CSV logging — `--log PATH` (requires `--fps`; both `--log=` and space forms) records per-frame `frame,frame_start_ns,frame_end_ns,frame_time_ns,sleep_requested_ns,sleep_actual_ns,input_latency_ns,missed` via the preallocated `FrameLog` (`src/frame_log.h`, drop-and-count when full, CSV written only on exit — never IO or reallocation inside a timed frame). `FramePacer::wait()` returns `WaitStats`; `frame_time_ns` is deadline-to-deadline from consecutive `WaitStats::deadline_ns`. `publish_ns` now comes from `pacer_now_ns()` (main.cpp) so `input_latency_ns = consume − publish` is a same-clock subtraction; the bitmask backend logs latency 0 (cannot carry the timestamp — the Phase 4 asymmetry made visible). Instrumented frames run `synthetic_workload` (`src/workload.h`, xorshift64, ~100 µs fixed CPU) so CSVs measure the pacer, not the GPU; tests in `tests/frame_log_test.cpp`.

Then change the trailing sentence to "Phases 6b+ (benchmark harness, plots) do not exist yet — update this file as they land." and update the Build run line to `build/Release/cube.exe [--input=mutex|bitmask|seqlock] [--fps N] [--log PATH]`.

- [ ] **Step 3: Commit and push**

```powershell
git add README.md CLAUDE.md
git commit -m "docs: instrumentation README section; record Phase 6a complete"
git push origin main
```

- [ ] **Step 4: Verify CI green**

`gh` is not installed. Via WebFetch: `https://api.github.com/repos/tiffany-mares/OpenGL-Renderer/actions/runs?per_page=3`, find the run for the pushed commit, poll until complete, then its `jobs` URL — both windows-latest and ubuntu-latest must conclude success (ctest now includes `frame_log_tests`). Remember the Phase 5 lesson: MSVC accepts missing includes that GCC rejects — if the ubuntu leg fails at Build, read the compiler error before judging.

---

## Verification (end-to-end)

1. `ctest --test-dir build -C Release --output-on-failure` — all six test targets pass (mat4, input, pacer, pacer_smoke, frame_log).
2. `cube.exe --fps=144 --log=run.csv` for ~10 s: CSV exists with the exact header, ~1400 rows, `frame_time_ns` p50 ≈ 6,944,444, `input_latency_ns` p50 well under 2 ms, missed ≈ 0 on a quiet machine; stdout reports records written and 0 dropped.
3. `--input=bitmask --fps=144 --log=bm.csv`: every `input_latency_ns` is 0.
4. `--log=x.csv` without `--fps` exits 1; `--log=` (empty) exits 1; runs without `--log` behave exactly as before (no CSV, no workload).
5. CI green on both legs; one commit per task, all pushed.
