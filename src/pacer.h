#pragma once
#include <cmath>
#include <cstdint>
#include <string_view>

// Phase 5: the frame pacer. The pure logic (margin estimator, absolute-
// deadline schedule) lives in this header where unit tests can reach it
// without sleeping; the platform clock/sleep and the FramePacer that ties
// it all together live in pacer.cpp.

// Online mean/variance (Welford's algorithm). Samples are OS sleep
// overshoots in nanoseconds: the margin is measured, not hardcoded, because
// the constant that is right on this machine is wrong on someone else's
// laptop.
class WelfordEstimator {
public:
    void add(double x) {
        ++n_;
        const double d = x - mean_;
        mean_ += d / static_cast<double>(n_);
        m2_ += d * (x - mean_);
    }

    uint64_t count() const { return n_; }
    double mean() const { return mean_; }
    // Sample variance (n-1 denominator); zero until there are two samples.
    double variance() const {
        return n_ > 1 ? m2_ / static_cast<double>(n_ - 1) : 0.0;
    }
    double stddev() const { return std::sqrt(variance()); }

private:
    uint64_t n_ = 0;
    double mean_ = 0.0;
    double m2_ = 0.0;
};

struct PaceDecision {
    uint64_t deadline_ns;  // when the next frame should start
    bool missed;           // deadline already passed: no sleep, no spin
};

// Absolute-deadline schedule: next += period, never now + period — otherwise
// one slow frame permanently shifts every frame after it. A miss is counted
// and the schedule re-anchored at `now` (the late frame starts immediately;
// the following deadline is one period after the re-anchor). The debt is
// dropped, never chased with a burst of short frames.
class FrameSchedule {
public:
    FrameSchedule(uint64_t period_ns, uint64_t start_ns)
        : period_(period_ns), next_(start_ns) {}

    PaceDecision advance(uint64_t now_ns) {
        next_ += period_;
        if (now_ns >= next_) {
            ++missed_;
            next_ = now_ns;  // resync
            return {now_ns, true};
        }
        return {next_, false};
    }

    uint64_t period_ns() const { return period_; }
    uint64_t missed() const { return missed_; }

private:
    uint64_t period_;
    uint64_t next_;
    uint64_t missed_ = 0;
};

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

// Phase 6b: the pacing-strategy matrix. TimerSpin is the shipping default;
// the others exist to be priced against it in the benchmark.
enum class PaceStrategy {
    SleepFor,   // naive relative sleep_for to the deadline — scheduler-tick quantized
    Timer,      // high-res absolute timer to the deadline, no spin
    TimerSpin,  // sleep short by the Welford margin, spin the rest (default)
    Spin,       // never sleep: most accurate, costs a whole core
};

// Flag-string parser for --pace. On failure returns false and leaves `out`
// untouched.
inline bool parse_pace_strategy(std::string_view name, PaceStrategy& out) {
    if (name == "sleep") { out = PaceStrategy::SleepFor; return true; }
    if (name == "timer") { out = PaceStrategy::Timer; return true; }
    if (name == "timer_spin") { out = PaceStrategy::TimerSpin; return true; }
    if (name == "spin") { out = PaceStrategy::Spin; return true; }
    return false;
}

// CPU time consumed by the CALLING thread, for the benchmark's honesty
// column: it prices what each strategy pays for accuracy. Windows reports
// cycles via QueryThreadCycleTime (calibrated against pacer_now_ns); POSIX
// platforms use CLOCK_THREAD_CPUTIME_ID which reports both kernel and user
// time. GetThreadTimes only samples at scheduler-tick boundaries and reads
// either 0 or a full tick, never the true value. Defined in pacer.cpp.
uint64_t thread_cpu_now_ns();

// Monotonic clock in nanoseconds; per-platform, defined in pacer.cpp. The
// same timeline the platform sleep targets, so absolute sleeps need no
// cross-clock conversion. Phase 6 timestamps come from here too.
uint64_t pacer_now_ns();

// Paces a loop to a fixed period: sleeps short of each absolute deadline by
// a Welford-estimated margin, then spins the rest. Construct on the thread
// that calls wait() (on Windows it owns a waitable-timer handle).
class FramePacer {
public:
    explicit FramePacer(uint64_t period_ns,
                        PaceStrategy strategy = PaceStrategy::TimerSpin);
    ~FramePacer();
    FramePacer(const FramePacer&) = delete;
    FramePacer& operator=(const FramePacer&) = delete;

    // Blocks until the next frame deadline and reports what happened. Call
    // once per frame, after the frame's work (right after swap).
    WaitStats wait();

    uint64_t missed() const { return sched_.missed(); }

private:
    uint64_t margin_ns() const;
    void sleep_until_ns(uint64_t target_ns);

    FrameSchedule sched_;
    WelfordEstimator overshoot_;
    void* os_timer_ = nullptr;  // Windows HANDLE; void* keeps windows.h out of this header
    bool legacy_period_raised_ = false;  // Windows pre-1803 fallback engaged
    PaceStrategy strategy_;
};
