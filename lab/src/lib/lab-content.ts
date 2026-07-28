export interface Decision {
  title: string;
  reasoning: string;
  changeMind: string;
}

export const BACKSTORY = {
  title: "Backstory",
  paragraphs: [
    `This is the writeup of a C++20 OpenGL renderer whose real subject was never the cube — it was three systems problems: a graphics context that must live on one thread, a lock-free input handoff between two, and hitting a frame deadline on a general-purpose OS. The cube is the demo. The pacer is the point.`,
    `A render loop is simple on paper: do the work, wait for the next deadline, repeat. The "wait" is where the OS gets a say. A request to sleep for a few milliseconds is honored at the scheduler's convenience — on a stock Windows CI runner that means waking on the 15.6 ms timer tick, and even on a desktop with a high-resolution timer request the wake lands 2–3 ms late often enough to eat the frame. The OS is optimized for throughput and battery, not sub-millisecond wake-up precision.`,
    `Everything below is measured, not asserted: 10,000 frames per configuration (first 500 discarded as warmup), per-frame CSV logging with no file IO inside a timed frame, and a synthetic ~100 µs CPU workload per frame so the numbers characterize the pacer rather than the GPU. Three platforms: the desktop run of record (Windows 11, Intel Arc) and two weekly CI runners (windows-latest and ubuntu-latest, both Mesa llvmpipe software GL — honestly a different measurement class, labeled as such).`,
  ],
  context: [
    `Threaded renderer: the main thread owns the window and polls input at ~1 kHz; a render thread owns the GL context and every GL object.`,
    `Four pacing strategies behind one flag (--pace=sleep|timer|timer_spin|spin); timer_spin is the shipping default.`,
    `9,500 measured frames per cell, three refresh targets (60 / 144 / 240 Hz), three platforms.`,
    `The goal is hitting the deadline, not minimizing average frame time — misses are counted and resynced, never chased.`,
  ],
};

export const HOW_IT_WORKS = {
  title: "How it works",
  paragraphs: [
    `The pacer schedules absolute deadlines: next += period, never now + period. When a deadline is missed, the miss is counted and the schedule re-anchored at the current time — the debt is dropped, never repaid with a burst of short frames. (The repo carries a --resched=relative flag purely to measure the classic drift bug: a schedule restarted from "now" leaks every frame's work time into the timeline and cannot observe itself being late.)`,
    `The wait is split in two. First the thread sleeps to the deadline minus a margin, using each platform's sharpest timer — CreateWaitableTimerExW with the high-resolution flag on Windows, clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME) on Linux. Then it spins the remainder on the monotonic clock. The margin is not a constant: it is estimated online from the measured overshoot of every sleep — Welford mean + 3σ, clamped to half the period — because the quantity it must cover is machine- and power-plan-dependent. A constant tuned on one machine is wrong on the next, in either direction.`,
    `Every frame is measured against the same monotonic clock: deadline-to-deadline frame time, requested vs actual sleep, and end-to-end input latency (consume time minus the publish timestamp carried in the input snapshot). The per-run CSV is preallocated and written only at exit — file IO never happens inside a timed frame.`,
  ],
  steps: [
    `Main thread polls keys and publishes an input snapshot at ~1 kHz — it never touches GL.`,
    `Render thread consumes the latest snapshot and submits the frame's GL work.`,
    `Swap buffers (vsync off — glfwSwapInterval(0); the pacer owns the frame clock).`,
    `Sleep to the deadline minus the Welford-estimated margin, then spin the last stretch.`,
    `On a miss: count it, re-anchor the schedule, keep the frame period honest.`,
  ],
  uniqueFeatures: {
    intro:
      "Most OpenGL renderers stop at vsync: they hand the frame cadence to the driver and live with whatever comes back. This project treats frame delivery as a measured systems problem.",
    items: [
      {
        title: "Explicit frame pacer instead of swap interval",
        body: "The render loop owns its own deadline and measures its own interval. VSync is off at context creation; the pacer decides when the next frame starts, and reports every miss instead of hiding it in the driver.",
      },
      {
        title: "An adaptive spin margin, measured per machine",
        body: "The sleep-short margin is a Welford mean + 3σ estimate of this machine's actual wake overshoot, updated every frame. The tables below show what it buys: the bare high-res timer hands its wake jitter straight to frame starts; timer + spin absorbs it for a measured CPU cost.",
      },
      {
        title: "Input decoupled from the frame rate",
        body: "Input is published at ~1 kHz from the main thread through a swappable handoff (mutex, atomic bitmask, or seqlock) and consumed by the render thread — so a frame cap no longer sets input latency. Measured end-to-end: p50 ≈ 0.64 ms against a 6.9 ms frame period.",
      },
      {
        title: "Misses are counted, never chased",
        body: "Absolute deadlines with re-anchoring on a miss. No catch-up bursts, no silent drift — the missed-deadline column in every table is the pacer grading itself.",
      },
      {
        title: "Instrumentation with no observer cost",
        body: "Per-frame records go into a preallocated buffer, flushed only at exit. Render-thread CPU time comes from cycle-accurate counters (QueryThreadCycleTime calibrated against the pacer clock; CLOCK_THREAD_CPUTIME_ID on POSIX), not tick-sampled APIs.",
      },
    ],
  },
};

