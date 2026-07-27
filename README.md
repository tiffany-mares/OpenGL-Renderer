# OpenGL-Renderer

A C++20 threaded OpenGL renderer whose real subject is three systems problems:
thread-affine graphics contexts, a lock-free input handoff, and precise frame
pacing on general-purpose OSes. The rotating cube is the demo, not the point.

## Build & run

    cmake -B build
    cmake --build build --config Release
    build/Release/cube.exe [--input=mutex|bitmask|seqlock] [--fps N]   # build/cube on Linux
    ctest --test-dir build -C Release --output-on-failure

Arrow keys rotate the cube, SPACE pauses the spin, ESC exits.

## Input handoff backends (`--input=`, default `mutex`)

The main thread polls input at ~1 kHz and publishes a snapshot; the render
thread consumes it once per frame. Three interchangeable backends sit behind
one interface (`src/input_state.h`):

| flag      | mechanism                                                        | carries                              |
|-----------|------------------------------------------------------------------|--------------------------------------|
| `mutex`   | `std::mutex` around a small POD — the baseline                   | keys + mouse delta + `publish_ns`    |
| `bitmask` | `std::atomic<uint32_t>`, one bit per key; writer `fetch_or`/`fetch_and` (release), reader one acquire load per frame | keys only |
| `seqlock` | sequence counter; writer never blocks, reader retries on odd/changed sequence | keys + mouse delta + `publish_ns` |

The payload carries a `uint64_t publish_ns` timestamp so end-to-end input
latency (consume time − publish time) can be measured. That field is why the
bitmask alone is not the final answer: it is genuinely lock-free with no torn
reads and no ABA, but 32 bits cannot carry a timestamp.

### The seqlock's data race, named

The seqlock reader copies the payload without synchronization while the writer
may be mid-store. Under the C++ memory model that is formally a data race —
undefined behavior. The sequence-number retry discards every torn copy on real
hardware, the fences around the copy are the standard practical construction
(Boehm, *Can seqlocks get along with programming language memory models?*),
and every shipping engine contains something shaped like this. It works on
every real CPU; it is still bending a rule of the abstract machine. Naming the
rule being bent is the signal — pretending there isn't one would be the bug.

## Frame pacing (`--fps N`, default uncapped)

The pacer sleeps to an **absolute** deadline: `next += period`, never
`now + period`. With relative deadlines, one slow frame permanently shifts
every frame after it; with absolute ones, a hiccup is local. A missed
deadline is counted and the schedule re-anchored at the current time —
the debt is dropped, never repaid with a burst of short frames.

Each platform gets its sharpest timer: `CreateWaitableTimerExW` with
`CREATE_WAITABLE_TIMER_HIGH_RESOLUTION` on Windows (pre-1803 falls back to
the legacy timer plus `timeBeginPeriod(1)` — which raises the timer
interrupt rate for the entire machine, and the code says so),
`clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME)` retried on `EINTR` on
Linux, and `mach_wait_until` on macOS (where real precision also wants
`THREAD_TIME_CONSTRAINT_POLICY`).

### Why the spin loop is not optional

No OS sleep wakes you *at* a time; it wakes you *at or after* it, by a
scheduler-dependent overshoot that ranges from tens of microseconds to
milliseconds depending on machine, power plan, and load. Sleeping all the
way to the deadline therefore hands that jitter directly to the frame
time. So the pacer sleeps *short* of the deadline by a margin and spins
the remainder on the monotonic clock, converting unbounded OS wake jitter
into a bounded busy-wait. The margin is not a constant — a value tuned on
one machine is wrong on another — so it is estimated online from the
measured overshoot of every sleep (Welford's mean and variance,
margin = mean + 3σ, clamped to half the period).
