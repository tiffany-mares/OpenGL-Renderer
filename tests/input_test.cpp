#include <atomic>
#include <cstdio>
#include <string_view>
#include <thread>

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

int main() {
    {
        MutexChannel ch;
        full_payload_suite(ch, "mutex");
        expect(std::string_view(ch.name()) == "mutex", "mutex name");
    }

    bitmask_suite();

    {
        SeqlockChannel ch;
        full_payload_suite(ch, "seqlock");
        expect(std::string_view(ch.name()) == "seqlock", "seqlock name");
        seqlock_stress();
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

    expect(make_input_channel("mutex") && std::string_view(make_input_channel("mutex")->name()) == "mutex",
           "factory mutex");
    expect(make_input_channel("bitmask") && std::string_view(make_input_channel("bitmask")->name()) == "bitmask",
           "factory bitmask");
    expect(make_input_channel("seqlock") && std::string_view(make_input_channel("seqlock")->name()) == "seqlock",
           "factory seqlock");
    expect(make_input_channel("bogus") == nullptr, "factory rejects unknown");

    if (g_failures) {
        std::fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("all input tests passed\n");
    return 0;
}
