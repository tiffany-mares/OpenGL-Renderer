#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "input_state.h"
#include "renderer.h"

static void glfw_error_callback(int code, const char* desc) {
    std::fprintf(stderr, "GLFW error %d: %s\n", code, desc);
}

int main() {
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

    InputChannel input;
    {
        InputSnapshot first;
        glfwGetFramebufferSize(window, &first.fb_width, &first.fb_height);
        input.publish(first);
    }

    std::atomic<bool> stop{false};
    std::atomic<bool> render_failed{false};
    std::thread render_thread(render_thread_main, window, std::cref(input),
                              std::cref(stop), std::ref(render_failed));

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
            glfwSetWindowShouldClose(window, GLFW_TRUE);

        InputSnapshot s;
        s.space_held = glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS;
        glfwGetFramebufferSize(window, &s.fb_width, &s.fb_height);
        s.publish_ns = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch())
                .count());
        input.publish(s);

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
