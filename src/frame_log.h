#pragma once
#include <cstdint>
#include <cstdio>
#include <vector>

// Phase 6a: per-run frame log. The buffer is preallocated once and appends
// never reallocate — a mid-run malloc or file write would show up in the very
// frame times this exists to measure. The CSV is written in one pass after
// the loop exits.

struct FrameRecord {
    uint64_t frame;
    uint64_t frame_start_ns;
    uint64_t frame_end_ns;
    uint64_t frame_time_ns;       // deadline to deadline, not work duration
    uint64_t sleep_requested_ns;  // duration handed to the OS sleep (0 = none)
    uint64_t sleep_actual_ns;     // duration the OS sleep actually took (0 = none)
    uint64_t input_latency_ns;    // consume time - payload publish_ns (0 = unknown)
    uint8_t missed;               // 1 if this frame's deadline was missed/resynced
};

class FrameLog {
public:
    explicit FrameLog(size_t capacity) : capacity_(capacity) {
        records_.reserve(capacity);
    }

    // Hot path: no allocation, no IO. Once full, drop and count — growing
    // would reallocate inside a timed frame.
    void append(const FrameRecord& r) {
        if (records_.size() < capacity_) records_.push_back(r);
        else ++dropped_;
    }

    size_t size() const { return records_.size(); }
    uint64_t dropped() const { return dropped_; }

    // Exit-time only. Returns false (with a stderr note) if the file cannot
    // be opened or a write fails.
    bool write_csv(const char* path) const {
        std::FILE* f = std::fopen(path, "w");
        if (!f) {
            std::fprintf(stderr, "frame log: cannot open '%s'\n", path);
            return false;
        }
        std::fprintf(f,
            "frame,frame_start_ns,frame_end_ns,frame_time_ns,"
            "sleep_requested_ns,sleep_actual_ns,input_latency_ns,missed\n");
        for (const FrameRecord& r : records_) {
            std::fprintf(f, "%llu,%llu,%llu,%llu,%llu,%llu,%llu,%u\n",
                         static_cast<unsigned long long>(r.frame),
                         static_cast<unsigned long long>(r.frame_start_ns),
                         static_cast<unsigned long long>(r.frame_end_ns),
                         static_cast<unsigned long long>(r.frame_time_ns),
                         static_cast<unsigned long long>(r.sleep_requested_ns),
                         static_cast<unsigned long long>(r.sleep_actual_ns),
                         static_cast<unsigned long long>(r.input_latency_ns),
                         static_cast<unsigned>(r.missed));
        }
        const bool ok = std::ferror(f) == 0;
        std::fclose(f);
        if (!ok) std::fprintf(stderr, "frame log: write error on '%s'\n", path);
        return ok;
    }

private:
    std::vector<FrameRecord> records_;
    size_t capacity_;
    uint64_t dropped_ = 0;
};
