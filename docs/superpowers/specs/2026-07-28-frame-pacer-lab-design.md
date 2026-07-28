# Frame Pacer Lab — wiring the Lovable page to real benchmark data

**Date:** 2026-07-28
**Status:** Approved — all open decisions resolved with the user (see Resolved decisions at the end)

## Problem

`Frame Pacer Lab/` (untracked, Lovable-generated TanStack Start + React + Tailwind app inside
the repo) is a designed narrative page for the frame pacer — hero, backstory, how-it-works,
stat tiles, interactive histogram, results tables, decision log, limitations, build section.
Every number and most claims in it are fabricated:

- `src/lib/lab-data.ts` synthesizes histograms and metrics from a seeded RNG.
- It claims measurements on macOS 14 / M2 Pro and Linux 6.8 / 7950X that were never taken.
- The framing is "p99 jitter of a tuned pacer vs naive sleep" with a **fixed** spin margin;
  the real pacer's margin is adaptive (Welford), and real paced cells report
  p50 = p95 = p99 = max = period **by construction** (deadline-to-deadline frame time deviates
  only on a miss+resync — the repo's own docs are explicit about this).
- The build section clones a fictional `nine/frame-pacer` repo with flags (`--hz`,
  `--baseline`, `--csv`) that don't exist; the real CLI is `--fps`, `--pace`, `--bench-frames`,
  `--log`.
- The cube caption implies the canvas cube is the project's browser build; it is a decorative
  2D-canvas wireframe local to the page.

Goal of this step: make the page truthful — real data, real platforms, real prose, and the
**real wasm cube** embedded in place of the decorative wireframe. The folder is renamed
`lab/` and committed into this repo. Actually swapping the live Cloudflare deployment over
to this page is a **later step**; nothing here touches `web/`, `dist/`, or the workflows.

## Real data sources (all committed in this repo)

| Source | Content |
|---|---|
| `bench/results/2026-07-27-summary.csv` | Desktop (win11-arc) 13-cell pacing matrix: `strategy,rate_hz,n,p50_ns,p95_ns,p99_ns,max_ns,stddev_ns,missed,cpu_pct` |
| `web/data/frametime-hist.json` | Desktop start-to-start interval histograms, 50 µs bins, 13 cells, provenance embedded |
| `bench/results/2026-07-27-handoff-summary.csv` | Desktop handoff results: `cost` rows (per-op publish/read ns × 3 backends), `app` rows (in-app latency p50/p99 at 1 k/10 k poll), `sweep` rows (reader p99 vs publish rate, 5 rates × 3 backends) |
| `bench/results/ci/{windows-latest,ubuntu-latest}/pacing-summary.csv` | Same schema, CI runners (llvmpipe) |
| `bench/results/ci/<platform>/handoff-summary.csv` | Micro-only (`cost` + `sweep`; no `app` cells — those are the desktop protocol) |
| `bench/results/ci/<platform>/frametime-hist.json` | Same histogram schema, CI |
| `bench/results/ci/<platform>/provenance.json` | CI run provenance |
| `README.md` | Real decision log (5 entries), known limitations, build instructions, CLI reference |
| `bench/results/2026-07-27-pacing-matrix.md` | Desktop provenance + machine notes (incl. the timer_spin-60 214 ms one-off artifact) |

Real platforms: **win11-arc** (desktop, Intel Core Ultra 7 155H / Arc, run of record),
**windows-latest** (CI, llvmpipe — reproduces the classic 15.6 ms scheduler tick: naive sleep
missed 2,852/9,500 at 144 Hz), **ubuntu-latest** (CI, llvmpipe — near-zero misses everywhere).

Mapping for the page's tuned-vs-naive narrative: **tuned = `timer_spin`** (the shipping
default), **naive = `sleep`**. `timer` and `spin` are mentioned in prose with a link to the
full dashboard, not tabulated (keeps the page's two-series design; the dashboard already
covers all four strategies).

## Approaches considered

1. **Build-time generation (chosen).** A stdlib-only Node script
   `Frame Pacer Lab/scripts/gen-lab-data.mjs` reads the repo files above via relative paths,
   validates them, and writes `src/lib/lab-data.gen.json`, which is committed and imported
   statically. Pros: SSR-safe, no runtime fetch/CORS, page stays self-contained if it is ever
   built outside the repo (Lovable sync), numbers are verbatim-from-CSV with provenance.
   Weekly CI data refreshes flow through by re-running the script.
