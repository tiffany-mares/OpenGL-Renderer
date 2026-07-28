#pragma once
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

// Phase 7: raw frame capture for the README GIF. Same discipline as the
// frame log (frame_log.h): one preallocation before the first frame, no
// allocation or file IO inside the render loop, everything written on exit.
// Frames are stored exactly as glReadPixels delivers them — tightly packed
// GL_RGB / GL_UNSIGNED_BYTE, rows bottom-up; bench/make_gif.py flips them.

class CaptureBuffer {
public:
    // One reserve for the whole run. Size comes from the FramebufferSize
    // side channel, which main populated before the render thread started.
    CaptureBuffer(uint32_t max_frames, int w, int h)
        : max_frames_(max_frames), width_(w), height_(h) {
        data_.resize(static_cast<size_t>(max_frames) * frame_bytes());
    }

    size_t frame_bytes() const {
        return static_cast<size_t>(width_) * static_cast<size_t>(height_) * 3;
    }

    // Hot path. The plane for the next frame, or nullptr when the buffer is
    // full or the framebuffer no longer matches the latched size — a
    // mid-capture resize stops the capture rather than corrupting it.
    uint8_t* next_frame(int w, int h) {
        if (frames_ == max_frames_) return nullptr;
        if (w != width_ || h != height_) {
            resized_ = true;
            return nullptr;
        }
        return data_.data() + static_cast<size_t>(frames_) * frame_bytes();
    }
    void commit() { ++frames_; }

    bool done() const { return frames_ == max_frames_ || resized_; }
    uint32_t frames() const { return frames_; }
    bool resized() const { return resized_; }
    int width() const { return width_; }
    int height() const { return height_; }

    // Exit-time only: raw planes to `path`, sidecar meta to `path` + ".json".
    bool write(const char* path, uint32_t fps) const {
        std::FILE* f = std::fopen(path, "wb");
        if (!f) {
            std::fprintf(stderr, "capture: cannot open '%s'\n", path);
            return false;
        }
        const size_t bytes = static_cast<size_t>(frames_) * frame_bytes();
        const bool data_ok =
            bytes == 0 || std::fwrite(data_.data(), 1, bytes, f) == bytes;
        if (std::fclose(f) != 0 || !data_ok) {
            std::fprintf(stderr, "capture: write error on '%s'\n", path);
            return false;
        }
        const std::string meta_path = std::string(path) + ".json";
        std::FILE* m = std::fopen(meta_path.c_str(), "w");
        if (!m) {
            std::fprintf(stderr, "capture: cannot open '%s'\n", meta_path.c_str());
            return false;
        }
        std::fprintf(m,
                     "{\"width\":%d,\"height\":%d,\"channels\":3,\"frames\":%u,"
                     "\"requested\":%u,\"fps\":%u,\"pixel_format\":\"RGB8\","
                     "\"row_order\":\"bottom-up\",\"resized\":%s}\n",
                     width_, height_, frames_, max_frames_, fps,
                     resized_ ? "true" : "false");
        const bool meta_ok = std::ferror(m) == 0;
        if (std::fclose(m) != 0 || !meta_ok) {
            std::fprintf(stderr, "capture: write error on '%s'\n", meta_path.c_str());
            return false;
        }
        return true;
    }

private:
    std::vector<uint8_t> data_;
    uint32_t max_frames_;
    uint32_t frames_ = 0;
    int width_, height_;
    bool resized_ = false;
};
