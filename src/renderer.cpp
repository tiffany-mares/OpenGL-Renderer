#include "renderer.h"

#include <cstdio>
#include <cstdint>
#include <memory>

#define GLAD_GL_IMPLEMENTATION
#include <glad/gl.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "frame_log.h"
#include "input_state.h"
#include "mat4.h"
#include "pacer.h"
#include "workload.h"

// Transform comes from the CPU now (src/mat4.h). Colors stay `flat`: the
// provoking (last) vertex of each triangle colors the whole face — see the
// index buffer comment.
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
        pacer = std::make_unique<FramePacer>(1'000'000'000ull / cfg.fps_cap, cfg.pace);
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
        const double dt = now - prev;
        if (!(in.keys & kKeySpace)) angle += dt * 0.9;
        constexpr double kManualRate = 2.2;  // rad/s while an arrow is held
        yaw   += dt * kManualRate * (((in.keys & kKeyRight) ? 1 : 0) - ((in.keys & kKeyLeft) ? 1 : 0));
        pitch += dt * kManualRate * (((in.keys & kKeyDown) ? 1 : 0) - ((in.keys & kKeyUp) ? 1 : 0));
        prev = now;

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
        glUseProgram(program);
        glUniformMatrix4fv(mvpLoc, 1, GL_FALSE, mvp.m);  // column-major: no transpose
        glBindVertexArray(vao);
        glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, nullptr);

        if (logging) g_workload_sink = synthetic_workload(kWorkloadIters);

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

    // GL teardown on the owning thread, before the main thread joins us.
    glDeleteVertexArrays(1, &vao);
    glDeleteBuffers(1, &vbo);
    glDeleteBuffers(1, &ebo);
    glDeleteProgram(program);
    glfwMakeContextCurrent(nullptr);
}