2. **Runtime fetch** from `opengl-renderer.pages.dev/data/*`. Rejected: CORS + SSR
   complexity, couples the page's correctness to another deployment's availability, breaks in
   Lovable preview.
3. **Hand-transcribed literals.** Rejected: transcription-error risk across ~80 numbers, rots
   when the weekly CI run refreshes `bench/results/ci/**`.

## Design

### 0. Folder rename + repo adoption

`Frame Pacer Lab/` → `lab/` (drops the space; one plain `git mv`-equivalent since the folder
is untracked: rename, then `git add lab/`). The lab keeps its own `.gitignore`
(`node_modules/`, build output); `cube.js`/`cube.wasm` staged into `lab/public/` are added to
that ignore list — they are build artifacts, same doctrine as `dist/`.

### 1. Data generator — `lab/scripts/gen-lab-data.mjs`

- Node ≥18, zero dependencies, run via `npm run gen-data` (kept out of `predev`/`prebuild`
  so the page builds from the committed JSON without the parent repo present; re-run
  manually after a weekly CI data refresh).
- Reads, per platform: pacing summary CSV, frametime-hist JSON, provenance (CI JSON files;
  for the desktop, the provenance object embedded in `web/data/frametime-hist.json`).
- Emits `src/lib/lab-data.gen.json` (committed):

```
{
  platforms: {
    "win11-arc": {
      label, sublabel,            // "Windows 11 · Arc (desktop)", machine string
      provenance: { run_date, source_commit|run_id, machine, results_doc },
      pacing: [ { strategy, hz, n, p50_ms, p95_ms, p99_ms, max_ms, stddev_ms,
                  missed, missed_pct, cpu_pct } × 13 ],
      hist: { bin_ms: 0.05, cells: { "sleep-60": {n, bins, counts}, ... } },
      handoff: {
        cost:  [ { backend, publish_ns, read_ns } × 3 ],
        sweep: [ { backend, target_hz, achieved_hz, read_p99_ns,
                   retries_per_sec, torn } × 15 ],
        app:   [ { backend, poll_hz, lat_p50_ns, lat_p99_ns, achieved_hz,
                   retries_per_sec } ]   // desktop only; empty [] for CI platforms
      }
    },
    "windows-latest": { ... }, "ubuntu-latest": { ... }
  }
}
```

- Histogram cells are trimmed to the 6 the page renders per platform
  (`sleep`/`timer_spin` × 60/144/240) to keep the JSON small.
