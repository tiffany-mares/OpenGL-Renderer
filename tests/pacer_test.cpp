#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <string_view>

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

    // A frame that is many periods late still counts as ONE miss and resyncs
    // once from now — no burst of make-up deadlines for the accumulated debt.
    d = s.advance(2100);  // next was 1650; we are 4+ periods past it
    expect(d.missed, "multi-period-late frame flagged as one miss");
    expect(d.deadline_ns == 2100, "multi-period-late frame starts immediately");
    expect(s.missed() == 2, "multi-period lateness counts a single miss");
    d = s.advance(2105);
    expect(d.deadline_ns == 2200 && !d.missed, "schedule resumes one period after late reanchor");
}

static void pace_strategy_suite() {
    std::fprintf(stderr, "-- pace strategy parsing\n");
    PaceStrategy s = PaceStrategy::SleepFor;
    expect(parse_pace_strategy("sleep", s) && s == PaceStrategy::SleepFor, "parse sleep");
    expect(parse_pace_strategy("timer", s) && s == PaceStrategy::Timer, "parse timer");
    expect(parse_pace_strategy("timer_spin", s) && s == PaceStrategy::TimerSpin, "parse timer_spin");
    expect(parse_pace_strategy("spin", s) && s == PaceStrategy::Spin, "parse spin");
    const PaceStrategy before = s;
    expect(!parse_pace_strategy("bogus", s) && s == before, "reject unknown, out untouched");
    expect(!parse_pace_strategy("", s), "reject empty");
}

static void relative_schedule_suite() {
    std::fprintf(stderr, "-- relative schedule\n");
    // Relative mode: next = now + period — the classic bug, kept measurable.
    FrameSchedule s(100, 1000, ReschedulePolicy::Relative);
    PaceDecision d = s.advance(1000);
    expect(d.deadline_ns == 1100 && !d.missed, "relative first deadline = now + period");
    // Woken 5 late: absolute would still target 1200; relative slips to 1205.
    d = s.advance(1105);
    expect(d.deadline_ns == 1205 && !d.missed, "relative reschedules from now (drift +5)");
    // Late again: drift ACCUMULATES — 1310 vs the absolute grid's 1300.
    d = s.advance(1210);
    expect(d.deadline_ns == 1310 && !d.missed, "relative drift accumulates (+10)");
    // Grossly late (absolute would flag a miss and resync): relative absorbs
    // it silently — no miss flag, no miss count, deadline = now + period.
    d = s.advance(1500);
    expect(d.deadline_ns == 1600 && !d.missed, "relative absorbs gross lateness, never misses");
    expect(s.missed() == 0, "relative missed() stays 0 by construction");

    // Drift compounds linearly: ten wakes each 7 late end 9*7 ns behind the
    // pure-period ladder (first advance is on time; drift starts at wake 2).
    uint64_t now = 1000;
    FrameSchedule r(100, now, ReschedulePolicy::Relative);
    uint64_t deadline = 0;
    for (int i = 0; i < 10; ++i) {
        deadline = r.advance(now).deadline_ns;
        now = deadline + 7;  // simulate a constant 7 ns late wake
    }
    expect(deadline == 1000 + 10 * 100 + 9 * 7, "ten late wakes drift 7 ns each");
    expect(r.missed() == 0, "no misses across the drifting run");
}

static void resched_default_suite() {
    std::fprintf(stderr, "-- resched default regression\n");
    // The 2-arg ctor and explicit Absolute must be indistinguishable — the
    // absolute path is the 6b/6c baseline of record and must not move.
    FrameSchedule a(100, 1000);
    FrameSchedule b(100, 1000, ReschedulePolicy::Absolute);
    for (uint64_t now : {1000ull, 1105ull, 1203ull, 1450ull, 1455ull, 2100ull}) {
        const PaceDecision da = a.advance(now);
        const PaceDecision db = b.advance(now);
        expect(da.deadline_ns == db.deadline_ns && da.missed == db.missed,
               "explicit Absolute matches defaulted ctor");
    }
    expect(a.missed() == b.missed() && a.missed() == 2, "miss counts match (2 each)");
}

static void resched_parse_suite() {
    std::fprintf(stderr, "-- resched policy parsing\n");
    ReschedulePolicy p = ReschedulePolicy::Absolute;
    expect(parse_resched_policy("relative", p) && p == ReschedulePolicy::Relative, "parse relative");
    expect(parse_resched_policy("absolute", p) && p == ReschedulePolicy::Absolute, "parse absolute");
    p = ReschedulePolicy::Relative;
    expect(!parse_resched_policy("Absolute", p) && p == ReschedulePolicy::Relative,
           "case-sensitive, out untouched on failure");
    expect(!parse_resched_policy("", p), "reject empty");
    expect(!parse_resched_policy("now+period", p), "reject junk");
}

int main() {
    welford_suite();
    schedule_suite();
    pace_strategy_suite();
    relative_schedule_suite();
    resched_default_suite();
    resched_parse_suite();
    if (g_failures) {
        std::fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("all pacer tests passed\n");
    return 0;
}
