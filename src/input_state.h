#pragma once
#include <atomic>
#include <cstdint>
#include <mutex>

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
    uint64_t publish_ns = 0;  // steady_clock at publish
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
