#include <cmath>
#include <cstdio>

#include "pacer.h"

static int g_failures = 0;

static void expect(bool cond, const char* what) {
    if (!cond) {
        std::fprintf(stderr, "FAIL %s\n", what);
        ++g_failures;
    }
}

static void expect_near(double actual, double want, double eps, const char* what) {
    if (std::fabs(actual - want) > eps) {
        std::fprintf(stderr, "FAIL %s: got %.12g want %.12g\n", what, actual, want);
        ++g_failures;
    }
}

static void welford_suite() {
    std::fprintf(stderr, "-- welford\n");
    WelfordEstimator w;
    expect(w.count() == 0, "empty count");
    expect_near(w.variance(), 0.0, 0.0, "empty variance is zero");

    w.add(5.0);
    expect(w.count() == 1, "count after one");
    expect_near(w.mean(), 5.0, 1e-12, "single-sample mean");
    expect_near(w.variance(), 0.0, 0.0, "single-sample variance is zero");

    // Textbook set: mean 5, sample variance 32/7.
    WelfordEstimator w2;
    for (double x : {2.0, 4.0, 4.0, 4.0, 5.0, 5.0, 7.0, 9.0}) w2.add(x);
    expect(w2.count() == 8, "count after eight");
    expect_near(w2.mean(), 5.0, 1e-12, "mean of textbook set");
    expect_near(w2.variance(), 32.0 / 7.0, 1e-9, "sample variance of textbook set");
    expect_near(w2.stddev(), std::sqrt(32.0 / 7.0), 1e-9, "stddev of textbook set");

    // The reason it's Welford and not sum-of-squares: large offsets must not
    // destroy the variance. Samples 1e9+1, 1e9+2, 1e9+3 -> variance exactly 1.
    WelfordEstimator w3;
    for (double x : {1e9 + 1.0, 1e9 + 2.0, 1e9 + 3.0}) w3.add(x);
    expect_near(w3.mean(), 1e9 + 2.0, 1e-3, "large-offset mean");
    expect_near(w3.variance(), 1.0, 1e-6, "large-offset variance stays exact");
}

static void schedule_suite() {
    std::fprintf(stderr, "-- schedule\n");
    // Absolute deadlines: with start 1000 and period 100, deadlines are
    // 1100, 1200, 1300 no matter when advance() is called — never now+period.
    FrameSchedule s(100, 1000);
    expect(s.period_ns() == 100, "period stored");

    PaceDecision d = s.advance(1000);
    expect(d.deadline_ns == 1100 && !d.missed, "first deadline start+period");
    d = s.advance(1105);  // called a touch late, still before 1200
    expect(d.deadline_ns == 1200 && !d.missed, "second deadline absolute, not 1105+100");
    d = s.advance(1203);
    expect(d.deadline_ns == 1300 && !d.missed, "third deadline absolute, not 1203+100");
    expect(s.missed() == 0, "no misses so far");

    // Miss: now is already past the next deadline (1400). Counted once,
    // schedule re-anchored at now — the late frame starts immediately.
    d = s.advance(1450);
    expect(d.missed, "late frame flagged as miss");
    expect(d.deadline_ns == 1450, "missed frame starts immediately (deadline = now)");
    expect(s.missed() == 1, "miss counted");

    // Resync, not catch-up: the following deadline is one full period after
    // the re-anchor — no burst of short frames to repay the debt.
    d = s.advance(1455);
    expect(d.deadline_ns == 1550 && !d.missed, "post-miss deadline = reanchor + period");
    expect(s.missed() == 1, "no extra misses after resync");
}

int main() {
    welford_suite();
    schedule_suite();
    if (g_failures) {
        std::fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("all pacer tests passed\n");
    return 0;
}
