# Phase 8: The Live Link — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A clickable GitHub Pages URL serving the cube compiled to WebAssembly — single-threaded and browser-paced by explicit decision, with the page itself stating what deliberately does not port (the pacer, the render thread).

**Architecture:** Extract the shared GL scene out of `render_thread_main` into `SceneGL`/`scene_init`/`scene_draw`/`scene_destroy` (pure code motion for native; the 15-suite ctest run is the regression gate), then add an `#ifdef __EMSCRIPTEN__` path: ES 3.00 shaders, GLES3 headers instead of glad, a `run_web` entry whose frame callback is driven by `emscripten_set_main_loop(web_frame, 0, 1)` (requestAnimationFrame owns the clock), and a separate tiny web `main()`. A stdlib `web/build.py` runs the spec's emcc line; a Pages workflow builds with a pinned emsdk and deploys `dist/`.

**Tech Stack:** C++20, Emscripten (`-sUSE_GLFW=3`, WebGL2), GitHub Pages via Actions, Python stdlib driver.

## Context

Phases 0–7 are complete and pushed; the README is the artifact. Phase 8 adds the live link the README can point to. The user's spec is explicit about the platform boundary: the frame pacer is meaningless in a browser (no high-res sleep, no busy-spin on the main thread, requestAnimationFrame locked to display refresh, `performance.now()` coarsened to ~100 µs without cross-origin isolation — the native "<200 µs" headline cannot even be measured there), and the threaded render cannot keep GLFW (Emscripten's OFFSCREEN_FRAMEBUFFER / OFFSCREENCANVAS paths support only HTML5-API or SDL2 contexts). So: single-threaded, browser-paced, and the page says so. Staying single-threaded also removes the cross-origin-isolation deployment headache entirely.

## Global Constraints

- The emcc flags are the spec's, verbatim: `-std=c++20 -O3 -sUSE_GLFW=3 -sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2 -sALLOW_MEMORY_GROWTH=1 -sEXPORTED_RUNTIME_METHODS=ccall` (only `-o` moves to the driver's `--out`; `src/*.cpp` becomes a sorted glob; `-Iextern -Isrc` added). `EXPORTED_RUNTIME_METHODS=ccall` is spec-mandated though nothing calls ccall today — keep it, with a comment saying so.
- The web build is single-threaded: no `-pthread`, no COOP/COEP headers, no render thread, no pacer in the loop. `emscripten_set_main_loop(web_frame, 0, 1)` — fps=0 (RAF), simulate_infinite_loop=1 (never returns).
- Web shaders are `#version 300 es` with the version directive as the FIRST characters of the literal (the native literals start with a newline; ES rejects that) and an explicit `precision highp float;` in the fragment stage.
- Native behavior must be unchanged: full rebuild + all 15 ctest suites green after the refactor, plus a manual native run. The scene extraction is pure code motion — the capture/workload/swap ordering inside `render_thread_main` must not change.
- The page states the boundary ("what you are not seeing") in the user's terms — pacer meaningless, threaded render doesn't port — and links back to the repo.
- Python additions are stdlib-only, house `FATAL:` style via `sys.exit` (pattern: `bench/run_plots.py`).
- emsdk version: Task 1 records the exact local `emcc --version`; `pages.yml` pins that exact version. No version drift between local and CI.
- Existing CI (`build.yml`) must stay green; the new `pages.yml` is separate (push to main + workflow_dispatch), self-enables Pages via `actions/configure-pages@v5` `enablement: true`.
- `dist/` is git-ignored; committed web artifacts are only `web/build.py` and `web/index.html`.
- Commit style: `feat:` / `docs:` / `fix:` with ` -- ` sub-clauses, matching the log.

## File map

- Create: `web/build.py`, `web/index.html`, `.github/workflows/pages.yml`, `docs/superpowers/plans/2026-07-28-phase-8-live-link.md` (copy of this plan, Task 1).
- Modify: `src/renderer.cpp` (include block, shader literals, SceneGL extraction, `#ifndef __EMSCRIPTEN__` around the native loop + workload statics, web path), `src/renderer.h` (run_web decl), `src/main.cpp` (web main / native main split), `.gitignore` (+`dist/`), `README.md` + `CLAUDE.md` (Task 4).
- Contingency only: `src/pacer.cpp` (an `#elif defined(__EMSCRIPTEN__)` branch, ONLY if the web link fails on `clock_nanosleep` — code provided in Task 1 Step 6).

---

### Task 1: The port and the build driver

