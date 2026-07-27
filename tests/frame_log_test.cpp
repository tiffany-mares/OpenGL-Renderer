#include <cstdio>
#include <cstring>
#include <string>

#include "frame_log.h"
#include "workload.h"

static int g_failures = 0;

static void expect(bool cond, const char* what) {
    if (!cond) {
        std::fprintf(stderr, "FAIL %s\n", what);
        ++g_failures;
    }
}

static FrameRecord make_record(uint64_t frame) {
    FrameRecord r{};
    r.frame = frame;
    r.frame_start_ns = 1000 + frame;
    r.frame_end_ns = 2000 + frame;
    r.frame_time_ns = 6944444;
    r.sleep_requested_ns = 5000000;
    r.sleep_actual_ns = 5100000;
    r.input_latency_ns = 450000;
    r.missed = frame == 2 ? 1 : 0;
    return r;
}

static void log_suite() {
    std::fprintf(stderr, "-- frame_log\n");
    FrameLog log(2);
    expect(log.size() == 0 && log.dropped() == 0, "empty log");

    log.append(make_record(0));
    log.append(make_record(1));
    expect(log.size() == 2, "two records stored");

    // Full: drop and count, never reallocate mid-run.
    log.append(make_record(2));
    expect(log.size() == 2, "append past capacity drops");
    expect(log.dropped() == 1, "dropped counted");
}

static void csv_suite() {
    std::fprintf(stderr, "-- csv\n");
    const char* path = "frame_log_test_out.csv";
    {
        FrameLog log(4);
        log.append(make_record(0));
        log.append(make_record(1));
        log.append(make_record(2));
        expect(log.write_csv(path), "write_csv succeeds");
    }

    std::FILE* f = std::fopen(path, "r");
    expect(f != nullptr, "csv file exists");
    if (!f) return;
    char line[512];

    expect(std::fgets(line, sizeof line, f) != nullptr, "header line present");
    expect(std::strcmp(line,
        "frame,frame_start_ns,frame_end_ns,frame_time_ns,"
        "sleep_requested_ns,sleep_actual_ns,input_latency_ns,missed\n") == 0,
        "header exact");

    expect(std::fgets(line, sizeof line, f) != nullptr, "row 0 present");
    expect(std::strcmp(line,
        "0,1000,2000,6944444,5000000,5100000,450000,0\n") == 0,
        "row 0 exact");

    expect(std::fgets(line, sizeof line, f) != nullptr, "row 1 present");
    expect(std::fgets(line, sizeof line, f) != nullptr, "row 2 present");
    expect(std::strcmp(line,
        "2,1002,2002,6944444,5000000,5100000,450000,1\n") == 0,
        "row 2 exact (missed flag)");

    expect(std::fgets(line, sizeof line, f) == nullptr, "exactly 3 rows");
    std::fclose(f);
    std::remove(path);

    // Failure path: unopenable target reports false, no crash.
    FrameLog empty(1);
    expect(!empty.write_csv("no_such_dir_xyz/out.csv"), "bad path returns false");
}

static void workload_suite() {
    std::fprintf(stderr, "-- workload\n");
    expect(synthetic_workload(0) == 0x9E3779B97F4A7C15ull, "zero iterations returns seed");
    expect(synthetic_workload(1000) == synthetic_workload(1000), "deterministic");
    expect(synthetic_workload(1000) != synthetic_workload(1001), "iteration-sensitive");
    expect(synthetic_workload(1000) != 0, "nonzero result");
}

int main() {
    log_suite();
    csv_suite();
    workload_suite();
    if (g_failures) {
        std::fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("all frame log tests passed\n");
    return 0;
}
