# Phase 4: The Input Handoff — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Three input-handoff backends (mutex, atomic bitmask, seqlock) behind one interface, selectable with `--input=`, with arrow keys rotating the cube through all three.

**Architecture:** `InputChannel` becomes an abstract base with `publish`/`read`/`name`; `MutexChannel` (refactor of today's class), `BitmaskChannel` (`std::atomic<uint32_t>`, one bit per key, `fetch_or`/`fetch_and` release, single acquire load per frame), and `SeqlockChannel` (odd/even sequence counter, writer never blocks, reader retries; the payload copy is a deliberate, documented C++ data race). The framebuffer size leaves the input payload and moves into a dedicated packed `std::atomic<uint64_t>` side channel — the bitmask backend has only 32 bits and cannot carry it, and window size is window state, not input. `InputSnapshot` becomes `{uint32_t keys; float mouse_dx, mouse_dy; uint64_t publish_ns}` — deliberately bigger than a word so the seqlock has a real job, and `publish_ns` is the reason the bitmask alone is not the final answer (Phase 6 needs it for end-to-end latency).

**Tech Stack:** C++20, `<atomic>`/`<mutex>`/`<thread>`, GLFW 3.4, existing hand-rolled `mat4`, ctest.

## Context

Phases 0–3 are done: a threaded renderer where `src/main.cpp` owns the window/events and publishes a mutex-guarded `InputSnapshot` at ~1 kHz, and `src/renderer.cpp` (`render_thread_main`) owns the GL context and reads the snapshot each frame (SPACE pauses rotation — the Phase 3 handoff proof). Phase 4 is the project's second headline system: three interchangeable handoff backends behind one interface. Done when arrow keys rotate the cube through all three backends, selectable by command-line flag; the README must honestly name the seqlock's formal data race.

Repo: `C:\Users\tiffm\Desktop\OpenGL Renderer\OpenGL-Renderer` (parent folder has a space — always quote paths). Remote: `github.com/tiffany-mares/OpenGL-Renderer`.

## Global Constraints

- C++20; no new dependencies. No SDL, no SFML, no glm, no engine. (From CLAUDE.md.)
- Thread split is non-negotiable: main thread = window + `glfwPollEvents` + input polling, never touches GL; render thread owns context and all GL objects. Shutdown order: signal stop → render thread deletes GL objects, detaches context → join → `glfwDestroyWindow` on main.
- GLFW input queries (`glfwGetKey`, `glfwGetCursorPos`, `glfwGetFramebufferSize`) stay on the main thread.
- Bitmask backend: `std::atomic<uint32_t>`, `fetch_or`/`fetch_and` with `memory_order_release` on the writer, a **single** `memory_order_acquire` load per reader frame. Genuinely lock-free, no torn reads, no ABA.
- Seqlock: writer never blocks; reader retries on odd or changed sequence. The unsynchronized payload copy is formally a data race under the C++ memory model — **document it, don't hide it** (README + code comment).
- `uint64_t publish_ns` stays in the payload (mutex and seqlock carry it; bitmask structurally cannot — that asymmetry is a feature to document, not a bug).
- Mutex backend is the default when no flag is given.
- Commit after each task; push at the end and watch CI to green.
- Build/test commands (on this machine `cmake` may need its full path `"C:\Program Files\CMake\bin\cmake.exe"`; generator is VS 16 2019):

```powershell
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

- Manual-run scripting caveat (from CLAUDE.md): `Process.CloseMainWindow()` silently returns false during the first few hundred ms of startup — wait ~2 s before sending close, or it looks like a shutdown hang.

## File Structure

- `src/input_state.h` — **rewritten.** Key-bit constants, `InputSnapshot` v2, abstract `InputChannel`, `MutexChannel`, `BitmaskChannel`, `SeqlockChannel`, `make_input_channel` factory, `FramebufferSize` side channel. Header-only, matching `mat4.h` style; all three backends are small enough that one focused header beats four fragments.
- `src/main.cpp` — CLI flag parsing, arrow/space key + cursor-delta polling, publishes via the chosen backend; publishes framebuffer size via `FramebufferSize`.
- `src/renderer.h` / `src/renderer.cpp` — `render_thread_main` gains a `const FramebufferSize&` parameter; consumes `keys` bits: SPACE pauses spin, arrows add yaw/pitch.
- `tests/input_test.cpp` — extended: shared roundtrip suite run against mutex and seqlock, bitmask-specific tests, seqlock torn-read stress test, factory tests, `FramebufferSize` roundtrip.
- `CMakeLists.txt` — `input_tests` links `Threads::Threads` (stress test spawns a thread).
- `README.md` — build/run instructions, backend table, and the honest seqlock data-race section.
- `docs/superpowers/plans/2026-07-26-phase-4-input-handoff.md` — copy of this plan (superpowers convention).
- `CLAUDE.md` — record Phase 4 complete.

---

### Task 1: Interface + MutexChannel refactor + FramebufferSize side channel

Behavior-parity refactor: same app behavior (SPACE pauses, resize works), new shapes underneath. This is the spec's "build the mutex baseline first."

**Files:**
- Modify: `src/input_state.h` (full rewrite below — Task 1 writes everything except `BitmaskChannel`, `SeqlockChannel`, and the factory, which Tasks 3–5 add)
- Modify: `tests/input_test.cpp`
- Modify: `src/main.cpp`, `src/renderer.h`, `src/renderer.cpp`
- Modify: `CMakeLists.txt`
- Create: `docs/superpowers/plans/2026-07-26-phase-4-input-handoff.md`

**Interfaces:**
- Produces (later tasks rely on these exact names):
  - `struct InputSnapshot { uint32_t keys; float mouse_dx, mouse_dy; uint64_t publish_ns; }` (all zero-default)
  - Key bits: `kKeySpace = 1u<<0`, `kKeyLeft = 1u<<1`, `kKeyRight = 1u<<2`, `kKeyUp = 1u<<3`, `kKeyDown = 1u<<4`
  - `class InputChannel { virtual void publish(const InputSnapshot&); virtual InputSnapshot read() const; virtual const char* name() const; }` (pure virtual + virtual default dtor)
  - `class MutexChannel final : public InputChannel` — `name()` returns `"mutex"`
  - `class FramebufferSize { void store(int w, int h); void load(int& w, int& h) const; }` — defaults 1280×720
  - `void render_thread_main(GLFWwindow*, const InputChannel&, const FramebufferSize&, const std::atomic<bool>& stop, std::atomic<bool>& failed)`

- [ ] **Step 1: Rewrite `tests/input_test.cpp` for the new shapes (failing first)**

Replace the whole file with:

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

// Roundtrip suite shared by every backend that carries the full payload
// (mutex now; seqlock in a later task).
static void full_payload_suite(InputChannel& ch, const char* label) {
    std::fprintf(stderr, "-- %s\n", label);
    InputSnapshot d = ch.read();  // readable before any publish
    expect(d.keys == 0, "default keys zero");
    expect(d.publish_ns == 0, "default publish_ns zero");

    InputSnapshot s;
    s.keys = kKeySpace | kKeyLeft;
    s.mouse_dx = 3.5f;
    s.mouse_dy = -2.f;
    s.publish_ns = 42;
    ch.publish(s);
    InputSnapshot r = ch.read();
    expect(r.keys == (kKeySpace | kKeyLeft), "keys roundtrip");
    expect(r.mouse_dx == 3.5f && r.mouse_dy == -2.f, "mouse delta roundtrip");
    expect(r.publish_ns == 42, "publish_ns roundtrip");

    // read() returns a copy: mutating it must not affect the channel.
    r.keys = 0;
    expect(ch.read().keys == (kKeySpace | kKeyLeft), "read returns independent copy");
}

int main() {
    {
        MutexChannel ch;
        full_payload_suite(ch, "mutex");
        expect(std::string_view(ch.name()) == "mutex", "mutex name");
    }

    {
        FramebufferSize fb;
        int w = 0, h = 0;
        fb.load(w, h);
        expect(w == 1280 && h == 720, "fb default size");
        fb.store(800, 600);
        fb.load(w, h);
        expect(w == 800 && h == 600, "fb roundtrip");
    }

    if (g_failures) {
        std::fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("all input tests passed\n");
    return 0;
}
```

(Add `#include <string_view>` at the top with the other includes.)

- [ ] **Step 2: Build only the test target to verify it fails**

```powershell
cmake --build build --config Release --target input_tests
```

Expected: **compile errors** (`kKeySpace` undeclared, no `MutexChannel`, `InputSnapshot` has no member `keys`). That is this step's "failing test."

- [ ] **Step 3: Rewrite `src/input_state.h`**

Replace the whole file with:

```cpp
#pragma once
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
```

(Add `#include <atomic>` next to `<mutex>`.)

- [ ] **Step 4: Add Threads to `input_tests` in `CMakeLists.txt`** (needed by the Task 4 stress test; do it now so the test target's link line is settled)

```cmake
add_executable(input_tests tests/input_test.cpp)
target_include_directories(input_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(input_tests PRIVATE Threads::Threads)
add_test(NAME input_tests COMMAND input_tests)
```

- [ ] **Step 5: Run the tests**

```powershell
cmake --build build --config Release --target input_tests
ctest --test-dir build -C Release -R input_tests --output-on-failure
```

Expected: PASS, `all input tests passed`. (`cube` is still broken — next steps fix it.)

- [ ] **Step 6: Adapt `src/renderer.h`**

Replace the declaration block with:

```cpp
#pragma once
#include <atomic>

struct GLFWwindow;
class InputChannel;
class FramebufferSize;

// Body of the render thread. Owns the GL context and every GL object:
// makes the context current, loads GLAD, sets swap interval 0, builds
// shaders/buffers, draws until `stop` is set, then deletes all GL objects
// and detaches the context before returning (shutdown ordering requires
// GL teardown to happen on this thread, before the join).
// On init failure: sets `failed`, requests window close, returns early.
void render_thread_main(GLFWwindow* window, const InputChannel& input,
                        const FramebufferSize& fb,
                        const std::atomic<bool>& stop, std::atomic<bool>& failed);
```

- [ ] **Step 7: Adapt `src/renderer.cpp`**

Signature to match Step 6, then inside the loop replace the snapshot/viewport code. The loop body becomes:

```cpp
    while (!stop.load(std::memory_order_relaxed)) {
        InputSnapshot in = input.read();
        int fb_w = 0, fb_h = 0;
        fb.load(fb_w, fb_h);

        double now = glfwGetTime();
        if (!(in.keys & kKeySpace)) angle += (now - prev) * 0.9;
        prev = now;

        glViewport(0, 0, fb_w, fb_h);
        glClearColor(0.05f, 0.05f, 0.06f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        mat4 model = rotate({0.5f, 1.f, 0.25f}, static_cast<float>(angle));
        mat4 view = lookAt({2.2f, 1.6f, 2.6f}, {0.f, 0.f, 0.f}, {0.f, 1.f, 0.f});
        mat4 proj = perspective(1.0471976f,
                                static_cast<float>(fb_w) / static_cast<float>(fb_h),
                                0.1f, 100.f);
        mat4 mvp = proj * view * model;
        glUseProgram(program);
        glUniformMatrix4fv(mvpLoc, 1, GL_FALSE, mvp.m);  // column-major: no transpose
        glBindVertexArray(vao);
        glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, nullptr);
        glfwSwapBuffers(window);  // safe: this thread holds the context
    }
```

- [ ] **Step 8: Adapt `src/main.cpp`**

Channel construction and the poll loop change; the concrete type is `MutexChannel` for now (the factory arrives in Task 5). Replace the channel setup and loop body:

```cpp
    MutexChannel input;
    FramebufferSize fb;
    {
        int w = 0, h = 0;
        glfwGetFramebufferSize(window, &w, &h);
        fb.store(w, h);
    }

    std::atomic<bool> stop{false};
    std::atomic<bool> render_failed{false};
    std::thread render_thread(render_thread_main, window, std::cref(input),
                              std::cref(fb), std::cref(stop), std::ref(render_failed));

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
            glfwSetWindowShouldClose(window, GLFW_TRUE);

        int w = 0, h = 0;
        glfwGetFramebufferSize(window, &w, &h);
        fb.store(w, h);

        InputSnapshot s;
        if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS) s.keys |= kKeySpace;
        s.publish_ns = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch())
                .count());
        input.publish(s);

        // ~1000 Hz poll cadence. sleep_for's real resolution on stock
        // Windows is the scheduler tick; the honest pacer is Phase 5.
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
```

- [ ] **Step 9: Full build, tests, and behavior-parity smoke run**

```powershell
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
$p = Start-Process -PassThru .\build\Release\cube.exe
Start-Sleep -Seconds 2
$p.CloseMainWindow() | Out-Null
$p.WaitForExit(5000)
$p.ExitCode
```

Expected: clean build, both test suites pass, exit code `0`. Manually confirm (or ask the human partner): cube spins, SPACE pauses, resize still tracks.

- [ ] **Step 10: Copy this plan into the repo and commit**

Copy this plan file to `docs/superpowers/plans/2026-07-26-phase-4-input-handoff.md`, then:

```powershell
git add src/input_state.h src/renderer.h src/renderer.cpp src/main.cpp tests/input_test.cpp CMakeLists.txt docs/
git commit -m "refactor: input backend interface with mutex baseline; framebuffer size sidechannel"
```

---

### Task 2: Arrow keys rotate the cube

**Files:**
- Modify: `src/main.cpp` (publish arrow bits + mouse delta)
- Modify: `src/renderer.cpp` (integrate yaw/pitch)

**Interfaces:**
- Consumes: `kKeyLeft/kKeyRight/kKeyUp/kKeyDown`, `InputSnapshot.keys`, `mouse_dx/mouse_dy` (Task 1).
- Produces: nothing new for later tasks — this is the behavior the done-condition tests.

No unit test: this is windowing + GPU behavior, verified by observation like Phases 0–1 (project convention: unit tests cover logic, e.g. `mat4` and the channels; GL behavior is verified manually).

- [ ] **Step 1: Publish arrows and mouse delta in `src/main.cpp`**

Before the loop (after `fb.store`):

```cpp
    double mx_prev = 0.0, my_prev = 0.0;
    glfwGetCursorPos(window, &mx_prev, &my_prev);
```

In the loop, replace the snapshot-filling block:

```cpp
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
        input.publish(s);
```

- [ ] **Step 2: Integrate yaw/pitch in `src/renderer.cpp`**

Replace the angle state and the model-matrix code:

```cpp
    // Rotation: the wall-clock spin from Phase 3 (SPACE pauses it) plus
    // manual yaw/pitch from the arrow keys — the visible proof that key
    // state crosses the thread boundary through whichever backend is live.
    double angle = 0.0, yaw = 0.0, pitch = 0.0;
    double prev = glfwGetTime();
```

and in the loop, after `InputSnapshot in = input.read();` and the `now` computation:

```cpp
        double now = glfwGetTime();
        const double dt = now - prev;
        if (!(in.keys & kKeySpace)) angle += dt * 0.9;
        constexpr double kManualRate = 2.2;  // rad/s while an arrow is held
        yaw   += dt * kManualRate * (((in.keys & kKeyRight) ? 1 : 0) - ((in.keys & kKeyLeft) ? 1 : 0));
        pitch += dt * kManualRate * (((in.keys & kKeyDown) ? 1 : 0) - ((in.keys & kKeyUp) ? 1 : 0));
        prev = now;
```

and the model matrix:

```cpp
        mat4 model = rotate({1.f, 0.f, 0.f}, static_cast<float>(pitch)) *
                     rotate({0.f, 1.f, 0.f}, static_cast<float>(yaw)) *
                     rotate({0.5f, 1.f, 0.25f}, static_cast<float>(angle));
```

- [ ] **Step 3: Build and verify by observation**

```powershell
cmake --build build --config Release
.\build\Release\cube.exe
```

Expected: LEFT/RIGHT yaw the cube, UP/DOWN pitch it, SPACE still pauses the spin, ESC exits cleanly. If an arrow's direction feels inverted, flip the sign on that term — it's a taste call, note it in the commit.

- [ ] **Step 4: Commit**

```powershell
git add src/main.cpp src/renderer.cpp
git commit -m "feat: arrow keys rotate the cube via published key bitmask"
```

---

### Task 3: Atomic bitmask backend

**Files:**
- Modify: `src/input_state.h` (add `BitmaskChannel` after `MutexChannel`)
- Modify: `tests/input_test.cpp`

**Interfaces:**
- Consumes: `InputChannel`, `InputSnapshot`, key bits (Task 1).
- Produces: `class BitmaskChannel final : public InputChannel` — `name()` returns `"bitmask"`; `read()` carries **only** `keys` (mouse delta and `publish_ns` come back zero).

- [ ] **Step 1: Write the failing tests**

Add to `tests/input_test.cpp` (a new function, called from `main` after the mutex block):

```cpp
static void bitmask_suite() {
    std::fprintf(stderr, "-- bitmask\n");
    BitmaskChannel ch;
    expect(std::string_view(ch.name()) == "bitmask", "bitmask name");
    expect(ch.read().keys == 0, "bitmask default keys zero");

    InputSnapshot s;
    s.keys = kKeySpace | kKeyLeft;
    s.mouse_dx = 3.5f;
    s.publish_ns = 42;
    ch.publish(s);
    InputSnapshot r = ch.read();
    expect(r.keys == (kKeySpace | kKeyLeft), "bitmask keys roundtrip");
    // Structural limitation, on purpose: 32 bits cannot carry the rest.
    expect(r.mouse_dx == 0.f && r.mouse_dy == 0.f, "bitmask drops mouse delta");
    expect(r.publish_ns == 0, "bitmask drops publish_ns");

    // Edge transitions across publishes must both set and clear bits.
    s = InputSnapshot{};
    s.keys = kKeySpace | kKeyRight;  // Left released, Right pressed
    ch.publish(s);
    expect(ch.read().keys == (kKeySpace | kKeyRight), "bitmask clears released keys");
    s = InputSnapshot{};             // everything released
    ch.publish(s);
    expect(ch.read().keys == 0, "bitmask clears to zero");
}
```

- [ ] **Step 2: Build the test target to verify it fails**

```powershell
cmake --build build --config Release --target input_tests
```

Expected: compile error, `BitmaskChannel` not declared.

- [ ] **Step 3: Implement `BitmaskChannel` in `src/input_state.h`**

Insert after `MutexChannel`:

```cpp
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
```

- [ ] **Step 4: Run the tests**

```powershell
cmake --build build --config Release --target input_tests
ctest --test-dir build -C Release -R input_tests --output-on-failure
```

Expected: PASS.

- [ ] **Step 5: Commit**

```powershell
git add src/input_state.h tests/input_test.cpp
git commit -m "feat: lock-free atomic bitmask input backend"
```

---

### Task 4: Seqlock backend

**Files:**
- Modify: `src/input_state.h` (add `SeqlockChannel` after `BitmaskChannel`)
- Modify: `tests/input_test.cpp`

**Interfaces:**
- Consumes: `InputChannel`, `InputSnapshot` (Task 1).
- Produces: `class SeqlockChannel final : public InputChannel` — `name()` returns `"seqlock"`; carries the full payload.

- [ ] **Step 1: Write the failing tests**

Add to `tests/input_test.cpp` (`#include <atomic>` and `#include <thread>` at the top). The roundtrip reuses `full_payload_suite`; the stress test is the real content — a writer publishes snapshots whose fields are all derived from one counter, and a concurrent reader asserts every snapshot it sees is internally consistent. A torn read breaks the derivation.

```cpp
static void seqlock_stress() {
    SeqlockChannel ch;
    std::atomic<bool> done{false};
    std::thread writer([&] {
        for (uint32_t i = 1; i <= 200000; ++i) {
            InputSnapshot s;
            s.keys = i;
            s.mouse_dx = static_cast<float>(i & 0xFFFF);
            s.mouse_dy = -static_cast<float>(i & 0xFFFF);
            s.publish_ns = uint64_t(i) * 1000003u;
            ch.publish(s);
        }
        done.store(true);
    });
    while (!done.load()) {
        InputSnapshot r = ch.read();
        const bool consistent =
            r.publish_ns == uint64_t(r.keys) * 1000003u &&
            r.mouse_dx == static_cast<float>(r.keys & 0xFFFF) &&
            r.mouse_dy == -static_cast<float>(r.keys & 0xFFFF);
        expect(consistent, "seqlock stress: torn snapshot");
        if (g_failures > 20) break;  // don't flood stderr if it's broken
    }
    writer.join();
}
```

And in `main`, after the bitmask block:

```cpp
    {
        SeqlockChannel ch;
        full_payload_suite(ch, "seqlock");
        expect(std::string_view(ch.name()) == "seqlock", "seqlock name");
        seqlock_stress();
    }
```

- [ ] **Step 2: Build the test target to verify it fails**

```powershell
cmake --build build --config Release --target input_tests
```

Expected: compile error, `SeqlockChannel` not declared.

- [ ] **Step 3: Implement `SeqlockChannel` in `src/input_state.h`**

Insert after `BitmaskChannel`:

```cpp
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
```

- [ ] **Step 4: Run the tests**

```powershell
cmake --build build --config Release --target input_tests
ctest --test-dir build -C Release -R input_tests --output-on-failure
```

Expected: PASS (stress test takes well under a second).

- [ ] **Step 5: Commit**

```powershell
git add src/input_state.h tests/input_test.cpp
git commit -m "feat: seqlock input backend with documented deliberate data race"
```

---

### Task 5: Factory + `--input=` flag, all three backends live

**Files:**
- Modify: `src/input_state.h` (factory at the bottom)
- Modify: `tests/input_test.cpp`
- Modify: `src/main.cpp`

**Interfaces:**
- Consumes: all three channel classes (Tasks 1, 3, 4).
- Produces: `std::unique_ptr<InputChannel> make_input_channel(std::string_view name)` — accepts `"mutex"`, `"bitmask"`, `"seqlock"`; returns `nullptr` for anything else.

- [ ] **Step 1: Write the failing factory tests**

Add to `tests/input_test.cpp` `main` (before the failure check):

```cpp
    expect(make_input_channel("mutex") && std::string_view(make_input_channel("mutex")->name()) == "mutex",
           "factory mutex");
    expect(make_input_channel("bitmask") && std::string_view(make_input_channel("bitmask")->name()) == "bitmask",
           "factory bitmask");
    expect(make_input_channel("seqlock") && std::string_view(make_input_channel("seqlock")->name()) == "seqlock",
           "factory seqlock");
    expect(make_input_channel("bogus") == nullptr, "factory rejects unknown");
```

- [ ] **Step 2: Build the test target to verify it fails**

```powershell
cmake --build build --config Release --target input_tests
```

Expected: compile error, `make_input_channel` not declared.

- [ ] **Step 3: Implement the factory in `src/input_state.h`**

At the top add `#include <memory>` and `#include <string_view>`; at the bottom:

```cpp
// Backend selection for main(): flag string in, channel out.
inline std::unique_ptr<InputChannel> make_input_channel(std::string_view name) {
    if (name == "mutex") return std::make_unique<MutexChannel>();
    if (name == "bitmask") return std::make_unique<BitmaskChannel>();
    if (name == "seqlock") return std::make_unique<SeqlockChannel>();
    return nullptr;
}
```

- [ ] **Step 4: Run the tests**

```powershell
cmake --build build --config Release --target input_tests
ctest --test-dir build -C Release -R input_tests --output-on-failure
```

Expected: PASS.

- [ ] **Step 5: Wire the flag in `src/main.cpp`**

`main()` gains argc/argv and parsing; the channel becomes factory-made. Change the signature to `int main(int argc, char** argv)`, add `#include <memory>` and `#include <string_view>`, and at the top of `main`:

```cpp
    const char* backend = "mutex";  // the baseline is the default
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg.rfind("--input=", 0) == 0) {
            backend = argv[i] + 8;
        } else {
            std::fprintf(stderr, "usage: cube [--input=mutex|bitmask|seqlock]\n");
            return EXIT_FAILURE;
        }
    }
    std::unique_ptr<InputChannel> input = make_input_channel(backend);
    if (!input) {
        std::fprintf(stderr, "unknown input backend '%s' (mutex|bitmask|seqlock)\n", backend);
        return EXIT_FAILURE;
    }
    std::printf("input backend: %s\n", input->name());
```

Replace `MutexChannel input;` (delete it — the unique_ptr above replaces it) and pass the dereferenced pointer to the thread:

```cpp
    std::thread render_thread(render_thread_main, window, std::cref(*input),
                              std::cref(fb), std::cref(stop), std::ref(render_failed));
```

Note: `glfwInit` currently happens before the channel exists; keep flag parsing **before** `glfwInit` so a bad flag exits without touching the window system.

- [ ] **Step 6: Full build and scripted smoke of all three backends + the error path**

```powershell
cmake --build build --config Release
foreach ($b in 'mutex','bitmask','seqlock') {
  $p = Start-Process -PassThru .\build\Release\cube.exe -ArgumentList "--input=$b"
  Start-Sleep -Seconds 2
  $p.CloseMainWindow() | Out-Null
  $p.WaitForExit(5000)
  "$b -> exit $($p.ExitCode)"
}
.\build\Release\cube.exe --input=bogus; "bogus -> exit $LASTEXITCODE"
```

Expected: `mutex -> exit 0`, `bitmask -> exit 0`, `seqlock -> exit 0`, `bogus -> exit 1` with the unknown-backend message.

- [ ] **Step 7: Human verification (the done-condition)**

Ask the human partner to run each of:

```powershell
.\build\Release\cube.exe --input=mutex
.\build\Release\cube.exe --input=bitmask
.\build\Release\cube.exe --input=seqlock
```

and confirm arrow keys rotate the cube (and SPACE pauses, ESC exits) identically in all three.

- [ ] **Step 8: Commit**

```powershell
git add src/input_state.h src/main.cpp tests/input_test.cpp
git commit -m "feat: --input flag selects handoff backend (mutex|bitmask|seqlock)"
```

---

### Task 6: README honesty section, CLAUDE.md, push, CI

**Files:**
- Modify: `README.md` (currently just a title line)
- Modify: `CLAUDE.md`

**Interfaces:** none — documentation and delivery.

- [ ] **Step 1: Write `README.md`**

Replace the file with:

```markdown
# OpenGL-Renderer

A C++20 threaded OpenGL renderer whose real subject is three systems problems:
thread-affine graphics contexts, a lock-free input handoff, and precise frame
pacing on general-purpose OSes. The rotating cube is the demo, not the point.

## Build & run

    cmake -B build
    cmake --build build --config Release
    build/Release/cube.exe [--input=mutex|bitmask|seqlock]   # build/cube on Linux
    ctest --test-dir build -C Release --output-on-failure

Arrow keys rotate the cube, SPACE pauses the spin, ESC exits.

## Input handoff backends (`--input=`, default `mutex`)

The main thread polls input at ~1 kHz and publishes a snapshot; the render
thread consumes it once per frame. Three interchangeable backends sit behind
one interface (`src/input_state.h`):

| flag      | mechanism                                                        | carries                              |
|-----------|------------------------------------------------------------------|--------------------------------------|
| `mutex`   | `std::mutex` around a small POD — the baseline                   | keys + mouse delta + `publish_ns`    |
| `bitmask` | `std::atomic<uint32_t>`, one bit per key; writer `fetch_or`/`fetch_and` (release), reader one acquire load per frame | keys only |
| `seqlock` | sequence counter; writer never blocks, reader retries on odd/changed sequence | keys + mouse delta + `publish_ns` |

The payload carries a `uint64_t publish_ns` timestamp so end-to-end input
latency (consume time − publish time) can be measured. That field is why the
bitmask alone is not the final answer: it is genuinely lock-free with no torn
reads and no ABA, but 32 bits cannot carry a timestamp.

### The seqlock's data race, named

The seqlock reader copies the payload without synchronization while the writer
may be mid-store. Under the C++ memory model that is formally a data race —
undefined behavior. The sequence-number retry discards every torn copy on real
hardware, the fences around the copy are the standard practical construction
(Boehm, *Can seqlocks get along with programming language memory models?*),
and every shipping engine contains something shaped like this. It works on
every real CPU; it is still bending a rule of the abstract machine. Naming the
rule being bent is the signal — pretending there isn't one would be the bug.
```

- [ ] **Step 2: Update `CLAUDE.md`**

In the Project paragraph: change “**Phases 0–3 are complete as of 2026-07-26.**” to “**Phases 0–4 are complete as of 2026-07-26.**”, and append after the Phase 3 sentences (before “Phases 4+ do not exist yet”):

> Phase 4: three handoff backends behind the abstract `InputChannel` in `src/input_state.h` — `MutexChannel` (default), `BitmaskChannel` (`atomic<uint32_t>`, keys only: no room for `publish_ns`, which is why it isn't the final answer), `SeqlockChannel` (full payload; the reader's payload copy is a deliberate, documented C++ data race — see README). Selected at startup via `--input=mutex|bitmask|seqlock` (`make_input_channel`); framebuffer size moved out of the payload into the packed-atomic `FramebufferSize` side channel; arrow keys yaw/pitch the cube through whichever backend is live. The seqlock stress test in `tests/input_test.cpp` derives every payload field from one counter and asserts consistency under a concurrent writer.

Then update the trailing sentence to “Phases 5+ do not exist yet — update this file as they land.”

Also update the run line in the Build section to `build/Release/cube.exe [--input=mutex|bitmask|seqlock]`.

- [ ] **Step 3: Commit and push**

```powershell
git add README.md CLAUDE.md
git commit -m "docs: input-backend README with named seqlock data race; record Phase 4 complete"
git push origin main
```

- [ ] **Step 4: Watch CI to green**

```powershell
gh run watch --repo tiffany-mares/OpenGL-Renderer
```

Expected: both matrix legs (windows-latest, ubuntu-latest) build and pass ctest, including the new backend and stress tests. If the Linux leg flakes on the stress test, that is signal, not noise — investigate before rerunning.

---

## Verification (end-to-end)

1. `ctest --test-dir build -C Release --output-on-failure` — mat4 tests, all-backend roundtrips, bitmask edge tests, seqlock stress, factory tests: all pass.
2. `.\build\Release\cube.exe --input=<each of mutex|bitmask|seqlock>` — arrow keys rotate the cube, SPACE pauses, ESC exits with code 0, identically across backends (this is the phase's done-condition; needs a human at the keyboard).
3. `.\build\Release\cube.exe --input=bogus` exits 1 with a usage message; no window appears.
4. README contains the backend table and the data-race section; CLAUDE.md records Phase 4.
5. CI green on both windows-latest and ubuntu-latest; `git log --oneline` shows one commit per task, all pushed.
