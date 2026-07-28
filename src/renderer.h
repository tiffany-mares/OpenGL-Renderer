#pragma once
#include <atomic>
#include <cstdint>

#include "pacer.h"  // PaceStrategy

struct GLFWwindow;
class InputChannel;
class FramebufferSize;

// Everything main() decides that the render thread needs. Passed by value —
// the render thread owns its copy for its whole life.
struct RenderConfig {
    uint32_t fps_cap = 0;                            // 0 = uncapped
    PaceStrategy pace = PaceStrategy::TimerSpin;     // used only when fps_cap > 0
    ReschedulePolicy resched = ReschedulePolicy::Absolute;  // Phase 6d drift figure
    const char* log_path = nullptr;                  // per-run CSV; null = no logging
    uint64_t bench_frames = 0;                       // >0: run exactly N frames, then exit
    const char* capture_path = nullptr;              // Phase 7: raw RGB frames; null = no capture
    uint32_t capture_frames = 0;                     // >0: capture exactly N frames, then exit
};

// Body of the render thread. Owns the GL context and every GL object:
// makes the context current, loads GLAD, sets swap interval 0, builds
// shaders/buffers, draws until `stop` is set (or cfg.bench_frames frames
// have run), then deletes all GL objects and detaches the context before
// returning (shutdown ordering requires GL teardown on this thread, before
// the join). Bench mode prints a one-line CPU/wall summary measured from
// the 500-frame warmup boundary. On init failure: sets `failed`, requests
// window close, returns early.
void render_thread_main(GLFWwindow* window, const InputChannel& input,
                        const FramebufferSize& fb, RenderConfig cfg,
                        const std::atomic<bool>& stop, std::atomic<bool>& failed);

#ifdef __EMSCRIPTEN__
// Phase 8 web entry: makes the context current on the (only) thread, builds
// the scene, hands the frame clock to requestAnimationFrame via
// emscripten_set_main_loop — which never returns. Returns nonzero only on
// scene-init failure, before the loop starts.
int run_web(GLFWwindow* window);
#endif
