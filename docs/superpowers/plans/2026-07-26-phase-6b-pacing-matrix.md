# Phase 6b: Pacing Benchmark Matrix — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Five pacing strategies × three targets (60/144/240 Hz), 10,000 frames per cell with the first 500 discarded as warmup, reporting p50/p95/p99/max/stddev/missed/render-thread-CPU% per cell — the CPU column measured with `GetThreadTimes` / `CLOCK_THREAD_CPUTIME_ID`.

**Architecture:** `FramePacer` gains a `PaceStrategy` enum — `SleepFor` (naive relative `sleep_for`, scheduler-tick quantized), `Timer` (high-res absolute sleep to the deadline, no spin), `TimerSpin` (the shipping Phase 5 behavior, default), `Spin` (never sleeps, prices a whole core) — all sharing the existing `FrameSchedule` and `WaitStats`; Uncapped is simply no pacer. A `--bench-frames N` mode makes runs self-terminating (render loop exits after N frames and requests window close), snapshots the render thread's CPU time at the 500-frame warmup boundary, and prints one parseable `bench:` line at exit. Uncapped logging becomes legal (its `frame_time_ns` degrades to frame-start-to-frame-start, documented). Two stdlib-only Python scripts (`bench/run_matrix.py`, `bench/summarize.py`) run the 13 configurations (4 strategies × 3 rates + uncapped once), collect the Phase 6a CSVs plus stdout, and emit a committed markdown matrix + summary CSV; raw per-run CSVs stay git-ignored.

**Tech Stack:** C++20 (existing pacer/log machinery), `GetThreadTimes` (Windows) / `clock_gettime(CLOCK_THREAD_CPUTIME_ID)` (POSIX/macOS), Python 3 stdlib (`csv`, `statistics`, `subprocess`) — Python 3.13.7 is at `C:\Python313\python` (`python` on PATH).

## Context

Phases 0–6a are complete. The instrument exists (`--log` writes per-frame CSVs; a live run already measured sleep overshoot p50 343 µs / p99 1.0 ms and exposed the ~15.6 ms scheduler-tick publisher). 6b runs the experiment the project was built for: pricing pacing strategies against each other. The CPU% column is the honesty column — it is where the project shows the spin loop buys accuracy with CPU, knowingly. Expected shape of the results (from the spec, to sanity-check against): naive sleep quantizes to ~15.6 ms so 144 Hz (6.9 ms budget) is not even approximately achievable; the high-res timer alone lands in the low hundreds of µs of jitter; adding the spin gets inside ~200 µs; pure spin is marginally better and costs a whole core — which is the point of including it.

Repo: `C:\Users\tiffm\Desktop\OpenGL Renderer\OpenGL-Renderer` (parent folder has a space — always quote paths). Remote: `github.com/tiffany-mares/OpenGL-Renderer`. Work directly on `main` (project convention), commit per task, push at the end, verify CI via the GitHub REST API (`gh` not installed).

**Execution note for Task 4:** the full matrix takes ~19 minutes of wall time (60 Hz cells are 167 s each) and the machine must be otherwise idle or the tails are garbage — coordinate with the human partner before starting it, and sanity-check results against the expectations above before committing.

## Global Constraints

- Strategies (CLI names): `sleep` (std::this_thread::sleep_for to the deadline), `timer` (high-res absolute timer only), `timer_spin` (timer + Welford margin + spin — current behavior, default), `spin` (pure spin), plus uncapped (no `--fps`). Targets: 60, 144, 240 Hz.
- 10,000 frames per configuration; first 500 discarded as warmup. Warmup count appears in the `bench:` stdout line so the analyzer never hardcodes it independently.
- Per cell report: p50, p95, p99, max, stddev of `frame_time_ns` (post-warmup), missed deadlines (post-warmup), render thread CPU%.
- Thread CPU time: `GetThreadTimes` on Windows (kernel+user, FILETIME 100 ns ticks), `clock_gettime(CLOCK_THREAD_CPUTIME_ID)` elsewhere (incl. macOS). CPU% is measured from the warmup boundary to loop exit, on the render thread, excluding GL init and the exit-time CSV write.
- `TimerSpin` timing behavior must remain byte-identical to Phase 5/6a (it is the shipping default and the baseline of record); the other strategies are additions, not modifications.
- The Phase 6a CSV format is unchanged (same 8 columns). Uncapped runs may now log: `frame_time_ns` is then frame-start-to-frame-start (no deadlines exist); `missed`/sleep fields are 0. Paced semantics unchanged.
- `--bench-frames N` (both `=` and space forms) must exceed the 500-frame warmup; works with or without `--fps`; the run self-terminates cleanly (normal shutdown ordering — signal, GL teardown on render thread, join, destroy window).
- `--pace NAME` requires `--fps > 0`; `--log` now requires `--fps > 0` OR `--bench-frames > 0`; all rejections before `glfwInit`.
- Python scripts: stdlib only (no pip installs); invoked as `python bench/...`. Raw results in `bench/results/raw/` (git-ignored); committed outputs are the summary markdown + `summary.csv` under `bench/results/`.
- C++20; no new dependencies. Build/test: `cmake --build build --config Release`, `ctest --test-dir build -C Release --output-on-failure` (cmake full path `"C:\Program Files\CMake\bin\cmake.exe"` if not on PATH; generator VS 16 2019).

