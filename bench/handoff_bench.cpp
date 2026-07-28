// Phase 6c: prices the three input-handoff backends. Headless — no GL/GLFW.
//
// Section 1 (uncontended cost): batched means — N ops between two
// pacer_now_ns() calls, so clock overhead amortizes to ~nothing. Median of
// R batches. All ops go through InputChannel& (virtual dispatch is what the
// app pays, and it blocks hoisting).
// Section 2 (contended sweep): a writer thread publishes at a target rate
// (absolute-deadline spin — one mechanism, exact from 1 kHz to unthrottled),
// the reader loops. One read in every kSampleEvery is timed; each sample
// carries one clock-pair (~20-30 ns, printed as handoff_clock:) which is
// identical across backends, so it cancels in the mutex-vs-seqlock
// comparison that defines the crossover. The reader validates payload
// consistency on every read (mutex/seqlock; bitmask is keys-only and
// unverifiable). Exit code is nonzero on any torn read or if a batch mean
// lands below 1 ns/op (elision tripwire).
#include "input_state.h"
#include "pacer.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>  // braced range-fors; GCC 13 rejects without it (Phase 5 lesson)
#include <thread>
#include <utility>
#include <vector>

namespace {

constexpr unsigned kSampleEvery = 32;

struct Params {
    uint64_t cost_warmup = 100'000;
    uint64_t cost_iters = 1'000'000;
    int      cost_reps = 5;
    double   sweep_warmup_s = 0.5;
    double   sweep_window_s = 2.0;
    std::vector<uint64_t> sweep_rates{1'000, 10'000, 100'000, 1'000'000, 0};
};

volatile uint64_t g_sink = 0;  // checksum sink: defeats dead-code elimination

InputSnapshot payload_for(uint64_t i) {
    InputSnapshot s;
    s.keys = static_cast<uint32_t>(i);
    s.mouse_dx = static_cast<float>(i & 0xFFFFu);
    s.mouse_dy = -s.mouse_dx;
    s.publish_ns = i * 1000003ull;
    return s;
}

// Every field re-derives from publish_ns; a torn snapshot fails. The zero
// snapshot (nothing published yet) is consistent by construction.
bool is_consistent(const InputSnapshot& r) {
    if (r.publish_ns % 1000003ull != 0) return false;
    const uint64_t i = r.publish_ns / 1000003ull;
    if (r.keys != static_cast<uint32_t>(i)) return false;
    const float dx = static_cast<float>(i & 0xFFFFu);
    return r.mouse_dx == dx && r.mouse_dy == -dx;
}

uint64_t percentile(const std::vector<uint64_t>& sorted, double q) {
    if (sorted.empty()) return 0;
    return sorted[static_cast<size_t>(
        std::llround(q * static_cast<double>(sorted.size() - 1)))];
}

double median_of(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

double time_publish_batch(InputChannel& ch, const Params& p) {
    std::vector<double> reps;
    for (int r = 0; r < p.cost_reps; ++r) {
        for (uint64_t i = 0; i < p.cost_warmup; ++i) ch.publish(payload_for(i));
        const uint64_t t0 = pacer_now_ns();
        for (uint64_t i = 0; i < p.cost_iters; ++i) ch.publish(payload_for(i));
        const uint64_t t1 = pacer_now_ns();
        reps.push_back(static_cast<double>(t1 - t0) /
                       static_cast<double>(p.cost_iters));
    }
    return median_of(reps);
}

double time_read_batch(const InputChannel& ch, const Params& p) {
    std::vector<double> reps;
    uint64_t sink = 0;
    for (int r = 0; r < p.cost_reps; ++r) {
        for (uint64_t i = 0; i < p.cost_warmup; ++i) sink += ch.read().keys;
        const uint64_t t0 = pacer_now_ns();
        for (uint64_t i = 0; i < p.cost_iters; ++i) {
            const InputSnapshot s = ch.read();
            sink += s.keys + s.publish_ns;
        }
        const uint64_t t1 = pacer_now_ns();
        reps.push_back(static_cast<double>(t1 - t0) /
                       static_cast<double>(p.cost_iters));
    }
    g_sink += sink;
    return median_of(reps);
}

double time_clock_pair(uint64_t iters) {
    uint64_t sink = 0;
    const uint64_t t0 = pacer_now_ns();
    for (uint64_t i = 0; i < iters; ++i) sink += pacer_now_ns();
    const uint64_t t1 = pacer_now_ns();
    g_sink += sink;
    // One timed sample brackets the op with two calls; report the pair.
    return 2.0 * static_cast<double>(t1 - t0) / static_cast<double>(iters);
}

struct SweepCell {
    double   achieved_hz = 0;
    uint64_t reads = 0;
    uint64_t read_p50_ns = 0, read_p99_ns = 0, read_max_ns = 0;
    uint64_t write_p99_ns = 0;
    uint64_t retries = 0;
    double   retries_per_sec = 0;
    uint64_t samples = 0;
    uint64_t torn = 0;
    double   window_s = 0;
};

SweepCell run_sweep_cell(InputChannel& ch, bool validate, uint64_t target_hz,
                         const Params& p) {
    std::atomic<bool> stop{false};
    std::atomic<uint64_t> published{0};
    std::vector<uint64_t> wsamples;
    wsamples.reserve(1'000'000);

    std::thread writer([&] {
        const uint64_t period = target_hz ? 1'000'000'000ull / target_hz : 0;
        uint64_t next = pacer_now_ns() + period;
        uint64_t i = 0;
        while (!stop.load(std::memory_order_relaxed)) {
            ++i;
            if (i % kSampleEvery == 0) {
                const uint64_t a = pacer_now_ns();
                ch.publish(payload_for(i));
                const uint64_t b = pacer_now_ns();
                if (wsamples.size() < wsamples.capacity()) wsamples.push_back(b - a);
            } else {
                ch.publish(payload_for(i));
            }
            published.store(i, std::memory_order_relaxed);
            if (period) {
                // Absolute deadlines: exact at every rate, owns a core (a
                // bench harness may). Late deadlines publish back-to-back to
                // catch up; achieved_hz reports the truth either way.
                while (pacer_now_ns() < next) { }
                next += period;
            }
        }
    });

    // Warmup: run the reader unmeasured while the writer settles.
    const uint64_t warm_end =
        pacer_now_ns() + static_cast<uint64_t>(p.sweep_warmup_s * 1e9);
    while (pacer_now_ns() < warm_end) g_sink += ch.read().keys;

    SweepCell cell;
    std::vector<uint64_t> samples;
    samples.reserve(4'000'000);  // ~32 MB cap; stop sampling when full
    const uint64_t retries0 = ch.read_retries();
    const uint64_t pub0 = published.load(std::memory_order_relaxed);
    const uint64_t w0 = pacer_now_ns();
    const uint64_t w_end = w0 + static_cast<uint64_t>(p.sweep_window_s * 1e9);
    uint64_t sink = 0;
    for (;;) {
        // kSampleEvery-1 untimed reads, then one timed read that also
        // checks the window clock (so the hot loop reads no extra clocks).
        for (unsigned k = 1; k < kSampleEvery; ++k) {
            const InputSnapshot r = ch.read();
            sink += r.keys + r.publish_ns;
            if (validate && !is_consistent(r)) ++cell.torn;
            ++cell.reads;
        }
        const uint64_t a = pacer_now_ns();
        const InputSnapshot r = ch.read();
        const uint64_t b = pacer_now_ns();
        sink += r.keys + r.publish_ns;
        if (validate && !is_consistent(r)) ++cell.torn;
        ++cell.reads;
        if (samples.size() < samples.capacity()) samples.push_back(b - a);
        if (b >= w_end) break;
    }
    const uint64_t w1 = pacer_now_ns();
    const uint64_t pub1 = published.load(std::memory_order_relaxed);
    const uint64_t retries1 = ch.read_retries();
    stop.store(true, std::memory_order_relaxed);
    writer.join();
    g_sink += sink;

    cell.window_s = static_cast<double>(w1 - w0) / 1e9;
    cell.achieved_hz = static_cast<double>(pub1 - pub0) / cell.window_s;
    cell.retries = retries1 - retries0;
    cell.retries_per_sec = static_cast<double>(cell.retries) / cell.window_s;
    cell.samples = samples.size();
    std::sort(samples.begin(), samples.end());
    cell.read_p50_ns = percentile(samples, 0.50);
    cell.read_p99_ns = percentile(samples, 0.99);
    cell.read_max_ns = samples.empty() ? 0 : samples.back();
    std::sort(wsamples.begin(), wsamples.end());
    cell.write_p99_ns = percentile(wsamples, 0.99);
    return cell;
}

}  // namespace

int main(int argc, char** argv) {
    Params p;
    if (argc > 1 && std::strcmp(argv[1], "--quick") == 0) {
        p.cost_warmup = 2'000;
        p.cost_iters = 20'000;
        p.cost_reps = 2;
        p.sweep_warmup_s = 0.05;
        p.sweep_window_s = 0.2;
        p.sweep_rates = {1'000, 0};
    }

    bool failed = false;
    std::printf("handoff_clock: pair_ns=%.1f\n", time_clock_pair(p.cost_iters));

    for (const char* name : {"mutex", "bitmask", "seqlock"}) {
        auto ch = make_input_channel(name);
        const double pub_ns = time_publish_batch(*ch, p);
        const double read_ns = time_read_batch(*ch, p);
        for (const auto& [op, ns] : {std::pair{"publish", pub_ns},
                                     std::pair{"read", read_ns}}) {
            std::printf("handoff_cost: backend=%s op=%s iters=%llu reps=%d "
                        "ns_per_op=%.1f\n",
                        name, op,
                        static_cast<unsigned long long>(p.cost_iters),
                        p.cost_reps, ns);
            if (ns < 1.0) {  // no real publish/read is sub-nanosecond
                std::fprintf(stderr, "FATAL: %s %s ns_per_op=%.3f < 1 — "
                             "compiler elided the op\n", name, op, ns);
                failed = true;
            }
        }
    }

    for (const char* name : {"mutex", "bitmask", "seqlock"}) {
        auto ch = make_input_channel(name);
        const bool validate = std::strcmp(name, "bitmask") != 0;
        for (const uint64_t hz : p.sweep_rates) {
            const SweepCell c = run_sweep_cell(*ch, validate, hz, p);
            std::printf("handoff_sweep: backend=%s target_hz=%llu "
                        "achieved_hz=%.1f reads=%llu read_p50_ns=%llu "
                        "read_p99_ns=%llu read_max_ns=%llu write_p99_ns=%llu "
                        "retries=%llu retries_per_sec=%.1f samples=%llu "
                        "torn=%llu window_s=%.2f\n",
                        name, static_cast<unsigned long long>(hz),
                        c.achieved_hz,
                        static_cast<unsigned long long>(c.reads),
                        static_cast<unsigned long long>(c.read_p50_ns),
                        static_cast<unsigned long long>(c.read_p99_ns),
                        static_cast<unsigned long long>(c.read_max_ns),
                        static_cast<unsigned long long>(c.write_p99_ns),
                        static_cast<unsigned long long>(c.retries),
                        c.retries_per_sec,
                        static_cast<unsigned long long>(c.samples),
                        static_cast<unsigned long long>(c.torn),
                        c.window_s);
            if (c.torn != 0) {
                std::fprintf(stderr, "FATAL: %s at %llu Hz: %llu torn reads\n",
                             name, static_cast<unsigned long long>(hz),
                             static_cast<unsigned long long>(c.torn));
                failed = true;
            }
            if (c.samples == 0 || c.achieved_hz <= 0.0) {
                std::fprintf(stderr, "FATAL: %s at %llu Hz: empty cell\n",
                             name, static_cast<unsigned long long>(hz));
                failed = true;
            }
        }
    }

    std::printf("handoff_bench: %s sink=%llu\n", failed ? "FAILED" : "ok",
                static_cast<unsigned long long>(g_sink));
    return failed ? 1 : 0;
}
