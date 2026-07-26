# Phase 3: Split the Render Thread — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Main thread owns the window and events (polled ~1000 Hz, publishing input snapshots); a render thread owns the GL context and every GL object; shutdown is ordered signal → GL teardown on the render thread → join → `glfwDestroyWindow` on main — surviving a five-minute soak and repeated open/close cycles.

**Architecture:** `src/main.cpp` shrinks to GLFW init, window creation, the poll/publish loop, and shutdown ordering — it never makes the context current and includes no GL headers. New `src/renderer.{h,cpp}` receives everything GL (shaders, helpers, geometry, `GLAD_GL_IMPLEMENTATION`, draw loop) as `render_thread_main(...)`, which makes the context current, loads GLAD, calls `glfwSwapInterval(0)` (it must run on the context thread), draws until signaled, then deletes GL objects and detaches the context before returning. New `src/input_state.h` holds the `InputSnapshot` POD and a mutex-guarded `InputChannel` — deliberately minimal; Phase 4 grows this into the three-backend interface. The snapshot carries `space_held` (render thread pauses rotation — an observable cross-thread effect), the framebuffer size (published by main because GLFW size queries are main-thread-only), and `publish_ns` (Phase 6 needs it).

**Tech Stack:** C++20 `std::thread`/`std::atomic`/`std::mutex`, OpenGL 3.3 core, GLFW 3.4, glad2, CMake + Threads::Threads.

## Context

Phases 0–2 are green: single-threaded rotating cube, hand-rolled mat4, ctest in CI. Phase 3 is the outline's "part of the original brief that is wrong as stated": GLFW events must stay on the main thread, so the *renderer* moves to a worker, not the input. GLFW thread-safety facts the design leans on: `glfwPollEvents`/`glfwGetKey`/`glfwGetFramebufferSize`/`glfwCreateWindow`/`glfwDestroyWindow` are main-thread-only; `glfwMakeContextCurrent`/`glfwSwapBuffers`/`glfwSwapInterval` belong to the thread that holds the context; `glfwWindowShouldClose`, `glfwSetWindowShouldClose`, and `glfwGetTime` (reads) are documented callable from any thread. Payoff to record in Phase 7: decoupling the rates means a 30 fps cap no longer implies 33 ms of input latency.

## Global Constraints

- Main thread never calls any `gl*` function and never makes the context current; render thread never calls main-thread-only GLFW functions.
- Shutdown ordering exactly: `stop` signaled → render thread deletes VAO/VBO/EBO/program, `glfwMakeContextCurrent(nullptr)`, returns → `join` → `glfwDestroyWindow` → `glfwTerminate`.
- `GLAD_GL_IMPLEMENTATION` in exactly one TU — it moves from `main.cpp` to `renderer.cpp`.
- Main loop paced ~1000 Hz via `sleep_for(1ms)` — real resolution on stock Windows is the scheduler tick; the honest pacer is Phase 5 (comment this in code).
- Render loop stays uncapped (comment unchanged) until Phase 5.
- The 8-vertex/36-index geometry, flat-shading scheme, and `mat4` API must not change.
- Build: `& "C:\Program Files\CMake\bin\cmake.exe" --build build --config Release`; reconfigure first (`-B build`) because source files are added. Tests: `& "C:\Program Files\CMake\bin\ctest.exe" --test-dir build -C Release --output-on-failure`.
- Repo root: `C:\Users\tiffm\Desktop\OpenGL Renderer\OpenGL-Renderer` (quote paths). Commit per task; push at the end; CI green.

---

### Task 1: InputSnapshot + InputChannel (TDD)

**Files:**
- Create: `tests/input_test.cpp`
- Create: `src/input_state.h`
- Modify: `CMakeLists.txt` (append test target)

**Interfaces:**
- Produces: `struct InputSnapshot { bool space_held; int fb_width, fb_height; uint64_t publish_ns; }` (defaults: false, 1280, 720, 0); `class InputChannel` with `void publish(const InputSnapshot&)` and `InputSnapshot read() const` — by-value copy in/out under a `std::mutex`. Task 2's main thread publishes, render thread reads.

- [ ] **Step 1: Write the failing test** — create `tests/input_test.cpp`:

```cpp
#include <cstdio>

#include "input_state.h"

static int g_failures = 0;

static void expect(bool cond, const char* what) {
    if (!cond) {
        std::fprintf(stderr, "FAIL %s\n", what);
        ++g_failures;
    }
}

int main() {
    InputChannel ch;
    InputSnapshot d = ch.read();  // readable before any publish
    expect(!d.space_held, "default space_held false");
    expect(d.fb_width == 1280 && d.fb_height == 720, "default fb size");

    InputSnapshot s;
    s.space_held = true;
    s.fb_width = 800;
    s.fb_height = 600;
    s.publish_ns = 42;
    ch.publish(s);
    InputSnapshot r = ch.read();
    expect(r.space_held, "space_held roundtrip");
    expect(r.fb_width == 800 && r.fb_height == 600, "fb size roundtrip");
    expect(r.publish_ns == 42, "publish_ns roundtrip");

    // read() returns a copy: mutating it must not affect the channel.
    r.fb_width = 1;
    expect(ch.read().fb_width == 800, "read returns independent copy");

    if (g_failures) {
        std::fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("all input tests passed\n");
    return 0;
}
```

- [ ] **Step 2: Add the test target** — in `CMakeLists.txt`, after the `mat4_tests` block:

```cmake
add_executable(input_tests tests/input_test.cpp)
target_include_directories(input_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME input_tests COMMAND input_tests)
```

- [ ] **Step 3: Verify red**

Run: `& "C:\Program Files\CMake\bin\cmake.exe" -B build` then `& "C:\Program Files\CMake\bin\cmake.exe" --build build --config Release --target input_tests`
Expected: fatal error C1083 — `input_state.h` not found.

- [ ] **Step 4: Implement `src/input_state.h`**

```cpp
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
```

- [ ] **Step 5: Verify green**

Run: `& "C:\Program Files\CMake\bin\cmake.exe" --build build --config Release --target input_tests` then ctest.
Expected: `2/2` tests pass (`mat4_tests` + `input_tests`), output `all input tests passed`.

- [ ] **Step 6: Commit**

```powershell
git add src/input_state.h tests/input_test.cpp CMakeLists.txt
git commit -m "feat: InputSnapshot and mutex-guarded InputChannel with tests"
```

---

### Task 2: Extract renderer.{h,cpp}; thread the render loop; ordered shutdown

**Files:**
- Create: `src/renderer.h`
- Create: `src/renderer.cpp`
- Modify: `src/main.cpp` (rewrite — becomes events/publish/shutdown only)
- Modify: `CMakeLists.txt` (add source + Threads)

**Interfaces:**
- Consumes: `InputChannel::read()`, `InputSnapshot` fields from Task 1; `mat4.h` functions; existing `kVertexSrc`/`kFragmentSrc`, `compile_shader`, `link_program`, `kVertices`, `kIndices` (moved verbatim out of `src/main.cpp` — do not retype them, move them).
- Produces: `void render_thread_main(GLFWwindow* window, const InputChannel& input, const std::atomic<bool>& stop, std::atomic<bool>& failed);` — the exact entry point Phases 4–6 keep extending.

- [ ] **Step 1: Write `src/renderer.h`**

```cpp
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
```

- [ ] **Step 2: Write `src/renderer.cpp`** — the moved GL code plus the thread body. Move these blocks **verbatim** from `src/main.cpp`: the `kVertexSrc`/`kFragmentSrc` literals with their comment, `compile_shader`, `link_program`, and the `kVertices`/`kIndices` arrays with their comment. The file is:

