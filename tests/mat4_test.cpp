#include <cmath>
#include <cstdio>

#include "mat4.h"

static int g_failures = 0;

static void expect_near(float actual, float expected, const char* what) {
    if (std::fabs(actual - expected) > 1e-5f) {
        std::fprintf(stderr, "FAIL %s: got %.7f expected %.7f\n", what, actual, expected);
        ++g_failures;
    }
}

// Multiply m by column vector (x,y,z,w); returns component `row`.
static float mul_row(const mat4& m, float x, float y, float z, float w, int row) {
    return m.m[0 * 4 + row] * x + m.m[1 * 4 + row] * y +
           m.m[2 * 4 + row] * z + m.m[3 * 4 + row] * w;
}

static void test_identity() {
    mat4 i = mat4::identity();
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r)
            expect_near(i.m[c * 4 + r], (c == r) ? 1.f : 0.f, "identity element");
}

static void test_translate() {
    mat4 t = translate({1.f, 2.f, 3.f});
    // T * origin = (1,2,3,1)
    expect_near(mul_row(t, 0, 0, 0, 1, 0), 1.f, "translate x");
    expect_near(mul_row(t, 0, 0, 0, 1, 1), 2.f, "translate y");
    expect_near(mul_row(t, 0, 0, 0, 1, 2), 3.f, "translate z");
    expect_near(mul_row(t, 0, 0, 0, 1, 3), 1.f, "translate w");
}

static void test_multiply() {
    mat4 a = translate({1.f, 0.f, 0.f});
    mat4 b = translate({0.f, 2.f, 0.f});
    mat4 ab = a * b;
    // Translations compose additively.
    expect_near(mul_row(ab, 0, 0, 0, 1, 0), 1.f, "compose x");
    expect_near(mul_row(ab, 0, 0, 0, 1, 1), 2.f, "compose y");
    // Identity is neutral on both sides.
    mat4 ia = mat4::identity() * a;
    mat4 ai = a * mat4::identity();
    for (int k = 0; k < 16; ++k) {
        expect_near(ia.m[k], a.m[k], "identity*a element");
        expect_near(ai.m[k], a.m[k], "a*identity element");
    }
}

static void test_vec3_helpers() {
    vec3 n = normalize({3.f, 0.f, 4.f});
    expect_near(n.x, 0.6f, "normalize x");
    expect_near(n.z, 0.8f, "normalize z");
    expect_near(dot({1.f, 2.f, 3.f}, {4.f, -5.f, 6.f}), 12.f, "dot");
    vec3 c = cross({1.f, 0.f, 0.f}, {0.f, 1.f, 0.f});
    expect_near(c.x, 0.f, "cross x");
    expect_near(c.y, 0.f, "cross y");
    expect_near(c.z, 1.f, "cross z");
}

static void test_perspective_reference() {
    // Reference values computed by hand for fovY=60deg, aspect=16/9,
    // zNear=0.1, zFar=100 (matches glm::perspective):
    //   f = 1/tan(30deg) = 1.7320508
    //   m[0]  = f/aspect                    =  0.9742786
    //   m[5]  = f                           =  1.7320508
    //   m[10] = (zFar+zNear)/(zNear-zFar)   = -1.0020020
    //   m[11] = -1
    //   m[14] = 2*zFar*zNear/(zNear-zFar)   = -0.2002002
    // every other element exactly 0 (including m[15] — catches identity-init bugs)
    mat4 p = perspective(1.0471976f, 16.f / 9.f, 0.1f, 100.f);
    for (int k = 0; k < 16; ++k) {
        float expected = 0.f;
        if (k == 0) expected = 0.9742786f;
        else if (k == 5) expected = 1.7320508f;
        else if (k == 10) expected = -1.0020020f;
        else if (k == 11) expected = -1.f;
        else if (k == 14) expected = -0.2002002f;
        expect_near(p.m[k], expected, "perspective element");
    }
}

static void test_lookat() {
    // Camera at (0,0,5) looking at origin: pure translation by -5 in z.
    mat4 v = lookAt({0.f, 0.f, 5.f}, {0.f, 0.f, 0.f}, {0.f, 1.f, 0.f});
    expect_near(mul_row(v, 0, 0, 0, 1, 0), 0.f, "lookAt origin x");
    expect_near(mul_row(v, 0, 0, 0, 1, 1), 0.f, "lookAt origin y");
    expect_near(mul_row(v, 0, 0, 0, 1, 2), -5.f, "lookAt origin z");
    // +X in world stays +X in view for this camera.
    expect_near(mul_row(v, 1, 0, 0, 1, 0), 1.f, "lookAt +x maps to +x");
}

static void test_rotate() {
    // 90deg about +Z maps (1,0,0) to (0,1,0).
    mat4 r = rotate({0.f, 0.f, 1.f}, 1.5707964f);
    expect_near(mul_row(r, 1, 0, 0, 1, 0), 0.f, "rotZ x");
    expect_near(mul_row(r, 1, 0, 0, 1, 1), 1.f, "rotZ y");
    expect_near(mul_row(r, 1, 0, 0, 1, 2), 0.f, "rotZ z");
    // Rotation about an axis leaves the axis fixed (axis passed unnormalized).
    mat4 rx = rotate({2.f, 0.f, 0.f}, 0.7f);
    expect_near(mul_row(rx, 1, 0, 0, 1, 0), 1.f, "axis fixed x");
    expect_near(mul_row(rx, 1, 0, 0, 1, 1), 0.f, "axis fixed y");
}

static void test_mvp_composition() {
    // Rotate 90deg about Z then translate +1 in x: (1,0,0) -> (0,1,0) -> (1,1,0).
    mat4 m = translate({1.f, 0.f, 0.f}) * rotate({0.f, 0.f, 1.f}, 1.5707964f);
    expect_near(mul_row(m, 1, 0, 0, 1, 0), 1.f, "T*R x");
    expect_near(mul_row(m, 1, 0, 0, 1, 1), 1.f, "T*R y");
}

int main() {
    test_identity();
    test_translate();
    test_multiply();
    test_vec3_helpers();
    test_perspective_reference();
    test_lookat();
    test_rotate();
    test_mvp_composition();
    if (g_failures) {
        std::fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("all mat4 tests passed\n");
    return 0;
}