export const DECISIONS: Decision[] = [
  {
    title: "Render on a worker thread, input on main",
    reasoning:
      "The main thread owns the window and event queue only — it polls keys and publishes a snapshot at ~1 kHz and never touches GL; a dedicated render thread makes the context current and owns every GL object. The decoupled rates are the payoff: ~970 Hz achieved input publishing against a 144 Hz frame consumer, with measured end-to-end input latency of p50 ≈ 0.64 ms against a 6.9 ms frame period. The price was paid once, in shutdown ordering: signal stop → render thread deletes its GL objects and detaches → join → destroy the window on main.",
    changeMind:
      "would change my mind: a windowing layer without the main-thread event constraint, or a target where second-thread contexts don't exist — the browser build already collapses to single-threaded for exactly that reason.",
  },
  {
    title: "Absolute deadlines, never relative",
    reasoning:
      "The pacer schedules next += period, never now + period. Two otherwise-identical 60-second runs differ only in this rule: absolute ends 0.345 ms from ideal; relative leaks every frame's work time and wake overshoot into the schedule permanently — +0.664 ms per frame, 5.7 s behind after a minute — while reporting zero missed deadlines the whole way, because a schedule restarted from now cannot observe itself being late.",
    changeMind:
      "would change my mind: a workload whose frames routinely exceed the period — but an absolute schedule fails that loudly (counted misses), and the fix is lowering the target rate, not a policy that hides the same failure as silent drift.",
  },
  {
    title: "An adaptive spin margin, not a constant",
    reasoning:
      "The margin the sleep must undershoot by is machine- and power-plan-dependent: this desktop's naive sleep wakes 2–3 ms late, while a stock Windows CI runner sleeps in full 15.6 ms scheduler ticks — a constant tuned on either machine is wrong on the other, in either direction. So the pacer estimates it online from every sleep's measured overshoot: Welford mean + 3σ, clamped to half the period, with a 1.5 ms bootstrap until 16 samples.",
    changeMind:
      "would change my mind: an overshoot distribution heavy-tailed enough that mean + 3σ under-covers — it would show up as rising missed-deadline counts — would argue for a quantile tracker; a hard-real-time platform with bounded overshoot would argue for a small constant.",
  },
  {
    title: "Mutex by default, lock-free behind a flag",
    reasoning:
      "A std::mutex around a small POD is the shipping input handoff; the lock-free backends exist, are tested, and are not the default. At this app's real rates — 1,000 publishes/s against 144 reads/s — the expected number of reads that ever meet a held lock is ≈0.002 per second, and the measured zeros agree: five of six app cells logged exactly zero reader retries. The contended sweep puts the crossover where the seqlock first measurably wins at ≈100,000 publishes/s — two orders of magnitude above this app. Below that, choosing the seqlock is a design statement, not a performance win.",
    changeMind:
      "would change my mind: publish rates approaching the measured crossover, a writer that must never block (an audio callback), or multiple readers — none of which this app has.",
  },
  {
    title: "Hand-rolled mat4, no glm",
    reasoning:
      "The project's subject is threads, handoff, and pacing; the matrix code exists to put a cube on screen and to be read. One small column-major header with exactly the four operations the demo needs, a dependency-free test suite including a 16-element perspective reference check, and its two sharp edges documented where they cut (transpose=GL_FALSE, and zNear/zFar because Windows headers #define near and far).",
    changeMind:
      "would change my mind: the first feature needing quaternions, SIMD, or more than a handful of ops — the moment the matrix code stops being trivially reviewable, glm goes in.",
  },
];

