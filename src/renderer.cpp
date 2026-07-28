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
static const char* kVertexSrc = R"glsl(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aColor;
uniform mat4 uMvp;
flat out vec3 vColor;

void main() {
    gl_Position = uMvp * vec4(aPos, 1.0);
    vColor = aColor;
}
)glsl";

static const char* kFragmentSrc = R"glsl(
#version 330 core
flat in vec3 vColor;
out vec4 FragColor;

void main() {
    FragColor = vec4(vColor, 1.0);
}
)glsl";
#endif

static GLuint compile_shader(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(s, sizeof log, nullptr, log);
        std::fprintf(stderr, "shader compile error: %s\n", log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

static GLuint link_program(const char* vs_src, const char* fs_src) {
    GLuint vs = compile_shader(GL_VERTEX_SHADER, vs_src);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, fs_src);
    if (!vs || !fs) {
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        return 0;
    }
    GLuint p = glCreateProgram();
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glLinkProgram(p);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetProgramInfoLog(p, sizeof log, nullptr, log);
        std::fprintf(stderr, "program link error: %s\n", log);
        glDeleteProgram(p);
        return 0;
    }
    return p;
}

// 8 shared vertices, interleaved position (xyz) + color (rgb).
// True per-face colors from only 8 vertices: colors are flat-shaded, and
// every face's two triangles are wound (CCW, outward-facing) to END on a
// vertex no other face ends on, so that provoking vertex's color paints the
// whole face. Vertices 2 and 4 are never provoking; their colors are unused.
static const float kVertices[] = {
    // position            color
    -1.f, -1.f, -1.f,      0.90f, 0.20f, 0.20f,  // 0: -Z back   (red)
     1.f, -1.f, -1.f,      0.90f, 0.80f, 0.20f,  // 1: +X right  (yellow)
     1.f,  1.f, -1.f,      0.f,   0.f,   0.f,    // 2: (never provoking)
    -1.f,  1.f, -1.f,      0.20f, 0.80f, 0.80f,  // 3: +Y top    (cyan)
    -1.f, -1.f,  1.f,      0.f,   0.f,   0.f,    // 4: (never provoking)
     1.f, -1.f,  1.f,      0.80f, 0.30f, 0.80f,  // 5: -Y bottom (magenta)
     1.f,  1.f,  1.f,      0.20f, 0.75f, 0.30f,  // 6: +Z front  (green)
    -1.f,  1.f,  1.f,      0.25f, 0.35f, 0.90f,  // 7: -X left   (blue)
};

static const unsigned int kIndices[] = {
    2, 1, 0,   3, 2, 0,  // -Z back   (provoking vertex 0)
    4, 5, 6,   7, 4, 6,  // +Z front  (provoking vertex 6)
    3, 0, 7,   0, 4, 7,  // -X left   (provoking vertex 7)
    6, 5, 1,   2, 6, 1,  // +X right  (provoking vertex 1)
    4, 0, 5,   0, 1, 5,  // -Y bottom (provoking vertex 5)
    6, 2, 3,   7, 6, 3,  // +Y top    (provoking vertex 3)
};

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

#ifndef __EMSCRIPTEN__
// render_thread_main calls gladLoadGL and cannot compile on web; the
// Emscripten path is run_web/web_frame further down this file.

// Fixed per-frame CPU cost for instrumented runs (~100 µs): the CSV then
// measures the pacer against a constant workload, not GPU/driver variance.
// The volatile sink is what stops the optimizer deleting the call.
static constexpr uint64_t kWorkloadIters = 100'000;
static volatile uint64_t g_workload_sink = 0;

