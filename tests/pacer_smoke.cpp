#include <cstdio>

#include "pacer.h"

// Integration smoke for the platform sleep path: pace 50 frames at 100 Hz
// and check wall time. Bounds are deliberately loose — CI runners are noisy.
// Undersleep is impossible by construction (absolute deadlines + spin), so
// the lower bound is tight-ish; the upper bound only catches "sleep path
// broken", not "pacing loose". Tight measurement is Phase 6's job.
int main() {
    constexpr uint64_t kPeriodNs = 10'000'000;  // 100 Hz
    constexpr int kFrames = 50;

    FramePacer pacer(kPeriodNs);
    const uint64_t t0 = pacer_now_ns();
    uint64_t missed_flags = 0;
    for (int i = 0; i < kFrames; ++i) {
        const WaitStats ws = pacer.wait();
        if (ws.missed) ++missed_flags;
    }
    const uint64_t elapsed = pacer_now_ns() - t0;

    const uint64_t expect_ns = kPeriodNs * kFrames;  // 500 ms
    std::printf("elapsed %.1f ms (expected %.0f ms), missed %llu\n",
                elapsed / 1e6, expect_ns / 1e6,
                static_cast<unsigned long long>(pacer.missed()));

    if (elapsed < expect_ns * 95 / 100) {
        std::fprintf(stderr, "FAIL: undersleep - pacer ran fast\n");
        return 1;
    }
    if (elapsed > expect_ns * 2) {
        std::fprintf(stderr, "FAIL: gross oversleep - sleep path broken?\n");
        return 1;
    }
    if (missed_flags != pacer.missed()) {
        std::fprintf(stderr, "FAIL: per-wait missed flags (%llu) != missed() (%llu)\n",
                     static_cast<unsigned long long>(missed_flags),
                     static_cast<unsigned long long>(pacer.missed()));
        return 1;
    }
    std::printf("pacer smoke passed\n");
    return 0;
}
