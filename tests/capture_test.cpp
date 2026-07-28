#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "capture.h"

static int failures = 0;
static void check(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

// Read a whole file into a string; empty on failure.
static std::string slurp(const char* path) {
    std::FILE* f = std::fopen(path, "rb");
    if (!f) return {};
    std::string out;
    char buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
    std::fclose(f);
    return out;
}

int main() {
    // Plane geometry: pointers spaced exactly w*h*3 apart, frames counted.
    {
        CaptureBuffer cb(3, 4, 2);  // 3 frames of 4x2 RGB = 24 bytes/frame
        check(cb.frame_bytes() == 24, "frame_bytes = w*h*3");
        uint8_t* p0 = cb.next_frame(4, 2);
        check(p0 != nullptr, "first plane available");
        cb.commit();
        uint8_t* p1 = cb.next_frame(4, 2);
        check(p1 == p0 + 24, "planes tightly packed");
        cb.commit();
        check(cb.frames() == 2 && !cb.done(), "two committed, not done");
        cb.next_frame(4, 2);
        cb.commit();
        check(cb.done() && cb.frames() == 3, "full after max_frames");
        check(cb.next_frame(4, 2) == nullptr, "full buffer returns null");
        check(!cb.resized(), "no resize seen");
    }
    // Resize mid-capture: null plane, resized latched, done.
    {
        CaptureBuffer cb(4, 4, 2);
        cb.next_frame(4, 2);
        cb.commit();
        check(cb.next_frame(5, 2) == nullptr, "size mismatch returns null");
        check(cb.resized() && cb.done(), "resize latches and stops capture");
        check(cb.frames() == 1, "frames unchanged by rejected plane");
    }
    // write(): raw file is exactly frames*w*h*3 bytes; sidecar carries meta.
    {
        CaptureBuffer cb(2, 4, 2);
        uint8_t* p = cb.next_frame(4, 2);
        std::memset(p, 0xAB, cb.frame_bytes());
        cb.commit();
        p = cb.next_frame(4, 2);
        std::memset(p, 0xCD, cb.frame_bytes());
        cb.commit();
        const char* path = "capture_test_tmp.raw";
        check(cb.write(path, 50), "write succeeds");
        std::string raw = slurp(path);
        check(raw.size() == 48, "raw file is frames*w*h*3 bytes");
        check((uint8_t)raw[0] == 0xAB && (uint8_t)raw[24] == 0xCD,
              "frames written in order");
        std::string meta = slurp("capture_test_tmp.raw.json");
        check(meta.find("\"width\":4") != std::string::npos, "meta width");
        check(meta.find("\"height\":2") != std::string::npos, "meta height");
        check(meta.find("\"frames\":2") != std::string::npos, "meta frames");
        check(meta.find("\"requested\":2") != std::string::npos, "meta requested");
        check(meta.find("\"fps\":50") != std::string::npos, "meta fps");
        check(meta.find("\"row_order\":\"bottom-up\"") != std::string::npos,
              "meta row order");
        check(meta.find("\"resized\":false") != std::string::npos, "meta resized");
        std::remove(path);
        std::remove("capture_test_tmp.raw.json");
    }
    if (failures) {
        std::fprintf(stderr, "%d capture test(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("capture tests passed\n");
    return EXIT_SUCCESS;
}
