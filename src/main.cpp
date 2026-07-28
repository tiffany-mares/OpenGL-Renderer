#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string_view>
#include <thread>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "input_state.h"
#include "pacer.h"
#include "renderer.h"

static void glfw_error_callback(int code, const char* desc) {
    std::fprintf(stderr, "GLFW error %d: %s\n", code, desc);
}

int main(int argc, char** argv) {
    const char* backend = "mutex";  // the baseline is the default
    uint32_t fps = 0;               // 0 = uncapped (pre-Phase-5 behavior)
    const char* log_path = nullptr;  // Phase 6a: per-run CSV (requires a paced run)
    PaceStrategy pace = PaceStrategy::TimerSpin;  // the shipping default
    bool pace_given = false;
    ReschedulePolicy resched = ReschedulePolicy::Absolute;  // Phase 6d drift figure
    bool resched_given = false;
    uint64_t bench_frames = 0;  // 0 = interactive run
    uint32_t poll_hz = 1000;  // Phase 6c: the poll loop is properly paced by default
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg.rfind("--input=", 0) == 0) {
            backend = argv[i] + 8;
        } else if (arg.rfind("--fps=", 0) == 0 || (arg == "--fps" && i + 1 < argc)) {
            const char* v = (arg == "--fps") ? argv[++i] : argv[i] + 6;
            char* end = nullptr;
            const unsigned long parsed = std::strtoul(v, &end, 10);
            if (end == v || *end != '\0' || parsed > 1000) {
                std::fprintf(stderr, "bad --fps value '%s' (want 0..1000; 0 = uncapped)\n", v);
                return EXIT_FAILURE;
            }
            fps = static_cast<uint32_t>(parsed);  // 0 = explicit uncapped
        } else if (arg.rfind("--log=", 0) == 0 || (arg == "--log" && i + 1 < argc)) {
            log_path = (arg == "--log") ? argv[++i] : argv[i] + 6;
            if (*log_path == '\0') {
                std::fprintf(stderr, "bad --log value: empty path\n");
                return EXIT_FAILURE;
            }
        } else if (arg.rfind("--pace=", 0) == 0 || (arg == "--pace" && i + 1 < argc)) {
            const char* v = (arg == "--pace") ? argv[++i] : argv[i] + 7;
            if (!parse_pace_strategy(v, pace)) {
                std::fprintf(stderr, "bad --pace value '%s' (sleep|timer|timer_spin|spin)\n", v);
                return EXIT_FAILURE;
            }
            pace_given = true;
        } else if (arg.rfind("--resched=", 0) == 0 || (arg == "--resched" && i + 1 < argc)) {
            const char* v = (arg == "--resched") ? argv[++i] : argv[i] + 10;
            if (!parse_resched_policy(v, resched)) {
                std::fprintf(stderr, "bad --resched value '%s' (absolute|relative)\n", v);
                return EXIT_FAILURE;
            }
            resched_given = true;
        } else if (arg.rfind("--bench-frames=", 0) == 0 ||
                   (arg == "--bench-frames" && i + 1 < argc)) {
            const char* v = (arg == "--bench-frames") ? argv[++i] : argv[i] + 15;
            char* end = nullptr;
            const unsigned long long parsed = std::strtoull(v, &end, 10);
            if (end == v || *end != '\0' || parsed <= 500 || parsed > 10'000'000ULL) {
                std::fprintf(stderr,
                             "bad --bench-frames value '%s' (must exceed the 500-frame warmup "
                             "and not exceed 10000000)\n", v);
                return EXIT_FAILURE;
            }
            bench_frames = parsed;
        } else if (arg.rfind("--poll-hz=", 0) == 0 || (arg == "--poll-hz" && i + 1 < argc)) {
            const char* v = (arg == "--poll-hz") ? argv[++i] : argv[i] + 10;
            char* end = nullptr;
            const unsigned long parsed = std::strtoul(v, &end, 10);
            if (end == v || *end != '\0' || parsed < 1 || parsed > 10000) {
                std::fprintf(stderr, "bad --poll-hz value '%s' (want 1..10000)\n", v);
                return EXIT_FAILURE;
            }
            poll_hz = static_cast<uint32_t>(parsed);
        } else {
            std::fprintf(stderr,
                         "usage: cube [--input=mutex|bitmask|seqlock] [--fps N] "
                         "[--pace sleep|timer|timer_spin|spin] [--resched absolute|relative] "
                         "[--log PATH] [--bench-frames N] "
                         "[--poll-hz N]\n");
            return EXIT_FAILURE;
        }
    }
    if (log_path && fps == 0 && bench_frames == 0) {
        std::fprintf(stderr,
                     "--log requires --fps or --bench-frames (uncapped interactive runs have "
                     "no frame clock to log against)\n");
        return EXIT_FAILURE;
    }
    if (pace_given && fps == 0) {
        std::fprintf(stderr, "--pace requires --fps (there is nothing to pace uncapped)\n");
        return EXIT_FAILURE;
    }
    if (resched_given && fps == 0) {
        std::fprintf(stderr,
                     "--resched requires --fps (there is no schedule to reschedule uncapped)\n");
        return EXIT_FAILURE;
    }
    std::unique_ptr<InputChannel> input = make_input_channel(backend);
    if (!input) {
        std::fprintf(stderr, "unknown input backend '%s' (mutex|bitmask|seqlock)\n", backend);
        return EXIT_FAILURE;
    }
    std::printf("input backend: %s\n", input->name());
    if (fps > 0) std::printf("fps cap: %u\n", fps);
    if (log_path) std::printf("frame log: %s\n", log_path);
    if (fps > 0) {
        const char* pace_name = pace == PaceStrategy::SleepFor ? "sleep"
                                : pace == PaceStrategy::Timer  ? "timer"
                                : pace == PaceStrategy::Spin   ? "spin"
                                                               : "timer_spin";
        std::printf("pace strategy: %s\n", pace_name);
    }
    if (fps > 0)
        std::printf("resched policy: %s\n",
                    resched == ReschedulePolicy::Relative ? "relative" : "absolute");
    if (bench_frames > 0)
        std::printf("bench frames: %llu\n", static_cast<unsigned long long>(bench_frames));
    std::printf("poll rate: %u Hz\n", poll_hz);

    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit()) {
        std::fprintf(stderr, "glfwInit failed\n");
        return EXIT_FAILURE;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif

    GLFWwindow* window = glfwCreateWindow(1280, 720, "cube", nullptr, nullptr);
    if (!window) {
        std::fprintf(stderr, "glfwCreateWindow failed\n");
        glfwTerminate();
        return EXIT_FAILURE;
    }

    // Deliberately no glfwMakeContextCurrent here: the render thread owns
    // the context; this thread owns the window and the event queue.

    FramebufferSize fb;
    {
        int w = 0, h = 0;
        glfwGetFramebufferSize(window, &w, &h);
        fb.store(w, h);
    }

    std::atomic<bool> stop{false};
    std::atomic<bool> render_failed{false};

    RenderConfig cfg;
    cfg.fps_cap = fps;
    cfg.pace = pace;
    cfg.resched = resched;
    cfg.log_path = log_path;
    cfg.bench_frames = bench_frames;

    std::thread render_thread(render_thread_main, window, std::cref(*input),
                              std::cref(fb), cfg, std::cref(stop),
                              std::ref(render_failed));

    double mx_prev = 0.0, my_prev = 0.0;
    glfwGetCursorPos(window, &mx_prev, &my_prev);

    // Phase 6c: pace the poll loop with the platform timer. sleep_for(1ms)
    // really wakes at the ~15.6 ms scheduler tick on stock Windows (measured
    // in 6a) — the app published at ~64 Hz, not the intended ~1 kHz.
    FramePacer poll_pacer(1'000'000'000ull / poll_hz, PaceStrategy::TimerSpin);
    uint64_t publishes = 0;
    const uint64_t poll_start_ns = pacer_now_ns();

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
            glfwSetWindowShouldClose(window, GLFW_TRUE);

        int w = 0, h = 0;
        glfwGetFramebufferSize(window, &w, &h);
        fb.store(w, h);

        InputSnapshot s;
        if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS) s.keys |= kKeySpace;
        if (glfwGetKey(window, GLFW_KEY_LEFT)  == GLFW_PRESS) s.keys |= kKeyLeft;
        if (glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS) s.keys |= kKeyRight;
        if (glfwGetKey(window, GLFW_KEY_UP)    == GLFW_PRESS) s.keys |= kKeyUp;
        if (glfwGetKey(window, GLFW_KEY_DOWN)  == GLFW_PRESS) s.keys |= kKeyDown;

        // Mouse delta since the previous publish. Carried for payload size
        // and Phase 6; nothing consumes it yet.
        double mx = 0.0, my = 0.0;
        glfwGetCursorPos(window, &mx, &my);
        s.mouse_dx = static_cast<float>(mx - mx_prev);
        s.mouse_dy = static_cast<float>(my - my_prev);
        mx_prev = mx;
        my_prev = my;

        // Same monotonic timeline as the render thread's consume timestamp —
        // input_latency_ns is publish-to-consume on ONE clock, or it is noise.
        s.publish_ns = pacer_now_ns();
        input->publish(s);

        ++publishes;
        poll_pacer.wait();
    }

    // Shutdown ordering: signal, join (render thread deletes its GL objects
    // and detaches the context before returning), THEN destroy the window
    // on this thread, then terminate.
    stop.store(true);
    render_thread.join();

    const uint64_t poll_wall_ns = pacer_now_ns() - poll_start_ns;
    std::printf("handoff: backend=%s poll_hz=%u publishes=%llu wall_ns=%llu "
                "achieved_hz=%.1f missed=%llu retries=%llu\n",
                input->name(), poll_hz,
                static_cast<unsigned long long>(publishes),
                static_cast<unsigned long long>(poll_wall_ns),
                poll_wall_ns ? 1e9 * static_cast<double>(publishes) /
                               static_cast<double>(poll_wall_ns) : 0.0,
                static_cast<unsigned long long>(poll_pacer.missed()),
                static_cast<unsigned long long>(input->read_retries()));

    glfwDestroyWindow(window);
    glfwTerminate();
    return render_failed.load() ? EXIT_FAILURE : EXIT_SUCCESS;
}
