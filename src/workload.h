#pragma once
#include <cstdint>

// Phase 6a: fixed synthetic CPU workload — xorshift64 spun a fixed number of
// iterations. Instrumented runs execute this every frame so frame cost is a
// deterministic CPU quantity and the CSV measures the pacer, not GPU/driver
// variance. The end state is returned and must be consumed by the caller so
// the optimizer cannot delete the loop; it is also what makes the function
// testable (deterministic in, deterministic out).
inline uint64_t synthetic_workload(uint64_t iterations) {
    uint64_t x = 0x9E3779B97F4A7C15ull;  // non-zero seed (golden-ratio constant)
    for (uint64_t i = 0; i < iterations; ++i) {
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
    }
    return x;
}