```cpp
#include "renderer.h"

#include <cstdio>

#define GLAD_GL_IMPLEMENTATION
#include <glad/gl.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "input_state.h"
#include "mat4.h"

// ==== moved verbatim from main.cpp: kVertexSrc, kFragmentSrc (with their
// ==== comment), compile_shader, link_program, kVertices, kIndices (with
// ==== their comment)

void render_thread_main(GLFWwindow* window, const InputChannel& input,
                        const std::atomic<bool>& stop, std::atomic<bool>& failed) {
    glfwMakeContextCurrent(window);
    glfwSwapInterval(0);  // must run on the context-owning thread

    if (gladLoadGL(glfwGetProcAddress) == 0) {
        std::fprintf(stderr, "gladLoadGL failed\n");
        failed.store(true);
        glfwMakeContextCurrent(nullptr);
        glfwSetWindowShouldClose(window, GLFW_TRUE);  // documented any-thread
        return;
    }

    std::printf("GL_VERSION:  %s\n", glGetString(GL_VERSION));
    std::printf("GL_RENDERER: %s\n", glGetString(GL_RENDERER));

    GLuint program = link_program(kVertexSrc, kFragmentSrc);
    if (program == 0) {
        failed.store(true);
        glfwMakeContextCurrent(nullptr);
        glfwSetWindowShouldClose(window, GLFW_TRUE);
        return;
    }

    GLint mvpLoc = glGetUniformLocation(program, "uMvp");

    GLuint vao = 0, vbo = 0, ebo = 0;
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glGenBuffers(1, &ebo);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof kVertices, kVertices, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof kIndices, kIndices, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glEnable(GL_DEPTH_TEST);

    // Rotation accumulates wall-clock time only while unpaused, so holding
    // SPACE (published by the main thread) visibly freezes the cube — the
    // cheapest proof the cross-thread handoff works. glfwGetTime reads are
    // documented any-thread.
    double angle = 0.0;
    double prev = glfwGetTime();

    // Uncapped loop: swap interval is 0 and the pacer doesn't exist until
    // Phase 5, so this spins as fast as the driver allows.
    while (!stop.load(std::memory_order_relaxed)) {
        InputSnapshot in = input.read();

        double now = glfwGetTime();
        if (!in.space_held) angle += (now - prev) * 0.9;
        prev = now;

        glViewport(0, 0, in.fb_width, in.fb_height);
        glClearColor(0.05f, 0.05f, 0.06f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        mat4 model = rotate({0.5f, 1.f, 0.25f}, static_cast<float>(angle));
        mat4 view = lookAt({2.2f, 1.6f, 2.6f}, {0.f, 0.f, 0.f}, {0.f, 1.f, 0.f});
        mat4 proj = perspective(1.0471976f,
                                static_cast<float>(in.fb_width) / static_cast<float>(in.fb_height),
                                0.1f, 100.f);
        mat4 mvp = proj * view * model;
        glUseProgram(program);
        glUniformMatrix4fv(mvpLoc, 1, GL_FALSE, mvp.m);  // column-major: no transpose
        glBindVertexArray(vao);
        glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, nullptr);
        glfwSwapBuffers(window);  // safe: this thread holds the context
    }

    // GL teardown on the owning thread, before the main thread joins us.
    glDeleteVertexArrays(1, &vao);
    glDeleteBuffers(1, &vbo);
    glDeleteBuffers(1, &ebo);
    glDeleteProgram(program);
    glfwMakeContextCurrent(nullptr);
}
```

- [ ] **Step 3: Rewrite `src/main.cpp`** — full replacement:

```cpp
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
```

- [ ] **Step 4: Update `CMakeLists.txt`** — replace the `cube` target block with:

```cmake
find_package(Threads REQUIRED)

add_executable(cube src/main.cpp src/renderer.cpp)
target_include_directories(cube PRIVATE ${CMAKE_SOURCE_DIR}/extern ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(cube PRIVATE glfw Threads::Threads)
```

- [ ] **Step 5: Reconfigure, build, run tests**

Run: `& "C:\Program Files\CMake\bin\cmake.exe" -B build`, build, ctest.
Expected: clean build (watch for a `GLAD_GL_IMPLEMENTATION` redefinition error — it must exist only in `renderer.cpp`), `2/2` tests pass.

- [ ] **Step 6: Run and verify the threaded cube**

Launch, wait 3 s, screenshot (Phase 1 harness), close via `CloseMainWindow`, check exit 0, empty stderr, cube visible and rotating (screenshot differs from the Phase 2 finals is not required — just a cube present).

- [ ] **Step 7: Commit**

```powershell
git add src/renderer.h src/renderer.cpp src/main.cpp CMakeLists.txt
git commit -m "feat: render thread owns GL context; main thread polls and publishes input"
```

