#include <cstdio>
#include <cstdlib>

#define GLAD_GL_IMPLEMENTATION
#include <glad/gl.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

// Fixed yaw+pitch and a minimal perspective, all hardcoded: hand-rolled
// mat4 math is Phase 2; this shader is demoted to `gl_Position = uMvp * aPos`
// there. Colors use `flat` so the provoking (last) vertex of each triangle
// colors the whole face — see the index buffer comment.
static const char* kVertexSrc = R"glsl(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aColor;
flat out vec3 vColor;

void main() {
    float cy = cos(0.6), sy = sin(0.6);    // yaw ~34 deg
    float cx = cos(0.45), sx = sin(0.45);  // pitch ~26 deg
    vec3 p = aPos * 0.5;
    p = vec3(cy * p.x + sy * p.z, p.y, -sy * p.x + cy * p.z); // rotate Y
    p = vec3(p.x, cx * p.y - sx * p.z, sx * p.y + cx * p.z);  // rotate X
    p.z -= 2.5;                                               // push away from camera
    float f = 1.5;                 // focal length (~67 deg vertical fov)
    float aspect = 1280.0 / 720.0;
    float zn = 0.1, zf = 100.0;
    gl_Position = vec4(f * p.x / aspect,
                       f * p.y,
                       ((zf + zn) / (zn - zf)) * p.z + (2.0 * zf * zn) / (zn - zf),
                       -p.z);
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

    glfwMakeContextCurrent(window);
    glfwSwapInterval(0);

    if (gladLoadGL(glfwGetProcAddress) == 0) {
        std::fprintf(stderr, "gladLoadGL failed\n");
        glfwDestroyWindow(window);
        glfwTerminate();
        return EXIT_FAILURE;
    }

    std::printf("GL_VERSION:  %s\n", glGetString(GL_VERSION));
    std::printf("GL_RENDERER: %s\n", glGetString(GL_RENDERER));

    GLuint program = link_program(kVertexSrc, kFragmentSrc);
    if (program == 0) {
        glfwDestroyWindow(window);
        glfwTerminate();
        return EXIT_FAILURE;
    }

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

    // Uncapped loop: swap interval is 0 and the pacer doesn't exist until
    // Phase 5, so this spins as fast as the driver allows.
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
            glfwSetWindowShouldClose(window, GLFW_TRUE);

        int w, h;
        glfwGetFramebufferSize(window, &w, &h);
        glViewport(0, 0, w, h);
        glClearColor(0.05f, 0.05f, 0.06f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glUseProgram(program);
        glBindVertexArray(vao);
        glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, nullptr);
        glfwSwapBuffers(window);
    }

    glDeleteVertexArrays(1, &vao);
    glDeleteBuffers(1, &vbo);
    glDeleteBuffers(1, &ebo);
    glDeleteProgram(program);
    glfwDestroyWindow(window);
    glfwTerminate();
    return EXIT_SUCCESS;
}