**Files:**
- Modify: `src/renderer.cpp`, `src/renderer.h`, `src/main.cpp`, `.gitignore`
- Create: `web/build.py`, `docs/superpowers/plans/2026-07-28-phase-8-live-link.md` (copy this plan verbatim)
- Contingency: `src/pacer.cpp` (Step 6, only on link failure)

**Interfaces:**
- Consumes: existing `compile_shader`/`link_program`, `kVertices`/`kIndices`, `mat4` ops, `render_thread_main`'s current body (pure extraction).
- Produces: `struct SceneGL { GLuint program, vao, vbo, ebo; GLint mvpLoc; }` with `static bool scene_init(SceneGL&)`, `static void scene_draw(const SceneGL&, int fb_w, int fb_h, double angle, double yaw, double pitch)`, `static void scene_destroy(SceneGL&)` (file-static in renderer.cpp, shared by both builds — future scene changes go here, never in the loops); `int run_web(GLFWwindow*)` (renderer.h, `#ifdef __EMSCRIPTEN__`); `web/build.py --out dist` printing `wasm: js_bytes=<n> wasm_bytes=<n> out=<dir>`; the recorded emsdk version string for Task 3.

- [ ] **Step 1: Install emsdk locally and record the version** (once, outside the repo):

```
git clone https://github.com/emscripten-core/emsdk.git C:\Users\tiffm\emsdk
cd C:\Users\tiffm\emsdk
.\emsdk install latest
.\emsdk activate latest
.\emsdk_env.ps1          # per-shell activation; emcc lands on PATH
emcc --version           # RECORD the exact version -- it becomes the pages.yml pin
```

- [ ] **Step 2: renderer.cpp — include block and shader literals.** Replace the includes (lines 1–17) with:

```cpp
#include "renderer.h"

#include <cstdio>
#include <cstdint>
#include <memory>

#ifdef __EMSCRIPTEN__
// Web build: WebGL2 via the GLES3 headers — no glad (gladLoadGL is
// desktop-only; the browser provides the GL symbols at link time).
#include <emscripten/emscripten.h>
#include <GLES3/gl3.h>
#else
#define GLAD_GL_IMPLEMENTATION
#include <glad/gl.h>
#endif
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "capture.h"
#include "frame_log.h"
#include "input_state.h"
#include "mat4.h"
#include "pacer.h"
#include "workload.h"
```

Wrap the shader literals: keep the existing two `#version 330 core` literals byte-identical inside the `#else`, and add the ES variants first:

```cpp
// Transform comes from the CPU now (src/mat4.h). Colors stay `flat`: the
// provoking (last) vertex of each triangle colors the whole face — see the
// index buffer comment.
#ifdef __EMSCRIPTEN__
// GLSL ES 3.00 for WebGL2. `#version 300 es` must be the very first
// characters of the source (the desktop literals start with a newline; ES
// rejects that), and the fragment stage requires an explicit default
// precision. `flat` interpolation with last-vertex provoking is the WebGL2
// default (there is no glProvokingVertex in the spec), so the index-winding
// per-face color trick survives unchanged.
static const char* kVertexSrc = R"glsl(#version 300 es
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aColor;
uniform mat4 uMvp;
flat out vec3 vColor;

void main() {
    gl_Position = uMvp * vec4(aPos, 1.0);
    vColor = aColor;
}
)glsl";

static const char* kFragmentSrc = R"glsl(#version 300 es
precision highp float;
flat in vec3 vColor;
out vec4 FragColor;

void main() {
    FragColor = vec4(vColor, 1.0);
}
)glsl";
#else
/* existing two literals, unchanged */
#endif
```

- [ ] **Step 3: renderer.cpp — SceneGL extraction (pure code motion).** After `kIndices`, add the shared helpers; their bodies are the existing code from `render_thread_main`, moved verbatim:

```cpp
// The GL scene, shared verbatim between the native render thread and the
// Phase 8 web path: one program, one VAO/VBO/EBO, one uniform.
struct SceneGL {
    GLuint program = 0, vao = 0, vbo = 0, ebo = 0;
    GLint mvpLoc = -1;
};