## File Structure

- `src/pacer.h` — modify: `PaceStrategy` enum, inline `parse_pace_strategy(std::string_view, PaceStrategy&)`, `thread_cpu_now_ns()` declaration, `FramePacer(period, strategy = TimerSpin)` + `strategy_` member.
- `src/pacer.cpp` — modify: `thread_cpu_now_ns()` per platform; `wait()` switches on strategy (TimerSpin branch verbatim-unchanged).
- `tests/pacer_test.cpp` — modify: `parse_pace_strategy` suite.
- `tests/pacer_smoke.cpp` — modify: parameterized over TimerSpin and Spin; CPU-time sanity bounds.
- `src/renderer.h` — modify: `RenderConfig` struct; signature takes it.
- `src/renderer.cpp` — modify: config plumbing, bench mode (auto-exit + CPU snapshot + `bench:` line), uncapped logging.
- `src/main.cpp` — modify: `--pace`, `--bench-frames` parsing; updated `--log` validation; `RenderConfig` construction.
- `bench/run_matrix.py` — **new.** Runs the 13 configurations, saves raw CSV + stdout per run, invokes the summarizer.
- `bench/summarize.py` — **new.** Raw dir → per-rate markdown tables + `summary.csv`.
- `.gitignore` — modify: ignore `bench/results/raw/`.
- `bench/results/` — Task 4 commits `<date>-pacing-matrix.md` + `<date>-summary.csv` here.
- `README.md`, `CLAUDE.md` — modify: benchmark section, Phase 6b record.
- `docs/superpowers/plans/2026-07-26-phase-6b-pacing-matrix.md` — copy of this plan.

---

### Task 1: PaceStrategy + thread CPU clock in the pacer

**Files:**
- Modify: `src/pacer.h`
- Modify: `src/pacer.cpp`
- Modify: `tests/pacer_test.cpp`
- Modify: `tests/pacer_smoke.cpp`
- Create: `docs/superpowers/plans/2026-07-26-phase-6b-pacing-matrix.md` (copy of this plan)

**Interfaces:**
- Consumes: existing `FramePacer`, `FrameSchedule`, `WaitStats`, `pacer_now_ns()`, `sleep_until_ns`, `margin_ns`.
- Produces (later tasks rely on these exact names):
  - `enum class PaceStrategy { SleepFor, Timer, TimerSpin, Spin }`
  - `inline bool parse_pace_strategy(std::string_view name, PaceStrategy& out)` — accepts `"sleep"`, `"timer"`, `"timer_spin"`, `"spin"`; returns false otherwise.
  - `uint64_t thread_cpu_now_ns()` — CPU time (kernel+user) of the calling thread.
  - `FramePacer(uint64_t period_ns, PaceStrategy strategy = PaceStrategy::TimerSpin)` — existing single-arg callers keep compiling.

---

### Task 2: `--pace`, `--bench-frames`, RenderConfig, bench mode

### Task 3: Python harness — runner + summarizer

### Task 4: The full matrix run, committed results, docs, push, CI

## Verification (end-to-end)

1. `ctest --test-dir build -C Release --output-on-failure` — all suites pass, including the parse suite and the two-strategy smoke with CPU bounds.
2. `python bench/run_matrix.py --frames 1000` completes unattended; every cell of the printed summary is populated.
3. The committed `bench/results/<date>-pacing-matrix.md` matches the spec's expected shape: sleep quantized to ~15.6 ms (144 Hz unachievable), timer in the low hundreds of µs of jitter, timer_spin inside ~200 µs, spin marginally better at ~100% CPU, and the CPU%% column tells the honesty story across all four.
4. Guardrails: `--pace` without `--fps`, `--bench-frames=100`, and `--pace=bogus` all exit 1 before any window; interactive runs (no `--bench-frames`) behave exactly as in Phase 6a.
5. CI green on both legs; one commit per task plus the results commit, all pushed.
