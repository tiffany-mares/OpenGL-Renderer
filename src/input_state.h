#pragma once
#include <cstdint>
#include <mutex>

// Phase 3: minimal mutex-guarded snapshot handoff, main thread -> render
// thread. Phase 4 puts three backends (mutex / atomic bitmask / seqlock)
// behind one interface; this struct is the payload they will carry.
struct InputSnapshot {
    bool space_held = false;  // held = pause rotation (visible cross-thread effect)
    int fb_width = 1280;      // framebuffer size, published by the main thread:
    int fb_height = 720;      //   GLFW size queries are main-thread-only
    uint64_t publish_ns = 0;  // steady_clock at publish; Phase 6 measures latency with it
};

class InputChannel {
public:
    void publish(const InputSnapshot& s) {
        std::lock_guard<std::mutex> lock(mu_);
        snap_ = s;
    }

    InputSnapshot read() const {
        std::lock_guard<std::mutex> lock(mu_);
        return snap_;
    }

private:
    mutable std::mutex mu_;
    InputSnapshot snap_;
};