- ns→ms conversion happens here (3 decimal places, matching the repo's MD tables);
  `missed_pct` = missed/n × 100. No other derivation — numbers stay verbatim.
- **Validation (FATAL, exit 1, matching the repo's build.py doctrine):** missing file,
  missing column, ≠13 pacing rows per platform, missing hist cell, `bin_ns` ≠ 50000,
  ≠3 handoff cost rows, ≠15 sweep rows, app rows present for the desktop but absent for CI,
  non-numeric field. All platforms are required — unlike build.py's skip-with-warning,
  the generator runs from a checkout where all three exist; failing loud beats silently
  shipping a two-platform page.

### 2. `src/lib/lab-data.ts` — same exports, real values

Keeps its public shape (the page components keep working) but becomes a thin layer over the
generated JSON:

- `type Platform = "win11-arc" | "windows-latest" | "ubuntu-latest"` (was
  `win11|macos|linux`); `PLATFORM_LABEL` from generated labels. Desktop stays the default.
- `metrics(hz, platform)` → looks up `timer_spin` and `sleep` rows: p99 frame time (ms),
  missed %, CPU % for the stat tiles. The fake "p99 jitter" framing is dropped; tiles become
  **p99 frame time**, **missed deadlines %**, **render-thread CPU %**, each with the naive
  comparison line.
- `histogram(hz, platform)` → real bins in ms from the generated JSON, aggregated 50 µs → 0.25 ms
  display bins (5:1), returning `{ms, naive, tuned}[]` like today. X-axis capped at 20 ms with
  an overflow note listing out-of-range mass (e.g. windows-latest sleep-240 mass beyond the cap,
  desktop timer_spin-60's single 214 ms stall) — same convention as the dashboard's cap.
- `RESULT_ROWS` / `BASELINE_ROWS` → derived: timer_spin (resp. sleep) rows for all
  3 platforms × 3 rates, columns `p50 / p95 / p99 / max / missed % / CPU %` (the fake
  "jitter deviation" columns go away). Table footnote explains the deadline-to-deadline
  convention (zeros are by construction; the histogram shows the start-to-start spread) and
  flags the timer_spin-60 desktop artifact.
- `DECISIONS` → adapted from the README's real five-entry decision log (worker-thread render,
  absolute deadlines, adaptive Welford margin, mutex-by-default handoff, hand-rolled mat4) in
  the page's decision/reasoning/"would change my mind" voice. The three fabricated decisions
  that contradict the codebase (fixed margin, process-wide timeBeginPeriod as the fix,
  "present exactly at the deadline") are removed.
- `LIMITATIONS` → the README's real ones (seqlock UB, macOS timers untuned, no GPU-side
  timing, one desktop machine) plus: CI platforms run llvmpipe software rasterization, and
  battery power throttles the Arc driver (why runs of record are AC-only).
- `BACKSTORY` / `HOW_IT_WORKS` → rewritten to the real system: threaded renderer (main
  thread polls input at 1 kHz and never touches GL; render thread owns the context), absolute
  deadlines (`next += period`) with misses counted and resynced never chased, sleep short by a
  Welford-estimated margin then spin, the four `--pace` strategies, per-frame CSV
  instrumentation with no IO in timed frames. Real headline numbers in prose: desktop naive
  sleep missed 4,750/9,500 at 144 Hz vs 0 for timer+spin at 12% of one core; windows-latest
  CI reproduces the 15.6 ms tick.
- Build section → real: clone `tiffany-mares/OpenGL-Renderer`, CMake commands from
  the README, real flags (`--fps 144 --pace=timer_spin --bench-frames 10000 --log out.csv`),
  `bench/run_matrix.py`.
- `KEYWORDS` highlight list → pruned/re-pointed to terms that exist in the new prose.

### 3. Components

- **`Histogram.tsx`:** consumes the same `Bin[]` shape; axis caption updates
  ("50 µs data · 0.25 ms display bins · n = 9,500 per cell"), x-max 20 ms, overflow note
  under the chart when the selected cells have out-of-range mass. Log y stays.
- **Cube — real wasm embed.** The decorative 2D wireframe `Cube.tsx` is replaced by a
  client-only component hosting the actual Emscripten build. The integration contract is
  what `web/index.html` already proves out: a `<canvas id="canvas" tabindex="0">`, a
  `window.Module = { canvas }` global set before load, and a `cube.js` script tag
  (which fetches `cube.wasm`). Component behavior:
  - SSR-guarded (`useEffect` only), with a module-scope boot-once flag — Emscripten
    modules can't be torn down, so dev hot-reload must not inject the script twice.
  - Keeps the live page's focus handling: click-to-focus, `preventDefault` on
    arrows/space while the canvas is focused; keeps the caption listing the controls.
  - The old overlay's fps / ms-per-frame readout stays (it measures rAF cadence via its
    own rAF loop — honest for the browser build).
  - Artifacts staged by `lab/scripts/stage-cube.mjs` (copies `../dist/cube.js` +
    `../dist/cube.wasm` into `lab/public/`), wired as `predev`/`prebuild`; FATALs with
    "run `python web/build.py --out dist` first" when `dist/` is missing or stale-empty.
    The staged files are git-ignored (build artifacts).
  - The boundary caption stays accurate and gets aligned with the live page's wording:
    single-threaded, rAF-paced, pacer and threaded render deliberately absent.
- **Handoff section (new).** A section after the pacing tables, in the SectionNav, covering
  the project's second system so the page fully supersedes the dashboard at swap time:
  - Short intro prose: the three `InputChannel` backends (mutex baseline, atomic bitmask,
    seqlock) and what the handoff benchmark measures.
  - **Per-op cost bars** — publish vs read ns per backend, from the `cost` rows. New SVG
    component in the lab's style (like `Histogram.tsx`; no chart library).
  - **Contended sweep** — reader p99 (ns) vs *achieved* publish rate, log-x, one series per
    backend, from the `sweep` rows. The ≈100 k publishes/s crossover annotation renders for
    the desktop platform only (repo doctrine: measured there, not asserted for llvmpipe CI).
    Captions carry the dashboard's honesty notes: batched/amortized cost is not single-call
    latency; points sit at achieved rates; the mutex tail below the crossover sits at the
    100 ns measurement floor.
  - **In-app latency mini-table** — desktop only (p50/p99 input latency at 1 k/10 k poll,
    with achieved poll rates); hidden for CI platforms with a one-line note that app cells
    are a desktop protocol. Bitmask shows "—" (it cannot carry the timestamp — the Phase 4
    asymmetry, worth one prose sentence).
  - The section reacts to the platform chip; the Hz chips don't apply (noted in the caption,
    same as the dashboard).
