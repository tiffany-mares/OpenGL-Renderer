# Phase 5: The Frame Pacer — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** An `--fps N` flag whose cap actually holds — absolute-deadline pacing with per-platform high-resolution timers, a Welford-estimated sleep margin, a spin finish, and counted-then-resynced misses.

**Architecture:** New `src/pacer.h` holds the pure, unit-testable logic — `WelfordEstimator` (online mean/variance of OS sleep overshoot) and `FrameSchedule` (absolute deadlines: `next += period`, never `now + period`; a miss is counted and re-anchored at `now`, debt dropped, never chased). `src/pacer.cpp` holds the platform layer — `pacer_now_ns()` and `FramePacer::sleep_until_ns()` via `CreateWaitableTimerExW(CREATE_WAITABLE_TIMER_HIGH_RESOLUTION)` (Windows, with pre-1803 legacy-timer + `timeBeginPeriod(1)` fallback on Windows, `clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME)` retried on EINTR (Linux), and `mach_wait_until` (macOS, with a THREAD_TIME_CONSTRAINT_POLICY note). `FramePacer::wait()` sleeps short of each deadline by `mean + 3σ` of measured overshoot, then spins to the deadline. The pacer lives on the render thread, after swap; `fps_cap == 0` (default) preserves today's uncapped loop.

**Tech Stack:** C++20, Win32 waitable timers + winmm (Windows), POSIX `clock_nanosleep` (Linux), `mach_wait_until` (macOS), existing GLFW/ctest setup.

## Context

Phases 0–4 are complete: threaded renderer, three input-handoff backends behind `--input=`. Phase 5 is the third headline system from CLAUDE.md's architecture constraints, which already specify this design verbatim: "sleeps to an **absolute** deadline (`next += period`, never `now + period`), using per-platform high-res timers … Sleeps short of the deadline by a margin estimated online (Welford mean/variance), then spins. Missed deadlines are counted and resynced, never chased with short frames." Done when `--fps 144` holds 144 and the code/README can explain why the spin loop is not optional. Phase 6 (instrumentation/benchmarks) will consume `pacer_now_ns()` and the miss counter.

Repo: `C:\Users\tiffm\Desktop\OpenGL Renderer\OpenGL-Renderer` (parent folder has a space — always quote paths). Remote: `github.com/tiffany-mares/OpenGL-Renderer`. Work directly on `main` (project convention), commit per task, push at the end, watch CI.

## Global Constraints

- C++20; no new third-party dependencies (winmm is a Windows system lib, allowed).
- Windows sleep: `CreateWaitableTimerExW` with `CREATE_WAITABLE_TIMER_HIGH_RESOLUTION`; on failure (pre-1803) fall back to the legacy waitable timer plus `timeBeginPeriod(1)`, with a code note that this raises the tick rate for the entire machine.
- Linux sleep: `clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, …)`, retried on `EINTR`.
- macOS sleep: `mach_wait_until`, with a comment that real precision also needs `THREAD_TIME_CONSTRAINT_POLICY`.
- Absolute deadlines: `next += period`, **never** `now + period` — one slow frame must not shift every frame after it.
- Sleep short of the deadline by a **measured** margin (Welford online mean/variance — no hardcoded constant), then spin.
- Missed deadlines are counted and the schedule resynced — never chased with a burst of short frames.
- `glfwSwapInterval(0)` stays; the pacer owns the frame clock, not vsync. The pacer runs on the render thread only; the thread split and shutdown ordering from Phases 3–4 are untouched.
- `--fps N` and `--fps=N` both accepted (1..1000); absent or 0 = uncapped (current behavior). Mutex/`--input=` flag behavior unchanged.
- Build/test commands (cmake may need full path `"C:\Program Files\CMake\bin\cmake.exe"`; generator VS 16 2019):

```powershell
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

- Windowed-app scripting caveat: wait ~2 s after launch before `CloseMainWindow()` (it silently fails during early startup).

## File Structure

- `src/pacer.h` — **new.** `WelfordEstimator`, `PaceDecision`, `FrameSchedule` (pure logic, fully testable without sleeping), `pacer_now_ns()` declaration, `FramePacer` class declaration (`void* os_timer_` keeps `windows.h` out of the header).
- `src/pacer.cpp` — **new.** Three platform blocks (`_WIN32` / `__APPLE__` / POSIX) defining `pacer_now_ns()`, `FramePacer` ctor/dtor, `sleep_until_ns`; shared `margin_ns()` and `wait()` below them.
- `tests/pacer_test.cpp` — **new.** Deterministic unit tests: Welford closed-form values, numerical stability, absolute-deadline arithmetic, miss/resync semantics. No sleeping — runs in microseconds.
- `tests/pacer_smoke.cpp` — **new.** Real-sleep integration test (50 frames @ 100 Hz, loose bounds) — exercises the platform sleep path on both CI OSes.
- `src/main.cpp` — modify: `--fps` parsing, pass `fps_cap` to the render thread.
- `src/renderer.h` / `src/renderer.cpp` — modify: `uint32_t fps_cap` parameter; pacer after swap; exit stats line (`frames / avg fps / missed`) when capped.
- `CMakeLists.txt` — modify: `pacer_tests`, `pacer_smoke` targets; `src/pacer.cpp` + winmm into `cube`.
- `README.md`, `CLAUDE.md` — modify: "Frame pacing" section (including why the spin is not optional), Phase 5 record.
- `docs/superpowers/plans/2026-07-26-phase-5-frame-pacer.md` — copy of this plan.

---

### Task 1: Pure pacing logic — WelfordEstimator + FrameSchedule

**Files:**
- Create: `src/pacer.h`
- Create: `tests/pacer_test.cpp`
- Modify: `CMakeLists.txt`
- Create: `docs/superpowers/plans/2026-07-26-phase-5-frame-pacer.md` (copy of this plan)

**Interfaces:**
- Produces (later tasks rely on these exact names):
  - `class WelfordEstimator { void add(double); uint64_t count() const; double mean() const; double variance() const; double stddev() const; }`
  - `struct PaceDecision { uint64_t deadline_ns; bool missed; }`
  - `class FrameSchedule { FrameSchedule(uint64_t period_ns, uint64_t start_ns); PaceDecision advance(uint64_t now_ns); uint64_t period_ns() const; uint64_t missed() const; }`
  - Declarations only (defined in Task 2): `uint64_t pacer_now_ns();` and `class FramePacer { explicit FramePacer(uint64_t period_ns); ~FramePacer(); void wait(); uint64_t missed() const; }`

- [ ] **Step 1: Write the failing tests**

Create `tests/pacer_test.cpp`: [test code exactly as in the brief]

- [ ] **Step 2: Add the test target to `CMakeLists.txt` and verify the test fails**

Append after the `input_tests` block: [CMake code exactly as in the brief]

- [ ] **Step 3: Write `src/pacer.h`**

[Header code exactly as in the brief]

- [ ] **Step 4: Run the tests**

Expected: PASS, `all pacer tests passed`. (Linker note: `pacer_tests` compiles only `pacer_test.cpp`, which never calls `pacer_now_ns` or instantiates `FramePacer`, so the missing definitions don't link-error. Task 2 provides them.)

- [ ] **Step 5: Copy this plan into the repo and commit**

Copy this plan file (source: `C:\Users\tiffm\.claude\plans\fluttering-watching-cake.md`) to `docs/superpowers/plans/2026-07-26-phase-5-frame-pacer.md`, then:

```powershell
git add src/pacer.h tests/pacer_test.cpp CMakeLists.txt docs/
git commit -m "feat: pacer core - Welford margin estimator and absolute-deadline schedule"
```

---

### Task 2: Platform sleep layer + FramePacer + real-sleep smoke test

**Files:**
- Create: `src/pacer.cpp`
- Create: `tests/pacer_smoke.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: everything `src/pacer.h` declares (Task 1).
- Produces: working `pacer_now_ns()` and `FramePacer` on Windows/Linux/macOS. Task 3 constructs `FramePacer` on the render thread and calls `wait()`/`missed()`.

---

### Task 3: `--fps` flag wired into the render loop

**Files:**
- Modify: `src/main.cpp`
- Modify: `src/renderer.h`
- Modify: `src/renderer.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `FramePacer(period_ns)`, `wait()`, `missed()` (Task 2).
- Produces: `render_thread_main(GLFWwindow*, const InputChannel&, const FramebufferSize&, uint32_t fps_cap, const std::atomic<bool>& stop, std::atomic<bool>& failed)` — `fps_cap == 0` means uncapped.

---

### Task 4: README pacing docs, CLAUDE.md, push, CI

**Files:**
- Modify: `README.md`
- Modify: `CLAUDE.md`

**Interfaces:** none — documentation and delivery.

---

## Verification (end-to-end)

1. `ctest --test-dir build -C Release --output-on-failure` — mat4, input, pacer unit, and pacer smoke tests all pass.
2. **The done-condition:** `.\build\Release\cube.exe --fps=144` for ~10 s prints `avg fps` within [141, 147] with missed deadlines under ~2% of frames — the cap holds.
3. `--fps=0` and `--fps=abc` exit 1 with `bad --fps value`; usage message updated.
4. The "why the spin loop is not optional" explanation exists in the code comment and README section.
5. CI green on both windows-latest and ubuntu-latest; `git log --oneline` shows one commit per task, all pushed.
