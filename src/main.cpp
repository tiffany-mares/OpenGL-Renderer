#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string_view>
#include <thread>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "input_state.h"
#include "renderer.h"

static void glfw_error_callback(int code, const char* desc) {
    std::fprintf(stderr, "GLFW error %d: %s\n", code, desc);
}

int main(int argc, char** argv) {
    const char* backend = "mutex";  // the baseline is the default
    uint32_t fps = 0;               // 0 = uncapped (pre-Phase-5 behavior)
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
        } else {
            std::fprintf(stderr,
                         "usage: cube [--input=mutex|bitmask|seqlock] [--fps N]\n");
            return EXIT_FAILURE;
        }
    }
    std::unique_ptr<InputChannel> input = make_input_channel(backend);
    if (!input) {
        std::fprintf(stderr, "unknown input backend '%s' (mutex|bitmask|seqlock)\n", backend);
        return EXIT_FAILURE;
    }
    std::printf("input backend: %s\n", input->name());
    if (fps > 0) std::printf("fps cap: %u\n", fps);

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
    std::thread render_thread(render_thread_main, window, std::cref(*input),
                              std::cref(fb), fps, std::cref(stop),
                              std::ref(render_failed));

    double mx_prev = 0.0, my_prev = 0.0;
    glfwGetCursorPos(window, &mx_prev, &my_prev);

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

        s.publish_ns = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch())
                .count());
        input->publish(s);

        // ~1000 Hz poll cadence. sleep_for's real resolution on stock
        // Windows is the scheduler tick; the honest pacer is Phase 5.
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    // Shutdown ordering: signal, join (render thread deletes its GL objects
    // and detaches the context before returning), THEN destroy the window
    // on this thread, then terminate.
    stop.store(true);
    render_thread.join();
    glfwDestroyWindow(window);
    glfwTerminate();
    return render_failed.load() ? EXIT_FAILURE : EXIT_SUCCESS;
}