- **`index.tsx`:** platform chips render the three real platforms; hero subtitle and stat
  claims re-anchored to real numbers ("zero missed deadlines out of 9,500 at 144 Hz, and what
  it costs"); branding becomes `tiffany-mares / opengl-renderer` in the header breadcrumb,
  footer, `<title>`, and og/twitter meta (headline keeps the frame-deadline hook); the build
  section clones the real repo; header/footer links keep GitHub
  (`tiffany-mares/OpenGL-Renderer`), LinkedIn, contact; a link to the live dashboard is added.

### 4. Error handling

All data-shape risk is pushed into the generator (FATAL at build time). The page imports a
committed JSON — it has no runtime data-failure mode. TypeScript types on the generated
import keep the page honest about the schema. The cube stage script FATALs when artifacts
are absent (build); at runtime the cube component shows a small "demo failed to load" note
if the wasm module errors (e.g. WebGL2 unavailable), never a blank panel.

### 5. Testing / verification

- Generator: re-runnable and deterministic (same inputs → byte-identical output; no
  timestamps in the JSON). Its validation is the test for malformed input.
- Spot-check generated values against the committed MD tables: desktop sleep-144
  missed = 4750 / p99 = 9.792 ms / cpu 6.34%; timer_spin-144 cpu 11.96%; windows-latest
  sleep-144 missed = 2852; ubuntu-latest timer_spin rows all-zero misses. Handoff:
  desktop cost mutex 16.6/15.9 ns vs seqlock 2.3/2.1 ns; desktop seqlock unthrottled sweep
  ≈15.7 M achieved Hz; desktop app mutex@1k lat_p50 = 639.5 µs.
- `npm run build` (or `bun run build`) passes; dev-server visual pass over all
  3 platforms × 3 rates: tiles, histogram, tables, overflow notes, no NaN/undefined.
- Cube: build `dist/` once locally (emsdk at `C:\Users\tiffm\emsdk`), run the dev server,
  verify the wasm cube renders and spins, click-to-focus works, arrows yaw/pitch, SPACE
  pauses, console is clean, and hot-reload does not double-boot the module.
- Prose audit: every number in prose traceable to a committed file; no claim about macOS,
  VRR, capture cards, or fullscreen-compositor measurements survives.
- The C++ project, `web/`, and workflows are untouched — no ctest or CI impact.

## Out of scope (this step)

- Swapping the live Cloudflare deployment to serve this page (follow-up step; will fold the
  lab build + cube staging into `pages.yml` and retire `web/index.html`/`dashboard.js`).
  With the handoff section now in scope, the lab page covers both of the dashboard's
  sections. The only content not carried over: the full four-strategy histograms/tables
  (`timer`, `spin`, uncapped) — the page tells the tuned-vs-naive story and links to the
  committed results docs for the rest. Acceptable loss to confirm at swap time.
- Lovable sync mechanics (Lovable is treated as a one-time design export; future edits are
  made locally).

## Resolved decisions (with the user, 2026-07-28)

1. **Page role:** the lab page replaces opengl-renderer.pages.dev. Confirmed as the
   immediate follow-up step after this one: `pages.yml` gains the lab's Node build (static
   prerender — Direct Upload serves static files), wrangler deploys the lab output to the
   same Cloudflare project/URL, `gen-data` runs in that build so weekly CI data refreshes
   flow onto the page, and `web/index.html`/`dashboard.js` retire. Gated on the user
   approving the finished page locally first.
2. **Repo placement:** committed into this repo, renamed `lab/`.
3. **Branding:** `tiffany-mares / opengl-renderer`; headline keeps the frame-deadline hook.
4. **Cube:** embed the real wasm build in this step, replacing the decorative wireframe
   (integration surface verified small against `web/index.html`).
5. **Handoff section:** in scope for this step — the page gains a full input-handoff section
   (cost bars, contended sweep, desktop app-latency table) so it supersedes the dashboard.
