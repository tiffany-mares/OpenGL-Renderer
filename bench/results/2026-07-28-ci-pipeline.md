# CI benchmark pipeline — 2026-07-28

## Provenance
- **Workflow:** `.github/workflows/bench.yml` — weekly (Mondays 09:00 UTC) + workflow_dispatch (`frames` input; 1000 = pipeline smoke, 10000 = run of record). First full run: [run 30362432755](https://github.com/tiffany-mares/OpenGL-Renderer/actions/runs/30362432755), 2026-07-28, ~32 min wall, frames=10000, bot commit `6d7dbf0`. Pipeline smoke run (frames=1000): run 30361572688, bot commit `59d8b15`.
- **Runners:** windows-latest (Mesa llvmpipe via pinned mesa-dist-win 26.1.3, `GALLIUM_DRIVER=llvmpipe` — never the d3d12/WARP path) and ubuntu-latest (Mesa llvmpipe under `xvfb-run -a -s "-screen 0 1280x800x24"`, `LIBGL_ALWAYS_SOFTWARE=1`; the explicit 24-bit screen matters — xvfb's default is 8-bit and GL context creation fails).
- **Pipeline:** `bench/ci_bench.py` per runner (GL smoke → 13-cell matrix via run_matrix.py → handoff_bench micro-bench summarized `--micro-only` → export_hist.py with CI provenance) → artifact per OS → ONE bot commit of `bench/results/ci/<platform>/{pacing-summary.csv,handoff-summary.csv,frametime-hist.json,provenance.json}` → explicit `pages.yml` dispatch (GITHUB_TOKEN pushes do not fire `on: push` workflows — GitHub's recursion guard; the redeploy must be dispatched).
- **Data policy:** stable paths overwritten weekly; git history is the archive. The commit job runs only if BOTH OSes succeeded — no half-dated data.

## Measurement class
GitHub Actions shared virtualized runner with a software rasterizer (Mesa llvmpipe) and no AC/idle control -- a different measurement class from the desktop run of record; compare CI platforms to each other and across weeks, not to the desktop tables. Concretely: runner preemption inflates timer_spin/spin tails unpredictably; llvmpipe makes SwapBuffers a CPU memcpy, so uncapped rates and CPU% are incomparable to the desktop; there is no AC/idle control. Compare CI platforms to each other and across weeks. The desktop tables under bench/results/ remain the run of record and the dashboard default.

## Protocol
Identical 13-cell pacing matrix (frames per the input; warmup 500) + the full handoff micro-bench (cost + contended sweep). The six GL app cells are deliberately absent (`--micro-only`) — the dashboard reads only table=cost and table=sweep.

## First-run observations

Source: `bench/results/ci/windows-latest/{pacing-summary.csv,provenance.json}` and `bench/results/ci/ubuntu-latest/{pacing-summary.csv,provenance.json}`, committed at `6d7dbf0` (run 30362432755, frames=10000).

**ubuntu-latest held every rate with zero missed deadlines on all four strategies, including naive `sleep_for`.** Linux `nanosleep` is precise enough on this runner that the deadline-to-deadline `p50`/`p95`/`p99` columns degenerate exactly to the period (16,666,666 / 6,944,444 / 4,166,666 ns at 60/144/240 Hz) for every strategy — the summary table has nothing left to say and the frametime histograms carry the real story. Pure `spin` cost 75–94% CPU (93.62% at 60 Hz down to 75.23% at 240 Hz), confirming the busy-wait tax is real even where scheduling is clean.

**windows-latest shows the textbook ~15.6 ms scheduler tick** that the desktop machine notes documented this machine as *not* having: naive `sleep` missed 2,852/9,500 deadlines at 144 Hz (p95 16.411 ms — almost exactly one tick late; p99 17.310 ms) and 3,472/9,500 at 240 Hz (p95 12.211 ms, p99 14.893 ms), while the high-resolution-timer strategies held almost cleanly (`timer` missed 1/9,500 at 144 Hz, 9/9,500 at 240 Hz; `timer_spin` and `spin` missed none). This is the shared-runner Windows scheduler asserting itself exactly where a naive sleep is most exposed.

GL on the Windows runner: `llvmpipe (LLVM 22.1.8, 256 bits)`, GL 4.6 (Mesa 26.1.3); CPU `Intel64 Family 6 Model 207 Stepping 2, GenuineIntel`. GL on the Ubuntu runner: `llvmpipe (LLVM 20.1.2, 256 bits)`, GL 4.5 (Mesa 25.2.8); CPU `AMD EPYC 9V74 80-Core Processor`.

This cross-platform contrast — same code, three machines (desktop, windows-latest, ubuntu-latest), three different sleep behaviors — is the pipeline's first real payoff: it turns "naive `sleep_for` is unreliable" from a claim about one machine into an observed, dated fact across independent schedulers.

GITHUB_TOKEN/pages-dispatch gotcha, verified live: zero push-triggered workflows fired at either bot commit (`59d8b15`, `6d7dbf0`); `pages.yml` ran only via the explicit `gh workflow run pages.yml --ref main` dispatch in the commit job, confirming GitHub's recursion guard on bot-authored pushes and the need for that explicit step.

**Watch item:** the cron-path `frames` fallback (`${{ github.event.inputs.frames || '10000' }}`) has only been exercised via `workflow_dispatch`, where GitHub pre-populates the input default before the expression evaluates. The `|| '10000'` branch itself gets its first real exercise at the first scheduled (`cron`) run, where `github.event.inputs` is unset entirely.

## The ci_bench: line
`ci_bench: platform=<p> frames=<n> commit=<7-hex> run_date=<iso> out=<dir>`
