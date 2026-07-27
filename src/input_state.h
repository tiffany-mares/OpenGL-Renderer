#pragma once
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string_view>

// Phase 4: three input-handoff backends behind one interface, main thread ->
// render thread. This file is the whole handoff system: payload, interface,
// backends, and the framebuffer-size side channel.

// One bit per tracked key.
inline constexpr uint32_t kKeySpace = 1u << 0;
inline constexpr uint32_t kKeyLeft  = 1u << 1;
inline constexpr uint32_t kKeyRight = 1u << 2;
inline constexpr uint32_t kKeyUp    = 1u << 3;
inline constexpr uint32_t kKeyDown  = 1u << 4;

// Deliberately bigger than a machine word: the seqlock backend needs a
// payload the atomic-bitmask backend cannot carry. publish_ns is why the
// bitmask alone is not the final answer — Phase 6 computes end-to-end input
// latency as consume time minus publish_ns.
struct InputSnapshot {
    uint32_t keys = 0;        // bitmask of kKey* bits
    float mouse_dx = 0.f;     // cursor delta since previous publish, pixels
    float mouse_dy = 0.f;
    uint64_t publish_ns = 0;  // pacer_now_ns() at publish — same timeline the consumer reads
};

// Single-producer (main thread) / single-consumer (render thread) handoff.
class InputChannel {
public:
    virtual ~InputChannel() = default;
    virtual void publish(const InputSnapshot& s) = 0;
    virtual InputSnapshot read() const = 0;
    virtual const char* name() const = 0;
};

// Baseline: a mutex around a small POD. The thing the other two are
// benchmarked against in Phase 6.
class MutexChannel final : public InputChannel {
public:
    void publish(const InputSnapshot& s) override {
        std::lock_guard<std::mutex> lock(mu_);
        snap_ = s;
    }

    InputSnapshot read() const override {
        std::lock_guard<std::mutex> lock(mu_);
        return snap_;
    }

    const char* name() const override { return "mutex"; }

private:
    mutable std::mutex mu_;
    InputSnapshot snap_;
};

// Lock-free: one atomic word, one bit per key. Writer turns key edges into
// fetch_or/fetch_and RMWs with release; reader does a single acquire load
// per frame. No torn reads (single word), no ABA (bits carry no generation
// — a set bit means "held", nothing else). The trade: 32 bits cannot carry
// mouse deltas or publish_ns, so those read back as zero — which is exactly
// why this backend alone is not the final answer (Phase 6 needs publish_ns).
class BitmaskChannel final : public InputChannel {
    static_assert(std::atomic<uint32_t>::is_always_lock_free);

public:
    void publish(const InputSnapshot& s) override {
        const uint32_t want = s.keys;
        const uint32_t to_set = want & ~prev_;
        const uint32_t to_clear = prev_ & ~want;
        if (to_set) keys_.fetch_or(to_set, std::memory_order_release);
        if (to_clear) keys_.fetch_and(~to_clear, std::memory_order_release);
        prev_ = want;
    }

    InputSnapshot read() const override {
        InputSnapshot s;
        s.keys = keys_.load(std::memory_order_acquire);  // the frame's single load
        return s;
    }

    const char* name() const override { return "bitmask"; }

private:
    std::atomic<uint32_t> keys_{0};
    uint32_t prev_ = 0;  // writer-thread-only: last published mask, for edge diffs
};

// Seqlock: for a payload that outgrew the word. Single writer bumps the
// sequence to odd, writes the payload, bumps to even; the reader copies the
// payload and keeps it only if the sequence was even and unchanged around
// the copy. The writer never blocks — a slow or absent reader costs it
// nothing; the reader retries instead.
//
// HONESTY NOTE (also in README.md): the reader's `payload_` copy is
// unsynchronized while the writer may be storing to it — formally a data
// race, i.e. undefined behavior under the C++ memory model; the fences
// below are the standard practical construction (Boehm, "Can seqlocks get
// along with programming language memory models?") and every real CPU +
// compiler shipping today makes the retry discard any torn copy. Naming the
// rule being bent is the point; pretending it isn't bent would be the bug.
class SeqlockChannel final : public InputChannel {
public:
    void publish(const InputSnapshot& s) override {
        const uint32_t s0 = seq_.load(std::memory_order_relaxed);
        seq_.store(s0 + 1, std::memory_order_relaxed);        // odd: write in progress
        std::atomic_thread_fence(std::memory_order_release);  // odd store before payload stores
        payload_ = s;                                         // the racy write
        seq_.store(s0 + 2, std::memory_order_release);        // even: payload stores before this
    }

    InputSnapshot read() const override {
        for (;;) {
            const uint32_t s0 = seq_.load(std::memory_order_acquire);
            if (s0 & 1u) continue;                            // writer mid-update
            InputSnapshot s = payload_;                       // the racy read
            std::atomic_thread_fence(std::memory_order_acquire);  // copy before recheck
            if (seq_.load(std::memory_order_relaxed) == s0) return s;
        }
    }

    const char* name() const override { return "seqlock"; }

private:
    std::atomic<uint32_t> seq_{0};
    InputSnapshot payload_;
};

// Framebuffer size, published by the main thread (GLFW size queries are
// main-thread-only), packed into one atomic word so a resize can never tear.
// Not part of the input payload: window state, not input — and the bitmask
// backend has no room for it anyway.
class FramebufferSize {
public:
    void store(int w, int h) {
        packed_.store((uint64_t(uint32_t(w)) << 32) | uint32_t(h),
                      std::memory_order_relaxed);
    }

    void load(int& w, int& h) const {
        const uint64_t p = packed_.load(std::memory_order_relaxed);
        w = int(uint32_t(p >> 32));
        h = int(uint32_t(p));
    }

private:
    std::atomic<uint64_t> packed_{(uint64_t(1280) << 32) | 720};
};

// Backend selection for main(): flag string in, channel out.
inline std::unique_ptr<InputChannel> make_input_channel(std::string_view name) {
    if (name == "mutex") return std::make_unique<MutexChannel>();
    if (name == "bitmask") return std::make_unique<BitmaskChannel>();
    if (name == "seqlock") return std::make_unique<SeqlockChannel>();
    return nullptr;
}
