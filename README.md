# OpenGL-Renderer

A C++20 threaded OpenGL renderer whose real subject is three systems problems:
thread-affine graphics contexts, a lock-free input handoff, and precise frame
pacing on general-purpose OSes. The rotating cube is the demo, not the point.

## Build & run

    cmake -B build
    cmake --build build --config Release
    build/Release/cube.exe [--input=mutex|bitmask|seqlock]   # build/cube on Linux
    ctest --test-dir build -C Release --output-on-failure

Arrow keys rotate the cube, SPACE pauses the spin, ESC exits.

## Input handoff backends (`--input=`, default `mutex`)

The main thread polls input at ~1 kHz and publishes a snapshot; the render
thread consumes it once per frame. Three interchangeable backends sit behind
one interface (`src/input_state.h`):

| flag      | mechanism                                                        | carries                              |
|-----------|------------------------------------------------------------------|--------------------------------------|
| `mutex`   | `std::mutex` around a small POD — the baseline                   | keys + mouse delta + `publish_ns`    |
| `bitmask` | `std::atomic<uint32_t>`, one bit per key; writer `fetch_or`/`fetch_and` (release), reader one acquire load per frame | keys only |
| `seqlock` | sequence counter; writer never blocks, reader retries on odd/changed sequence | keys + mouse delta + `publish_ns` |

The payload carries a `uint64_t publish_ns` timestamp so end-to-end input
latency (consume time − publish time) can be measured. That field is why the
bitmask alone is not the final answer: it is genuinely lock-free with no torn
reads and no ABA, but 32 bits cannot carry a timestamp.

### The seqlock's data race, named

The seqlock reader copies the payload without synchronization while the writer
may be mid-store. Under the C++ memory model that is formally a data race —
undefined behavior. The sequence-number retry discards every torn copy on real
hardware, the fences around the copy are the standard practical construction
(Boehm, *Can seqlocks get along with programming language memory models?*),
and every shipping engine contains something shaped like this. It works on
every real CPU; it is still bending a rule of the abstract machine. Naming the
rule being bent is the signal — pretending there isn't one would be the bug.
