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

int main() {
    test_identity();
    test_translate();
    test_multiply();
    test_vec3_helpers();
    if (g_failures) {
        std::fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("all mat4 tests passed\n");
    return 0;
}