export const LIMITATIONS = [
  `The seqlock's payload read is formally a data race — undefined behavior under the C++ memory model. The retry loop discards every torn copy on real hardware (torn=0 in all 15 contended-sweep rows) and the construction is the standard practical one, but it is still bending a rule of the abstract machine — naming that is the point.`,
  `macOS timer precision is untuned: the mach_wait_until path compiles but real precision there wants THREAD_TIME_CONSTRAINT_POLICY, which is left unset. Numbers here are Windows and Linux only; the macOS path is compiled, not measured.`,
  `No GPU-side timing. Every number is CPU-side by design (the ~100 µs synthetic workload characterizes the pacer, not the GPU), so driver behavior is visible only by its side effects — on battery this machine's Arc driver frame-limits GL inside SwapBuffers, which is why every run of record demands AC power.`,
  `The CI platforms run Mesa llvmpipe software rasterization on shared virtualized runners with no AC/idle control — a different measurement class. Compare CI platforms to each other and across weeks, not to the desktop tables.`,
  `The browser build on this page is not the pacer: it is single-threaded and paced by requestAnimationFrame, because neither high-resolution sleep nor a second GL thread exists on a browser main thread.`,
];

export const HANDOFF = {
  title: "Input handoff: mutex vs lock-free",
  intro: [
    `The second system: input crosses from the main thread (publishing at ~1 kHz) to the render thread through one of three interchangeable backends — a std::mutex around a small POD (the default), an atomic key bitmask, and a seqlock carrying the full payload with its publish timestamp. Selected at startup with --input=mutex|bitmask|seqlock.`,
    `This section is rate-independent — its x-axis IS the publish rate, so the Hz filter above doesn't apply here.`,
  ],
  costNote: `Amortized throughput from 1 M-iteration batches — comparable across backends, not "what one isolated call costs."`,
  sweepNote: `Reader p99 vs achieved publish rate. Points sit at each run's achieved rate; the unthrottled cells land where each backend actually reached. Low-rate read tails sit at each machine's clock measurement floor — 100 ns on the Windows machines, ≈30–50 ns on the Ubuntu runner.`,
  sweepCrossover: `≈100 k/s — the crossover`,
  appNote: `In-app end-to-end latency (consume time − publish timestamp), desktop run of record. The bitmask cannot carry the timestamp — 32 bits of keys is all it holds — so its latency is unmeasurable by construction: the Phase-4 asymmetry made visible. App cells are a desktop protocol; CI runs measure only the micro-benchmark.`,
};

export const BUILD_SNIPPET = `git clone https://github.com/tiffany-mares/OpenGL-Renderer
cd OpenGL-Renderer
cmake -B build            # Linux: add -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure

# run it (Windows: build\\Release\\cube.exe, Linux: build/cube)
cube --fps 144                          # paced, timer_spin default
cube --fps 144 --pace=sleep             # feel the naive baseline
cube --fps 144 --bench-frames 10000 --log out.csv
python bench/run_matrix.py              # the full 13-cell matrix`;