void render_thread_main(GLFWwindow* window, const InputChannel& input,
                        const FramebufferSize& fb, RenderConfig cfg,
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

    SceneGL scene;
    if (!scene_init(scene)) {
        failed.store(true);
        glfwMakeContextCurrent(nullptr);
        glfwSetWindowShouldClose(window, GLFW_TRUE);
        return;
    }

    glEnable(GL_DEPTH_TEST);

    // Rotation: the wall-clock spin from Phase 3 (SPACE pauses it) plus
    // manual yaw/pitch from the arrow keys — the visible proof that key
    // state crosses the thread boundary through whichever backend is live.
    double angle = 0.0, yaw = 0.0, pitch = 0.0;
    double prev = glfwGetTime();

    // Phase 5: the pacer owns the frame clock — vsync stays off
    // (glfwSwapInterval(0) above). fps_cap == 0 keeps the uncapped loop.
    // The pacer is constructed on this thread: on Windows it owns a
    // waitable-timer handle, and wait() must run where the frames run.
    std::unique_ptr<FramePacer> pacer;
    if (cfg.fps_cap > 0)
        pacer = std::make_unique<FramePacer>(1'000'000'000ull / cfg.fps_cap,
                                             cfg.pace, cfg.resched);
    uint64_t frames = 0;
    const double t_start = glfwGetTime();

    // Phase 6a instrumentation; 6b legalizes uncapped logging (frame_time_ns
    // then degrades to frame-start-to-frame-start — there are no deadlines).
    // Buffer preallocated before the first frame; append never allocates;
    // CSV written after the loop. Bench runs size the buffer exactly;
    // interactive paced runs get ten minutes of frames, then drop-and-count.
    const bool logging = cfg.log_path != nullptr;
    const size_t log_capacity =
        logging ? (cfg.bench_frames ? static_cast<size_t>(cfg.bench_frames)
                                    : static_cast<size_t>(cfg.fps_cap) * 600)
                : 0;
    FrameLog log(log_capacity);
    uint64_t prev_deadline_ns = 0;
    uint64_t prev_frame_start_ns = 0;

    // Phase 7: GIF capture. Preallocated before the first frame (FrameLog
    // discipline); read back pre-swap; file written after the loop. The
    // framebuffer size comes from the side channel main populated before
    // this thread started.
    const bool capturing = cfg.capture_path != nullptr;
    std::unique_ptr<CaptureBuffer> capture;
    if (capturing) {
        int cw = 0, ch = 0;
        fb.load(cw, ch);
        capture = std::make_unique<CaptureBuffer>(cfg.capture_frames, cw, ch);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);  // tightly packed rows
        glReadBuffer(GL_BACK);
    }

    // Phase 6b bench mode: run exactly cfg.bench_frames frames, snapshot this
    // thread's CPU time at the warmup boundary, then request close. CPU% is
    // the honesty column: it prices what each pacing strategy pays for its
    // accuracy.
    constexpr uint64_t kBenchWarmup = 500;
    uint64_t bench_cpu0 = 0, bench_wall0 = 0;

    // thread_cpu_now_ns()'s first call on Windows burns ~24 ms calibrating
    // QueryThreadCycleTime against pacer_now_ns (see pacer.cpp). It must
    // happen here, before the loop, and not be allowed to happen for the
    // first time at the frame-500 warmup snapshot below -- otherwise that
    // 24 ms of calibration spin would land inside a timed frame and get
    // counted as render-thread work in the bench's CPU% column.
    if (cfg.bench_frames) (void)thread_cpu_now_ns();

    while (!stop.load(std::memory_order_relaxed)) {
        const uint64_t frame_start_ns = logging ? pacer_now_ns() : 0;

        InputSnapshot in = input.read();
        uint64_t input_latency_ns = 0;
        if (logging) {
            // Consume-minus-publish on the shared pacer_now_ns timeline.
            // publish_ns == 0 means the backend cannot carry it (bitmask) —
            // logged as 0, which is the honest answer.
            const uint64_t consume_ns = pacer_now_ns();
            if (in.publish_ns != 0 && consume_ns > in.publish_ns)
                input_latency_ns = consume_ns - in.publish_ns;
        }

        int fb_w = 0, fb_h = 0;
        fb.load(fb_w, fb_h);

        double now = glfwGetTime();
        constexpr double kTwoPi = 6.283185307179586;
        const double dt = capturing ? kTwoPi / (0.9 * cfg.capture_frames)
                                    : now - prev;
        if (!(in.keys & kKeySpace)) angle += dt * 0.9;
        constexpr double kManualRate = 2.2;  // rad/s while an arrow is held
        yaw   += dt * kManualRate * (((in.keys & kKeyRight) ? 1 : 0) - ((in.keys & kKeyLeft) ? 1 : 0));
        pitch += dt * kManualRate * (((in.keys & kKeyDown) ? 1 : 0) - ((in.keys & kKeyUp) ? 1 : 0));
        prev = now;

        scene_draw(scene, fb_w, fb_h, angle, yaw, pitch);

        if (logging) g_workload_sink = synthetic_workload(kWorkloadIters);

        if (capturing) {
            if (uint8_t* dst = capture->next_frame(fb_w, fb_h)) {
                glReadPixels(0, 0, fb_w, fb_h, GL_RGB, GL_UNSIGNED_BYTE, dst);
                capture->commit();
            }
        }

        glfwSwapBuffers(window);  // safe: this thread holds the context

        WaitStats ws{};
        if (pacer) ws = pacer->wait();
        if (logging) {
            FrameRecord r{};
            r.frame = frames;
            r.frame_start_ns = frame_start_ns;
            r.frame_end_ns = pacer_now_ns();
            // Paced: deadline to deadline — the cadence the pacer delivered.
            // Uncapped: frame-start to frame-start — no deadlines exist.
            // Frame 0 has no previous value either way: logged as 0.
            r.frame_time_ns =
                pacer ? (prev_deadline_ns ? ws.deadline_ns - prev_deadline_ns : 0)
                      : (prev_frame_start_ns ? frame_start_ns - prev_frame_start_ns : 0);
            r.sleep_requested_ns = ws.sleep_requested_ns;
            r.sleep_actual_ns = ws.sleep_actual_ns;
            r.input_latency_ns = input_latency_ns;
            r.missed = ws.missed ? 1 : 0;
            log.append(r);
            prev_deadline_ns = ws.deadline_ns;
            prev_frame_start_ns = frame_start_ns;
        }
        ++frames;

        if (cfg.bench_frames) {
            if (frames == kBenchWarmup) {
                bench_cpu0 = thread_cpu_now_ns();
                bench_wall0 = pacer_now_ns();
            }
            if (frames >= cfg.bench_frames) {
                glfwSetWindowShouldClose(window, GLFW_TRUE);  // documented any-thread
                break;
            }
        }

        if (capturing && capture->done()) {
            glfwSetWindowShouldClose(window, GLFW_TRUE);  // documented any-thread
            break;
        }
    }

    // Bench snapshot immediately at loop exit — before the CSV write, so the
    // measured CPU window contains only frames.
    if (cfg.bench_frames && frames > kBenchWarmup) {
        const uint64_t cpu1 = thread_cpu_now_ns();
        const uint64_t wall1 = pacer_now_ns();
        const uint64_t cpu = cpu1 - bench_cpu0;
        const uint64_t wall = wall1 - bench_wall0;
        std::printf("bench: frames=%llu warmup=%llu measured=%llu cpu_ns=%llu wall_ns=%llu cpu_pct=%.2f\n",
                    static_cast<unsigned long long>(frames),
                    static_cast<unsigned long long>(kBenchWarmup),
                    static_cast<unsigned long long>(frames - kBenchWarmup),
                    static_cast<unsigned long long>(cpu),
                    static_cast<unsigned long long>(wall),
                    wall > 0 ? 100.0 * static_cast<double>(cpu) / static_cast<double>(wall) : 0.0);
    }

    // Evidence the cap holds; the CSV carries the real story.
    if (pacer) {
        const double elapsed = glfwGetTime() - t_start;
        std::printf("frames: %llu  avg fps: %.2f  missed deadlines: %llu\n",
                    static_cast<unsigned long long>(frames),
                    elapsed > 0.0 ? static_cast<double>(frames) / elapsed : 0.0,
                    static_cast<unsigned long long>(pacer->missed()));
    }
    if (logging) {
        if (log.write_csv(cfg.log_path))
            std::printf("frame log: %zu records -> %s (%llu dropped)\n",
                        log.size(), cfg.log_path,
                        static_cast<unsigned long long>(log.dropped()));
    }
    if (capturing) {
        const bool ok = capture->write(cfg.capture_path, cfg.fps_cap);
        std::printf("capture: path=%s frames=%u requested=%u width=%d height=%d "
                    "fps=%u bytes=%llu resized=%d missed=%llu\n",
                    cfg.capture_path, capture->frames(), cfg.capture_frames,
                    capture->width(), capture->height(), cfg.fps_cap,
                    static_cast<unsigned long long>(
                        static_cast<uint64_t>(capture->frames()) * capture->frame_bytes()),
                    capture->resized() ? 1 : 0,
                    static_cast<unsigned long long>(pacer ? pacer->missed() : 0));
        if (!ok) failed.store(true);
    }

    // GL teardown on the owning thread, before the main thread joins us.
    scene_destroy(scene);
    glfwMakeContextCurrent(nullptr);
}
#endif  // !__EMSCRIPTEN__

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
