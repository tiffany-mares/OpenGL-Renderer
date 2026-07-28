#include <chrono>
#include <cstdio>
#include <thread>

#include "pacer.h"

// Integration smoke for the platform sleep paths and the thread-CPU clock:
// pace 50 frames at 100 Hz per strategy (TimerSpin, Timer, Spin) and check wall time and CPU cost.
// Bounds are deliberately loose — CI runners are noisy. Undersleep is
// impossible by construction (absolute deadlines; TimerSpin/Spin also spin),
// so the lower bound is tight-ish; the upper bound catches "sleep path
// broken". SleepFor is excluded: its scheduler-tick quantization is the
// known-bad baseline the Phase 6b benchmark exists to measure, not a smoke
// invariant. Tight measurement is the benchmark's job.
struct SmokeResult {
    uint64_t elapsed_ns;
    uint64_t cpu_ns;
    uint64_t missed_flags;
    uint64_t missed_counter;
};

static SmokeResult run_pacer(PaceStrategy strategy) {
    constexpr uint64_t kPeriodNs = 10'000'000;  // 100 Hz
    constexpr int kFrames = 50;
    FramePacer pacer(kPeriodNs, strategy);
    const uint64_t c0 = thread_cpu_now_ns();
    const uint64_t t0 = pacer_now_ns();
    uint64_t missed_flags = 0;
    for (int i = 0; i < kFrames; ++i) {
        if (pacer.wait().missed) ++missed_flags;
    }
    return {pacer_now_ns() - t0, thread_cpu_now_ns() - c0, missed_flags,
            pacer.missed()};
}

static int check(const char* name, const SmokeResult& r) {
    constexpr uint64_t kExpectNs = 10'000'000ull * 50;  // 500 ms
    std::printf("%s: elapsed %.1f ms (expected %.0f ms), cpu %.1f ms, missed %llu\n",
                name, r.elapsed_ns / 1e6, kExpectNs / 1e6, r.cpu_ns / 1e6,
                static_cast<unsigned long long>(r.missed_counter));
    if (r.elapsed_ns < kExpectNs * 95 / 100) {
        std::fprintf(stderr, "FAIL %s: undersleep - pacer ran fast\n", name);
        return 1;
    }
    if (r.elapsed_ns > kExpectNs * 2) {
        std::fprintf(stderr, "FAIL %s: gross oversleep - sleep path broken?\n", name);
        return 1;
    }
    if (r.missed_flags != r.missed_counter) {
        std::fprintf(stderr, "FAIL %s: per-wait missed flags (%llu) != missed() (%llu)\n",
                     name, static_cast<unsigned long long>(r.missed_flags),
                     static_cast<unsigned long long>(r.missed_counter));
        return 1;
    }
    return 0;
}

// Regression for the tick-sampling bug in the old GetThreadTimes source
// (Task 4 fix, 2026-07-27): burst 100 us of real CPU work then sleep 2 ms,
// 50 times. True CPU consumed is ~50 * 100us = 5 ms spread across ~50 *
// (0.1 + 2) ms =~ 105 ms of wall time -- almost all of it inside sleeps, with
// only brief 100 us bursts on-CPU between them. GetThreadTimes only samples
// CPU accounting at scheduler-tick boundaries (~15.6 ms on Windows): the
// thread is asleep at nearly every tick, so it typically reads 0; on the
// rare tick where it happens to be caught on-CPU it gets charged a whole
// 15,625,000 ns quantum instead of the ~2 us of actual work it did in that
// tick. Neither outcome is anywhere near the true ~5 ms. The bounds below
// are deliberately asymmetric-tight: 2 ms rules out the "reads ~0" failure
// mode, and 14 ms (just under one 15.625 ms quantum) rules out the "charged
// one lucky full tick" failure mode -- that upper bound is what makes this
// test immune to a lucky-tick false pass. POSIX CLOCK_THREAD_CPUTIME_ID
// passes this natively (true nanosecond-resolution accounting), so CI's
// ubuntu leg is unaffected by this regression.
static int check_burst_visibility() {
    constexpr int kIters = 50;
    constexpr uint64_t kBurstNs = 100'000;  // 100 us of real on-CPU spin per iteration
    const uint64_t c0 = thread_cpu_now_ns();
    for (int i = 0; i < kIters; ++i) {
        const uint64_t burst_start = pacer_now_ns();
        while (pacer_now_ns() - burst_start < kBurstNs) {
            // busy-spin: real CPU time the thread-CPU clock must be able to see
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    const uint64_t cpu_ns = thread_cpu_now_ns() - c0;
    std::printf("burst_visibility: cpu %.3f ms (expect ~5 ms; bounds [2, 14] ms)\n",
                cpu_ns / 1e6);
    if (cpu_ns < 2'000'000ull || cpu_ns > 14'000'000ull) {
        std::fprintf(stderr,
                     "FAIL burst_visibility: cpu_ns=%llu outside [2000000, 14000000]\n",
                     static_cast<unsigned long long>(cpu_ns));
        return 1;
    }
    return 0;
}

int main() {
    int failures = 0;

    failures += check_burst_visibility();

    const SmokeResult ts = run_pacer(PaceStrategy::TimerSpin);
    failures += check("timer_spin", ts);
    // TimerSpin sleeps most of each period: CPU must be well under wall time.
    if (ts.cpu_ns >= ts.elapsed_ns * 8 / 10) {
        std::fprintf(stderr, "FAIL timer_spin: cpu %.1f ms >= 80%% of elapsed - not sleeping?\n",
                     ts.cpu_ns / 1e6);
        ++failures;
    }

    const SmokeResult tm = run_pacer(PaceStrategy::Timer);
    failures += check("timer", tm);
    // Timer sleeps the whole period (no spin): CPU must be well under wall time.
    if (tm.cpu_ns >= tm.elapsed_ns * 8 / 10) {
        std::fprintf(stderr, "FAIL timer: cpu %.1f ms >= 80%% of elapsed - not sleeping?\n",
                     tm.cpu_ns / 1e6);
        ++failures;
    }

    const SmokeResult sp = run_pacer(PaceStrategy::Spin);
    failures += check("spin", sp);
    // Pure spin burns the core: CPU must be a large fraction of wall time.
    // (Also proves thread_cpu_now_ns actually ticks.)
    if (sp.cpu_ns <= sp.elapsed_ns / 2) {
        std::fprintf(stderr, "FAIL spin: cpu %.1f ms <= 50%% of elapsed - CPU clock broken?\n",
                     sp.cpu_ns / 1e6);
        ++failures;
    }

    if (failures) return 1;
    std::printf("pacer smoke passed\n");
    return 0;
}
