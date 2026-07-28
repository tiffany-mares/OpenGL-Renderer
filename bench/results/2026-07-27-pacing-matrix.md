# Pacing benchmark matrix — 2026-07-27

## Provenance

- **Date:** 2026-07-27 (run 21:06–21:26 EDT; raw dir `bench/results/raw/20260727-210652/`, git-ignored)
- **Binary:** built from commit `45f87ee`, Release, MSVC (Visual Studio 16 2019 generator, MSVC 19.29)
- **Machine:** Intel(R) Core(TM) Ultra 7 155H; Intel(R) Arc(TM) Graphics, GL_VERSION `3.3.0 - Build 32.0.101.8132`; Windows 11 Home; AC power; machine otherwise idle for the full run
- **Protocol:** 10,000 frames per cell, first 500 discarded as warmup (n=9500); 13 cells — `sleep|timer|timer_spin|spin` × 60/144/240 Hz plus an uncapped baseline; each instrumented frame runs a synthetic ~100 µs CPU workload so the tables measure the pacer, not the GPU; `frame_time_ns` is deadline-to-deadline (paced cells) / start-to-start (uncapped)
- **Thread CPU:** render-thread CPU time via `QueryThreadCycleTime`, calibrated once against the pacer's QPC clock (Windows); `CLOCK_THREAD_CPUTIME_ID` on POSIX. The window is warmup boundary → loop exit. An earlier same-day run (raw dir `20260727-201948`, git-ignored) used `GetThreadTimes` and was discarded: that API is scheduler-tick-sampled (every reading a multiple of 15.625 ms), which made the CPU column meaningless for the low-duty strategies.

## Machine notes

Two known deviations of this machine from the generic expected shape, plus one run artifact —
none are defects:

- **(a) `sleep_for` wakes ~2–3 ms late here, not full 15.6 ms scheduler ticks.** Naive sleep
  therefore fails 144 Hz and 240 Hz through missed deadlines (4750 of 9500 — the schedule
  resyncs after each miss, so misses alternate with on-time frames) rather than through a
  grossly inflated p50; deadline-to-deadline frame time re-pins to the period after each
  resync.
- **(b) Pure spin's CPU% lands just below 100 (99.2–99.7 here)** because the render thread
  still blocks briefly in SwapBuffers/driver kernel waits; cycle-based accounting does not
  charge blocked time.
- **Run artifact:** the timer_spin-60 cell caught a single ~214 ms stall (max 214.352 ms,
  3 missed deadlines out of 9500, p50/p95/p99 unaffected) — a one-off OS/driver preemption
  during that 158 s window, visible in the max/stddev columns and left in the data.
- **CPU% only compares equal work.** At 240 Hz, `sleep_for`'s lower CPU% (6.19% vs the
  high-res timer's 9.14%) is not "cheaper at the same job": sleep-240 missed half its
  deadlines and delivered roughly half the target rate (p50 8.29 ms vs the 4.17 ms period),
  so it did less work per wall second. The CPU column only compares equal work between
  cells that held their target rate (zero or near-zero misses); it should not be read as a
  cost ranking across cells that didn't.

Note that paced cells with zero misses show p50=p95=p99=max=period with stddev 0.000 by
construction: deadline-to-deadline frame time deviates from the period only when a deadline
is missed and the schedule resyncs. The OS wake jitter each strategy absorbs is visible in
the raw per-frame CSVs (`sleep_requested_ns` vs `sleep_actual_ns`), not in these tables.

## Results

frame_time_ns is deadline-to-deadline (paced) or start-to-start (uncapped); first-warmup frames discarded per the runs' bench lines. CPU% is render-thread time / wall time over the measured window.

### 60 Hz (period 16.667 ms)

| strategy | p50 ms | p95 ms | p99 ms | max ms | stddev ms | missed | cpu % |
|---|---|---|---|---|---|---|---|
| sleep_for (naive) | 16.667 | 16.667 | 17.291 | 18.227 | 0.100 | 252 | 3.2 |
| high-res timer only | 16.667 | 16.667 | 16.667 | 16.667 | 0.000 | 0 | 3.3 |
| timer + spin (default) | 16.667 | 16.667 | 16.667 | 214.352 | 2.371 | 3 | 7.8 |
| pure spin | 16.667 | 16.667 | 16.667 | 16.667 | 0.000 | 0 | 99.2 |

### 144 Hz (period 6.944 ms)

| strategy | p50 ms | p95 ms | p99 ms | max ms | stddev ms | missed | cpu % |
|---|---|---|---|---|---|---|---|
| sleep_for (naive) | 7.153 | 9.522 | 9.792 | 11.048 | 1.126 | 4750 | 6.3 |
| high-res timer only | 6.944 | 6.944 | 6.944 | 6.944 | 0.000 | 0 | 6.8 |
| timer + spin (default) | 6.944 | 6.944 | 6.944 | 6.944 | 0.000 | 0 | 12.0 |
| pure spin | 6.944 | 6.944 | 6.944 | 6.944 | 0.000 | 0 | 99.4 |

### 240 Hz (period 4.167 ms)

| strategy | p50 ms | p95 ms | p99 ms | max ms | stddev ms | missed | cpu % |
|---|---|---|---|---|---|---|---|
| sleep_for (naive) | 8.287 | 12.313 | 12.562 | 14.885 | 3.886 | 4750 | 6.2 |
| high-res timer only | 4.167 | 4.167 | 4.167 | 4.167 | 0.000 | 0 | 9.1 |
| timer + spin (default) | 4.167 | 4.167 | 4.167 | 4.167 | 0.000 | 0 | 16.1 |
| pure spin | 4.167 | 4.167 | 4.167 | 4.167 | 0.000 | 0 | 99.7 |

### Uncapped baseline (no target)

| strategy | p50 ms | p95 ms | p99 ms | max ms | stddev ms | missed | cpu % |
|---|---|---|---|---|---|---|---|
| uncapped | 0.641 | 0.908 | 1.292 | 2.187 | 0.134 | 0 | 94.0 |
