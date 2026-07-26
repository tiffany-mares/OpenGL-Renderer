#pragma once
#include <atomic>

struct GLFWwindow;
class InputChannel;

// Body of the render thread. Owns the GL context and every GL object:
// makes the context current, loads GLAD, sets swap interval 0, builds
// shaders/buffers, draws until `stop` is set, then deletes all GL objects
// and detaches the context before returning (shutdown ordering requires
// GL teardown to happen on this thread, before the join).
// On init failure: sets `failed`, requests window close, returns early.
void render_thread_main(GLFWwindow* window, const InputChannel& input,
                        const std::atomic<bool>& stop, std::atomic<bool>& failed);
