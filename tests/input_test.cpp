#include <cstdio>
#include <string_view>

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

int main() {
    {
        MutexChannel ch;
        full_payload_suite(ch, "mutex");
        expect(std::string_view(ch.name()) == "mutex", "mutex name");
    }

    bitmask_suite();

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