---

### Task 3: Cross-thread handoff verification (no code changes)

**Files:** none — verification only.

- [ ] **Step 1: Verify SPACE (published by main, consumed by renderer) freezes the cube**

Launch `cube.exe`, focus its window (`SetForegroundWindow`), then with P/Invoke `keybd_event` (0x20 = VK_SPACE, flag 2 = KEYEVENTF_KEYUP):

```powershell
Add-Type @'
using System;
using System.Runtime.InteropServices;
public class Kbd {
    [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
}
'@
# hold SPACE
[Kbd]::keybd_event(0x20, 0, 0, [UIntPtr]::Zero)
Start-Sleep -Milliseconds 300
# snapshot A, wait 2 s, snapshot B (cube frozen => A ~ B)
# release SPACE
[Kbd]::keybd_event(0x20, 0, 2, [UIntPtr]::Zero)
# wait 2 s, snapshot C (rotation resumed => C differs from B)
```

Use the Phase 1 screenshot harness for A/B/C; view all three with the Read tool. Expected: A and B show the same orientation (frozen while SPACE held); C differs (resumed). If focus is flaky and injection doesn't land, retry once; if still flaky, fall back to asking the user to hold SPACE and confirm.

- [ ] **Step 2: Ten rapid open/close cycles (shutdown-race shakeout)**

```powershell
foreach ($i in 1..10) {
    $p = Start-Process -FilePath ".\build\Release\cube.exe" -PassThru
    Start-Sleep -Milliseconds 700
    $p.CloseMainWindow() | Out-Null
    if (-not $p.WaitForExit(5000)) { "HANG on cycle $i"; $p.Kill() }
    "cycle ${i}: exit=$($p.ExitCode)"
}
```

Expected: ten lines `exit=0`, no HANG. A crash-on-exit or hang here means the shutdown ordering is wrong — stop and fix before proceeding.

---

### Task 4: Five-minute soak, docs, push, CI

**Files:**
- Modify: `CLAUDE.md`
- Create: `docs/superpowers/plans/2026-07-26-phase-3-render-thread.md` (copy of this plan)

- [ ] **Step 1: Start the 5-minute soak (background)**

Launch `cube.exe`; leave it running 5 minutes (background timer), then `CloseMainWindow`, assert exit 0 and empty stderr. This is the phase's done-condition ("runs for five minutes without a crash on exit"). Do Steps 2–3 while it runs; do not push until it passes.

- [ ] **Step 2: Update `CLAUDE.md`** — phase status becomes Phases 0–3 complete; document: `src/main.cpp` = events/publish/shutdown only (never touches GL), `src/renderer.cpp` = context + all GL objects (`GLAD_GL_IMPLEMENTATION` lives here), `src/input_state.h` = snapshot channel (mutex-only until Phase 4), framebuffer size travels in the snapshot because GLFW size queries are main-thread-only, and the exact shutdown order. Note the payoff: decoupled rates mean a frame-rate cap no longer sets input latency.

- [ ] **Step 3: Copy plan into repo**

```powershell
Copy-Item "C:\Users\tiffm\.claude\plans\do-phase-0-scaffolding-lexical-sunset.md" "docs\superpowers\plans\2026-07-26-phase-3-render-thread.md"
```

- [ ] **Step 4: After soak passes — commit, push, watch CI**

```powershell
git add CLAUDE.md docs/
git commit -m "docs: record Phase 3 complete"
git push origin main
```

Poll `https://api.github.com/repos/tiffany-mares/OpenGL-Renderer/actions/runs?per_page=1` until completed; expect success on both matrix jobs (build + both test suites).

---

## Verification (end-to-end)

1. `ctest`: `2/2` (mat4 + input) locally and in CI on both OSes.
2. SPACE freeze test: screenshots A≈B (held) and B≠C (released) prove main-thread publishes reach the render thread.
3. Ten open/close cycles, all exit 0, no hangs — shutdown ordering holds under repetition.
4. Five-minute soak: exit 0, empty stderr — the phase's done-condition.
5. CI green on windows-latest and ubuntu-latest; commits pushed.
