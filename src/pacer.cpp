#include "pacer.h"

// ---------------- platform layer: monotonic clock + timed sleep ----------------

#if defined(_WIN32)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <timeapi.h>

uint64_t pacer_now_ns() {
    static const uint64_t freq = [] {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return static_cast<uint64_t>(f.QuadPart);
    }();
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    const uint64_t ticks = static_cast<uint64_t>(c.QuadPart);
    // Split the conversion so rem * 1e9 can't overflow 64 bits (QPC freq is
    // ~10 MHz, so rem < 1e7 and rem * 1e9 < 2^63).
    const uint64_t sec = ticks / freq;
    const uint64_t rem = ticks % freq;
    return sec * 1'000'000'000ull + rem * 1'000'000'000ull / freq;
}

FramePacer::FramePacer(uint64_t period_ns)
    : sched_(period_ns, pacer_now_ns()) {
    // High-resolution waitable timer (Windows 10 1803+): wakes within tens
    // of microseconds instead of on the scheduler tick.
    HANDLE t = CreateWaitableTimerExW(nullptr, nullptr,
                                      CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                      TIMER_ALL_ACCESS);
    if (!t) {
        // Pre-1803 fallback: the legacy timer wakes on the scheduler tick,
        // so raise the tick to 1 ms. NOTE: timeBeginPeriod(1) raises the
        // timer interrupt rate for the ENTIRE MACHINE — every process pays
        // in power and interrupts until timeEndPeriod in our destructor.
        // That is the documented price of precision on old Windows.
        t = CreateWaitableTimerExW(nullptr, nullptr, 0, TIMER_ALL_ACCESS);
        timeBeginPeriod(1);
        legacy_period_raised_ = true;
    }
    os_timer_ = t;
}

FramePacer::~FramePacer() {
    if (os_timer_) CloseHandle(static_cast<HANDLE>(os_timer_));
    if (legacy_period_raised_) timeEndPeriod(1);
}

void FramePacer::sleep_until_ns(uint64_t target_ns) {
    const uint64_t now = pacer_now_ns();
    if (target_ns <= now) return;
    // Relative wait computed against the same QPC timeline we just read.
    // Negative due time = relative, in 100 ns units.
    LARGE_INTEGER due;
    due.QuadPart = -static_cast<LONGLONG>((target_ns - now) / 100);
    if (due.QuadPart == 0) return;  // sub-100ns remainder: the spin covers it
    if (SetWaitableTimer(static_cast<HANDLE>(os_timer_), &due, 0,
                         nullptr, nullptr, FALSE)) {
        WaitForSingleObject(static_cast<HANDLE>(os_timer_), INFINITE);
    }
}

#elif defined(__APPLE__)

#include <mach/mach_time.h>

static mach_timebase_info_data_t pacer_timebase() {
    static const mach_timebase_info_data_t tb = [] {
        mach_timebase_info_data_t t;
        mach_timebase_info(&t);
        return t;
    }();
    return tb;
}

uint64_t pacer_now_ns() {
    const mach_timebase_info_data_t tb = pacer_timebase();
    const uint64_t t = mach_absolute_time();
    // Split the numer/denom scaling to avoid 64-bit overflow.
    return t / tb.denom * tb.numer + t % tb.denom * tb.numer / tb.denom;
}

FramePacer::FramePacer(uint64_t period_ns)
    : sched_(period_ns, pacer_now_ns()) {}

FramePacer::~FramePacer() = default;

void FramePacer::sleep_until_ns(uint64_t target_ns) {
    // NOTE: mach_wait_until wakes with ordinary-thread scheduling latency;
    // real precision on macOS also needs THREAD_TIME_CONSTRAINT_POLICY on
    // this thread (left untuned — macOS is not a CI platform here, and the
    // Welford margin absorbs the observed overshoot either way).
    const mach_timebase_info_data_t tb = pacer_timebase();
    const uint64_t deadline =
        target_ns / tb.numer * tb.denom + target_ns % tb.numer * tb.denom / tb.numer;
    mach_wait_until(deadline);
}

#else  // Linux and other POSIX

#include <cerrno>
#include <ctime>

uint64_t pacer_now_ns() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1'000'000'000ull +
           static_cast<uint64_t>(ts.tv_nsec);
}

FramePacer::FramePacer(uint64_t period_ns)
    : sched_(period_ns, pacer_now_ns()) {}

FramePacer::~FramePacer() = default;

void FramePacer::sleep_until_ns(uint64_t target_ns) {
    timespec ts;
    ts.tv_sec = static_cast<time_t>(target_ns / 1'000'000'000ull);
    ts.tv_nsec = static_cast<long>(target_ns % 1'000'000'000ull);
    // Absolute sleep on the same CLOCK_MONOTONIC timeline as pacer_now_ns.
    // EINTR means a signal woke us early: just sleep again — the deadline
    // is absolute, so no remaining-time arithmetic is needed.
    while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, nullptr) == EINTR) {
    }
}

#endif

// ---------------- shared pacing logic ----------------

uint64_t FramePacer::margin_ns() const {
    // Until there is enough data, assume a generous 1.5 ms overshoot; after
    // that, mean + 3 sigma covers effectively all wakes. Clamp to half the
    // period so a wild variance estimate can never turn the sleep into a
    // full-period spin.
    const uint64_t cap = sched_.period_ns() / 2;
    if (overshoot_.count() < 16) return cap < 1'500'000 ? cap : 1'500'000;
    double m = overshoot_.mean() + 3.0 * overshoot_.stddev();
    if (m < 0.0) m = 0.0;
    const uint64_t ns = static_cast<uint64_t>(m);
    return ns < cap ? ns : cap;
}

WaitStats FramePacer::wait() {
    const uint64_t now = pacer_now_ns();
    const PaceDecision d = sched_.advance(now);
    WaitStats stats{d.deadline_ns, 0, 0, d.missed};
    if (d.missed) return stats;  // already late: start the frame immediately, schedule resynced

    const uint64_t margin = margin_ns();
    const uint64_t sleep_target = d.deadline_ns - margin;
    if (sleep_target > now) {
        stats.sleep_requested_ns = sleep_target - now;
        sleep_until_ns(sleep_target);
        const uint64_t wake = pacer_now_ns();
        stats.sleep_actual_ns = wake - now;
        if (wake >= sleep_target)
            overshoot_.add(static_cast<double>(wake - sleep_target));
    }
    // The spin is not optional: the OS wake above lands anywhere inside a
    // scheduler-jitter window (tens of microseconds to milliseconds,
    // machine- and load-dependent). Sleeping all the way to the deadline
    // hands that jitter directly to the frame time; spinning the last
    // measured-margin stretch trades a sliver of CPU for a deadline hit
    // accurate to the clock read.
    while (pacer_now_ns() < d.deadline_ns) {
        // tight clock re-read; each call is itself a brief pause
    }
    return stats;
}