// Compile+link the shaders and build the cube's buffers on the current
// context. Returns false if the program fails (compile_shader/link_program
// already printed why). Depth-test enable stays with the caller.
static bool scene_init(SceneGL& s) {
    s.program = link_program(kVertexSrc, kFragmentSrc);
    if (s.program == 0) return false;

    s.mvpLoc = glGetUniformLocation(s.program, "uMvp");

    glGenVertexArrays(1, &s.vao);
    glGenBuffers(1, &s.vbo);
    glGenBuffers(1, &s.ebo);
    glBindVertexArray(s.vao);
    glBindBuffer(GL_ARRAY_BUFFER, s.vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof kVertices, kVertices, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, s.ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof kIndices, kIndices, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    return true;
}

// Viewport + clear + MVP + draw: one frame's GL, nothing else — no capture,
// no logging, no swap. Callers own everything between draw and swap.
static void scene_draw(const SceneGL& s, int fb_w, int fb_h,
                       double angle, double yaw, double pitch) {
    glViewport(0, 0, fb_w, fb_h);
    glClearColor(0.05f, 0.05f, 0.06f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    mat4 model = rotate({1.f, 0.f, 0.f}, static_cast<float>(pitch)) *
                 rotate({0.f, 1.f, 0.f}, static_cast<float>(yaw)) *
                 rotate({0.5f, 1.f, 0.25f}, static_cast<float>(angle));
    mat4 view = lookAt({2.2f, 1.6f, 2.6f}, {0.f, 0.f, 0.f}, {0.f, 1.f, 0.f});
    mat4 proj = perspective(1.0471976f,
                            static_cast<float>(fb_w) / static_cast<float>(fb_h),
                            0.1f, 100.f);
    mat4 mvp = proj * view * model;
    glUseProgram(s.program);
    glUniformMatrix4fv(s.mvpLoc, 1, GL_FALSE, mvp.m);  // column-major: no transpose
    glBindVertexArray(s.vao);
    glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, nullptr);
}

static void scene_destroy(SceneGL& s) {
    glDeleteVertexArrays(1, &s.vao);
    glDeleteBuffers(1, &s.vbo);
    glDeleteBuffers(1, &s.ebo);
    glDeleteProgram(s.program);
    s = SceneGL{};
}
```

Then in `render_thread_main`, three replacement sites (everything else — logging, pacing, bench, capture, all prints — untouched):

1. The setup block from `GLuint program = link_program(...)` through the attrib-pointer calls becomes:

```cpp
    SceneGL scene;
    if (!scene_init(scene)) {
        failed.store(true);
        glfwMakeContextCurrent(nullptr);
        glfwSetWindowShouldClose(window, GLFW_TRUE);
        return;
    }
```

(`glEnable(GL_DEPTH_TEST);` stays where it is, after this; the GL_VERSION/GL_RENDERER prints stay before it.)

2. The frame block from `glViewport(...)` through `glDrawElements(...)` becomes one line — the workload line, the capture block, and `glfwSwapBuffers` follow unchanged in their current order:

```cpp
        scene_draw(scene, fb_w, fb_h, angle, yaw, pitch);
```

3. The teardown's four `glDelete*` calls become `scene_destroy(scene);` (followed by the unchanged `glfwMakeContextCurrent(nullptr);`).

Finally, wrap the native-only region: `#ifndef __EMSCRIPTEN__` from just before `kWorkloadIters`/`g_workload_sink` (move these two statics inside — only the native loop uses them) through the end of `render_thread_main`, closed with `#endif  // !__EMSCRIPTEN__`. (`render_thread_main` calls `gladLoadGL` and cannot compile on web.)

- [ ] **Step 4: renderer.cpp — the web path** at the bottom of the file, and its declaration in `src/renderer.h`:

```cpp
#ifdef __EMSCRIPTEN__

// Phase 8: single-threaded, browser-paced. requestAnimationFrame owns the
// frame clock, so there is no pacer; one thread means glfwGetKey directly —
// the InputChannel machinery is deliberately bypassed (no log, no capture
// either: the instrumentation measures systems that do not exist here).
struct WebState {
    GLFWwindow* window = nullptr;
    SceneGL scene{};
    double angle = 0.0, yaw = 0.0, pitch = 0.0;
    double prev = 0.0;  // glfwGetTime at the previous frame
};
static WebState g_web;

static void web_frame() {
    glfwPollEvents();
    GLFWwindow* w = g_web.window;
    const bool space = glfwGetKey(w, GLFW_KEY_SPACE) == GLFW_PRESS;
    const bool left  = glfwGetKey(w, GLFW_KEY_LEFT)  == GLFW_PRESS;
    const bool right = glfwGetKey(w, GLFW_KEY_RIGHT) == GLFW_PRESS;
    const bool up    = glfwGetKey(w, GLFW_KEY_UP)    == GLFW_PRESS;
    const bool down  = glfwGetKey(w, GLFW_KEY_DOWN)  == GLFW_PRESS;

    // Same rotation math as the native loop: wall-clock dt, SPACE pauses
    // the spin, arrows yaw/pitch at 2.2 rad/s.
    const double now = glfwGetTime();
    const double dt = now - g_web.prev;
    g_web.prev = now;
    if (!space) g_web.angle += dt * 0.9;
    constexpr double kManualRate = 2.2;  // rad/s while an arrow is held
    g_web.yaw   += dt * kManualRate * ((right ? 1 : 0) - (left ? 1 : 0));
    g_web.pitch += dt * kManualRate * ((down ? 1 : 0) - (up ? 1 : 0));

    int fb_w = 0, fb_h = 0;
    glfwGetFramebufferSize(w, &fb_w, &fb_h);

    scene_draw(g_web.scene, fb_w, fb_h, g_web.angle, g_web.yaw, g_web.pitch);
    glfwSwapBuffers(w);  // effectively a no-op under RAF; kept to mirror native
}

int run_web(GLFWwindow* window) {
    glfwMakeContextCurrent(window);  // one thread: current here is current everywhere

    std::printf("GL_VERSION:  %s\n", glGetString(GL_VERSION));
    std::printf("GL_RENDERER: %s\n", glGetString(GL_RENDERER));

    if (!scene_init(g_web.scene)) return 1;

    glEnable(GL_DEPTH_TEST);

    g_web.window = window;
    g_web.prev = glfwGetTime();

    // simulate_infinite_loop=1: this call never returns (it unwinds out of
    // main); the browser drives web_frame at display refresh from here on.
    // fps=0 means "use requestAnimationFrame", which is the entire point.
    emscripten_set_main_loop(web_frame, 0, 1);
    return 0;  // unreachable
}

#endif  // __EMSCRIPTEN__
```

(`WebState` is file-scope because `emscripten_set_main_loop(..., 1)` unwinds main's stack — locals would die. No `scene_destroy` on this path: the loop never exits; page teardown reclaims everything.)

In `src/renderer.h`, after the `render_thread_main` declaration:

```cpp
#ifdef __EMSCRIPTEN__
// Phase 8 web entry: makes the context current on the (only) thread, builds
// the scene, hands the frame clock to requestAnimationFrame via
// emscripten_set_main_loop — which never returns. Returns nonzero only on
// scene-init failure, before the loop starts.
int run_web(GLFWwindow* window);
#endif
```

- [ ] **Step 5: main.cpp — split main.** Includes and `glfw_error_callback` (lines 1–18) stay unconditional (everything there compiles under Emscripten; just unused). Then:

```cpp
#ifdef __EMSCRIPTEN__

// Phase 8: the web main. No flags, no threads, no poll pacer — one thread,
// one window, and run_web hands the clock to requestAnimationFrame.
int main() {
    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit()) {
        std::fprintf(stderr, "glfwInit failed\n");
        return EXIT_FAILURE;
    }

    // Emscripten's GLFW maps CONTEXT_VERSION_MAJOR >= 3 to a WebGL2 context
    // (available because the build sets -sMAX_WEBGL_VERSION=2; MIN=2 makes
    // a WebGL1-only browser fail loudly instead of silently downgrading).
    // GLFW_OPENGL_PROFILE and FORWARD_COMPAT are desktop-GL concepts —
    // omitted deliberately; WebGL has no profiles.
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);

    GLFWwindow* window = glfwCreateWindow(960, 540, "cube", nullptr, nullptr);
    if (!window) {
        std::fprintf(stderr, "glfwCreateWindow failed\n");
        glfwTerminate();
        return EXIT_FAILURE;
    }

    return run_web(window);
}

#else  // native

int main(int argc, char** argv) {
    /* entire existing body, unchanged */
}

#endif  // __EMSCRIPTEN__
```

- [ ] **Step 6: web/build.py** (and add `dist/` to `.gitignore`):

```python
#!/usr/bin/env python3
"""Phase 8 web-build driver.

Compiles the whole app (src/*.cpp) to WebAssembly with Emscripten and stages
a servable site in --out: cube.js + cube.wasm + index.html. The same three
translation units as the native build; __EMSCRIPTEN__ guards select the
single-threaded browser path (no render thread, no pacer in the loop --
requestAnimationFrame owns the frame clock). Stdlib-only, like every runner
in bench/.

Requires an activated emsdk: emcc must be on PATH (run emsdk_env first).
"""
import argparse
import shutil
import subprocess
import sys
from pathlib import Path

# The Phase 8 build flags, verbatim from the spec (only -o moves to --out).
# EXPORTED_RUNTIME_METHODS=ccall is spec-mandated; nothing calls ccall today.
EMCC_FLAGS = [
    "-std=c++20",
    "-O3",
    "-sUSE_GLFW=3",
    "-sMIN_WEBGL_VERSION=2",
    "-sMAX_WEBGL_VERSION=2",
    "-sALLOW_MEMORY_GROWTH=1",
    "-sEXPORTED_RUNTIME_METHODS=ccall",
]


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--out", default="dist",
                    help="output directory for the servable site (git-ignored)")
    args = ap.parse_args()

    root = Path(__file__).resolve().parent.parent

    # shutil.which honors PATHEXT, so this resolves emcc.bat on Windows.
    emcc = shutil.which("emcc")
    if emcc is None:
        sys.exit("FATAL: emcc not found on PATH -- activate emsdk first "
                 "(emsdk_env.bat on Windows, `source emsdk_env.sh` on POSIX)")

    sources = sorted((root / "src").glob("*.cpp"))
    if not sources:
        sys.exit(f"FATAL: no sources matched {root / 'src'}/*.cpp")

    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    js = out / "cube.js"
    wasm = out / "cube.wasm"

    cmd = ([emcc] + [str(s) for s in sources] + ["-o", str(js)] + EMCC_FLAGS +
           [f"-I{root / 'extern'}", f"-I{root / 'src'}"])
    print(" ".join(cmd), flush=True)
    result = subprocess.run(cmd)
    if result.returncode != 0:
        sys.exit(f"FATAL: emcc exited {result.returncode}")
    for artifact in (js, wasm):
        if not artifact.is_file():
            sys.exit(f"FATAL: emcc exited 0 but {artifact} was not produced")

    page = root / "web" / "index.html"
    if page.is_file():
        shutil.copyfile(page, out / "index.html")

    print(f"wasm: js_bytes={js.stat().st_size} wasm_bytes={wasm.stat().st_size} "
          f"out={out}", flush=True)


if __name__ == "__main__":
    main()
```

**Contingency (apply ONLY if the emcc link fails on `clock_nanosleep`):** insert into `src/pacer.cpp` between the `__APPLE__` branch and the POSIX `#else`:

```cpp
#elif defined(__EMSCRIPTEN__)

#include <ctime>

uint64_t pacer_now_ns() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1'000'000'000ull +
           static_cast<uint64_t>(ts.tv_nsec);
}

FramePacer::FramePacer(uint64_t period_ns, PaceStrategy strategy,
                       ReschedulePolicy resched)
    : sched_(period_ns, pacer_now_ns(), resched), strategy_(strategy) {}

FramePacer::~FramePacer() = default;

void FramePacer::sleep_until_ns(uint64_t) {
    // Deliberate no-op. The web build never constructs a FramePacer:
    // requestAnimationFrame owns the frame clock, and there is no way to
    // sleep the browser main thread. This branch exists only so the
    // translation unit links; a paced web build would be a design bug.
}

uint64_t thread_cpu_now_ns() {
    timespec ts;
    if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts) != 0) return 0;
    return static_cast<uint64_t>(ts.tv_sec) * 1'000'000'000ull +
           static_cast<uint64_t>(ts.tv_nsec);
}
```

(Adjust the ctor signature to match pacer.cpp's actual constructor if it differs — it is the platform layer's existing per-OS ctor pattern; copy the POSIX branch's ctor/dtor shape exactly and no-op only the sleep primitive.)

- [ ] **Step 7: Build the wasm** (emsdk-activated shell, repo root):

Run: `python web/build.py --out dist`
Expected: the echoed emcc command, then `wasm: js_bytes=<n> wasm_bytes=<n> out=dist`. If the link fails on `clock_nanosleep`, apply the Step 6 contingency and rebuild.

- [ ] **Step 8: Native regression gate**:

Run: `cmake --build build --config Release && ctest --test-dir build -C Release --output-on-failure`
Expected: builds clean, all 15 tests pass. Then a brief manual run (`build/Release/cube.exe --fps=60`) to eyeball the cube — colors, spin, arrows, SPACE.

- [ ] **Step 9: Copy this plan** to `docs/superpowers/plans/2026-07-28-phase-8-live-link.md` and **commit**:

```bash
git add src/renderer.cpp src/renderer.h src/main.cpp web/build.py .gitignore docs/superpowers/plans/2026-07-28-phase-8-live-link.md
git commit -m "feat: Emscripten web path -- SceneGL extraction, run_web, stdlib build driver (Phase 8)"
```

(If the pacer contingency fired, include `src/pacer.cpp` with a separate `fix: emscripten pacer platform branch` commit.) Record the emsdk version string in the report for Tasks 3–4.

---

### Task 2: The page

**Files:**
- Create: `web/index.html`

**Interfaces:**
- Consumes: `dist/cube.js`/`cube.wasm` from Task 1's driver; emscripten GLFW binds `Module.canvas`.
- Produces: the page Task 3 deploys; the boundary prose Task 4's README links to.

- [ ] **Step 1: Write `web/index.html`:**

```html
<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>cube — OpenGL-Renderer, single-threaded web build</title>
<style>
  :root { color-scheme: dark; }
  body { margin: 0; background: #0d0d10; color: #c8c8cc;
         font: 15px/1.5 system-ui, sans-serif;
         display: flex; justify-content: center; }
  main { max-width: 760px; padding: 28px 16px 48px; }
  h1 { font-size: 1.3rem; font-weight: 600; color: #e8e8ec; margin: 0 0 4px; }
  h2 { font-size: 1.05rem; font-weight: 600; color: #e8e8ec; margin-top: 28px; }
  .sub { color: #9a9aa2; margin: 0 0 16px; }
  canvas { display: block; width: 100%; background: #000;
           border: 1px solid #26262c; border-radius: 4px; outline: none; }
  canvas:focus { border-color: #4a6a8a; }
  .controls { color: #9a9aa2; margin-top: 10px; }
  kbd { background: #1c1c22; border: 1px solid #33333a; border-radius: 3px;
        padding: 0 5px; font-family: ui-monospace, "Cascadia Mono", monospace; }
  .boundary { border-left: 3px solid #33333a; padding-left: 14px; color: #a8a8b0; }
  a { color: #7aa2c4; }
</style>
</head>
<body>
<main>
  <h1>cube</h1>
  <p class="sub">The OpenGL-Renderer demo, compiled to WebAssembly —
  single-threaded and browser-paced, on purpose.</p>

  <canvas id="canvas" tabindex="0"></canvas>
  <p class="controls">Click the cube to give it keyboard focus, then:
    <kbd>Space</kbd> pauses the spin &nbsp;·&nbsp;
    <kbd>←</kbd> <kbd>→</kbd> yaw &nbsp;·&nbsp;
    <kbd>↑</kbd> <kbd>↓</kbd> pitch.</p>

  <h2>What you are not seeing</h2>
  <p class="boundary">
    This build deliberately removes the two systems the project is actually
    about. The <strong>frame pacer</strong> is meaningless in a browser:
    there is no high-resolution sleep, busy-spinning the main thread would
    freeze the page, <code>requestAnimationFrame</code> owns the frame
    clock, and <code>performance.now()</code> is coarsened to ~100&nbsp;µs
    without cross-origin isolation — the native build's headline
    &lt;200&nbsp;µs pacing result cannot even be <em>measured</em> here. The
    <strong>threaded render</strong> does not port either: Emscripten's
    offscreen-canvas paths (<code>OFFSCREEN_FRAMEBUFFER</code> /
    <code>OFFSCREENCANVAS_SUPPORT</code>) only support HTML5-API or SDL2
    contexts, not GLFW — so there is no render thread, by decision rather
    than omission. What remains is the shared GL scene on one thread, paced
    by the browser.
  </p>
  <p>The pacer benchmarks, the input-handoff benchmarks, and the threaded
  architecture live in the native build:
  <a href="https://github.com/tiffany-mares/OpenGL-Renderer">source and full
  writeup on GitHub</a>.</p>
</main>
<script>
  var canvas = document.getElementById('canvas');
  canvas.addEventListener('click', function () { canvas.focus(); });
  var Module = { canvas: canvas };
</script>
<script src="cube.js"></script>
</body>
</html>
```

- [ ] **Step 2: Rebuild + serve locally:**

Run: `python web/build.py --out dist` then `python -m http.server 8000 -d dist`
Expected: `wasm:` line (index.html now copied in); server up.

- [ ] **Step 3: Browser verification at http://localhost:8000/** (the controller runs this with the claude-in-chrome tools — navigate, screenshot, read_console_messages):
  1. Canvas shows the dark-clear background and a spinning flat-shaded cube with six distinct face colors (validates the `flat`/provoking-vertex trick under WebGL2).
  2. Console shows `GL_VERSION: OpenGL ES 3.0 (WebGL 2.0 ...)`, zero errors from cube.js.
  3. Two screenshots ~1 s apart differ (spin is real).
  4. Click canvas (focus ring appears) → hold Right = yaw, hold Down = pitch, SPACE pauses the base spin while arrows still respond.
  5. Arrow keys/SPACE do not scroll the page while the canvas is focused. If they do, add this to the pre-script block and re-verify: `window.addEventListener('keydown', function (e) { if (document.activeElement === canvas && ['ArrowLeft','ArrowRight','ArrowUp','ArrowDown',' '].indexOf(e.key) >= 0) e.preventDefault(); });`

- [ ] **Step 4: Commit:**

```bash
git add web/index.html
git commit -m "feat: web demo page -- canvas, controls, and the what-you-are-not-seeing boundary"
```

---

### Task 3: The deploy

**Files:**
- Create: `.github/workflows/pages.yml`

**Interfaces:**
- Consumes: Task 1's recorded emsdk version (the pin), `web/build.py --out dist`.
- Produces: the live URL `https://tiffany-mares.github.io/OpenGL-Renderer/` that Task 4's README links.

- [ ] **Step 1: Write `.github/workflows/pages.yml`** — replace `EMSDK_VERSION_FROM_T1` with the exact version string Task 1 recorded (e.g. `4.0.15`):

```yaml
name: pages
on:
  push:
    branches: [main]
  workflow_dispatch:

permissions:
  contents: read
  pages: write
  id-token: write

concurrency:
  group: pages
  cancel-in-progress: true

jobs:
  build:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - uses: mymindstorm/setup-emsdk@v14
        with:
          version: EMSDK_VERSION_FROM_T1
          actions-cache-folder: emsdk-cache
      - name: Build wasm site
        run: python web/build.py --out dist
      - name: Configure Pages
        uses: actions/configure-pages@v5
        with:
          enablement: true
      - uses: actions/upload-pages-artifact@v3
        with:
          path: dist

  deploy:
    needs: build
    runs-on: ubuntu-latest
    environment:
      name: github-pages
      url: ${{ steps.deployment.outputs.page_url }}
    steps:
      - id: deployment
        uses: actions/deploy-pages@v4
```

- [ ] **Step 2: Commit and push:**

```bash
git add .github/workflows/pages.yml
git commit -m "feat: GitHub Pages deploy workflow, emsdk pinned to the local build's version (Phase 8)"
git push
```

- [ ] **Step 3: Verify both workflows** via the REST API (no gh CLI on this machine): the existing `build` workflow stays green (both legs), and the new `pages` workflow completes `success`. Known first-run wrinkle: the very first `deploy-pages` can fail before Pages finishes self-enabling — if it does, re-run once via `workflow_dispatch` before diagnosing further.

- [ ] **Step 4: Verify the live URL** in Chrome: load `https://tiffany-mares.github.io/OpenGL-Renderer/` and repeat Task 2 Step 3's checks 1–3. Allow minutes of propagation on the first deploy; retry with a cache-busting query string before declaring failure.

---

### Task 4: The record

**Files:**
- Modify: `README.md`, `CLAUDE.md`

- [ ] **Step 1: README** — insert after the opening one-sentence paragraph, before the GIF embed:

```markdown
**[Live web demo](https://tiffany-mares.github.io/OpenGL-Renderer/)** — a
single-threaded, browser-paced Emscripten build; the pacer and the threaded
render deliberately do not port, and the page says why.
```

Also add a short "### The browser build" subsection at the end of "Build & run on a clean machine" (after the Python-dependencies paragraph, before "### CLI reference"):

```markdown
### The browser build

The live demo is the same three translation units compiled with Emscripten
(`__EMSCRIPTEN__` guards select a single-threaded path: no render thread, no
pacer — `requestAnimationFrame` owns the frame clock, and the page explains
why that boundary exists). Build it locally with an activated emsdk:

    python web/build.py --out dist
    python -m http.server 8000 -d dist

Deployed automatically to GitHub Pages by `.github/workflows/pages.yml` on
every push to main, with emsdk pinned to the version recorded there.
```

- [ ] **Step 2: CLAUDE.md** — in the line-7 phase paragraph: change the bold opener `**Phases 0–7 are complete as of 2026-07-28.**` to `**Phases 0–8 are complete as of 2026-07-28.**`, and replace the final sentence `Phase 8 does not exist yet — update this file as it lands.` with (single paragraph, filling in the emsdk version Task 1 recorded):

```
Phase 8 (complete 2026-07-28): the live link — https://tiffany-mares.github.io/OpenGL-Renderer/ serves a single-threaded, browser-paced Emscripten build (the pacer and the render thread deliberately do not port: no high-res sleep or busy-spin on the browser main thread, RAF owns the frame clock, and Emscripten's offscreen-canvas paths don't support GLFW contexts — the page states this boundary). The GL scene was extracted into file-static `SceneGL`/`scene_init`/`scene_draw`/`scene_destroy` in `src/renderer.cpp`, shared verbatim by the native thread loop and the web path — scene changes go THERE, never in the loops (the native loop is compiled out under `__EMSCRIPTEN__`, so loop-only changes are web-invisible by design). Web path: `run_web` + `web_frame` (renderer.cpp, `#ifdef __EMSCRIPTEN__`), ES 3.00 shader variants (`#version 300 es` as the literal's first characters + explicit fragment precision; WebGL2's last-vertex provoking convention keeps the per-face color trick), GLES3 headers instead of glad, separate web `main()` (960×540 canvas, WebGL2 via CONTEXT_VERSION_MAJOR=3), input read directly via `glfwGetKey` — the InputChannel/pacer/log/capture machinery is bypassed. Build: `python web/build.py --out dist` (stdlib; spec's emcc flags verbatim incl. the unused-but-mandated `EXPORTED_RUNTIME_METHODS=ccall`; needs an activated emsdk — local install at `C:\Users\tiffm\emsdk`, version <EMSDK_VERSION> pinned in `.github/workflows/pages.yml`), which stages `dist/` (git-ignored) with `web/index.html`. Deploy: pages.yml (push to main + workflow_dispatch, configure-pages self-enablement). There are no Phase 8 unit tests: the native 15-suite ctest run gates the SceneGL refactor, and the web build is verified by building it plus in-browser checks (cube renders/spins, arrows/SPACE work, console clean).
```

- [ ] **Step 3: Verify links + commit + push:**

Run: check the README's new link renders and the live URL still loads; then:

```bash
git add README.md CLAUDE.md
git commit -m "docs: live demo link and Phase 8 record"
git push
```

Confirm `build` (both legs) and `pages` workflows green at the new HEAD via the REST API.

---

## Verification (end-to-end)

1. Native: full rebuild + `ctest --test-dir build -C Release --output-on-failure` → 15/15; manual `--fps=60` run looks unchanged.
2. Web local: `python web/build.py --out dist` prints the `wasm:` line; `http.server` + Chrome shows the spinning six-color cube, WebGL2 GL_VERSION in console, zero console errors, arrows/SPACE work with canvas focus, no page scroll.
3. Live: the Pages URL serves the same page and passes the same checks; both GitHub workflows green.
4. Docs: README live-link + browser-build subsection present; CLAUDE.md says Phases 0–8 with the Phase 8 record and real emsdk version.

## Risks (watch during execution)

- Canvas keyboard focus / page scroll: emscripten GLFW listens on `window`; the Task 2 Step 3.5 preventDefault mitigation is applied only if verification shows scroll.
- First Pages deploy can 404 for minutes or fail once before self-enablement settles; retry once before diagnosing.
- If the emcc link fails on `clock_nanosleep`, the Task 1 pacer contingency branch is the designed fix — pure addition, native branches untouched.
- `EXPORTED_RUNTIME_METHODS=ccall` is dead weight today — spec-mandated, kept verbatim, commented in build.py.

## Self-review notes

- Spec coverage: 8a emcc line (verbatim in build.py), loop gate (`emscripten_set_main_loop(web_frame, 0, 1)` vs pacer under `#else`), ES shaders with precision, single-threaded/no-COI (no `-pthread`, no COOP/COEP anywhere), "say so on the page" (the boundary paragraph in index.html carries the user's own claims), the live link itself (Pages workflow + README link).
- Type consistency: `SceneGL`/`scene_init`/`scene_draw`/`scene_destroy` signatures identical across Task 1's helper definitions, the native replacement sites, and the web path; `run_web(GLFWwindow*)` matches renderer.h decl and main.cpp call.
- Deliberate choices: web input bypasses InputChannel (single thread — the machinery measures a boundary that doesn't exist there; stated in code comment and CLAUDE.md record); native `render_thread_main` compiled out on web because glad is desktop-only; `dist/` never committed.
