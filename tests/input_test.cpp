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
