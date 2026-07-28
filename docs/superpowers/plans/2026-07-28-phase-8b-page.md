# Phase 8b: The Page — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Rebuild the live GitHub Pages site as one page, two halves — the WASM cube on top as a visual hook with a one-line browser-boundary note, and below it a static benchmark dashboard (Chart.js reading the repo's committed native-run data): frame-time histograms, the percentile tables, and the mutex-vs-lock-free comparison, filterable by target framerate and platform.

**Architecture:** A new stdlib exporter bins the surviving pacing-matrix raw per-frame logs (the exact run the committed tables came from — `bench/results/raw/20260727-210652/`, provenance `45f87ee`) into one committed sparse-histogram JSON; the dashboard is plain HTML + a vendored, pinned Chart.js 4.x + one `dashboard.js` that fetches the JSON and the two committed summary CSVs (staged under stable names by `web/build.py`). No WASM changes; `src/` and `pages.yml` untouched. Design executes the dataviz method: reference dark palette with fixed categorical slots, small-multiple histograms on a log count axis, validator-checked colors, tooltips everywhere, table views.

**Tech Stack:** Python stdlib (exporter), Chart.js 4.5.0 (vendored UMD, no build step), plain ES5-ish JS, GitHub Pages (existing workflow).

## Context

Phase 8 shipped the live cube; 8b makes the page show the work. The user's spec: "Top: the cube. Purely a visual hook… One line underneath explaining that pacing and threading are stripped in the browser build and why. Bottom: the benchmark dashboard. This is the actual live link. Static HTML plus a charting library reading the CSVs from your native runs. Frame time histograms, the p50/p95/p99/max tables, the mutex versus lock-free comparison, filterable by target framerate and platform. No WASM involved, works everywhere, and it shows the work."

Data reality, verified: the matrix raw dir of record survives on disk (13 cells × 10,000 frames), so real histograms come from the same run as the committed tables — nothing re-measured. All committed data is from one platform; the platform filter ships as a real control with one honest option (user-approved recommended default; AskUserQuestion timed out).

## Global Constraints

- **Data of record only.** Every number the page shows is fetched from committed files: `bench/results/2026-07-27-summary.csv`, `2026-07-27-handoff-summary.csv`, and the new `web/data/frametime-hist.json` (binned from raw dir `20260727-210652`, source commit `45f87ee`). Nothing re-measured; nothing numeric hardcoded in JS except axis/annotation labels justified by committed docs (the ≈100k crossover cites `2026-07-27-handoff.md`).
- **Histogram method = the committed figure's method**: start-to-start deltas of post-warmup `frame_start_ns` (warmup parsed from each cell's `bench:` line — the `bench/plot_frames.py` derivation, cite its `load_starts`), 50 µs bins (`BIN_NS = 50_000`), 18 ms x-cap with per-card overflow notes, log count axis floored at 0.8. The raw uncapped cell's files are `uncapped.csv/.txt` (no `-0` suffix); its JSON key is `uncapped-0`.
- **Interval count is 9,499 per cell** (9,500 post-warmup frames → one fewer intervals than summary.csv's n=9500); the exporter guards both numbers explicitly.
- **Charting library:** Chart.js **4.5.0**, vendored as `web/vendor/chart.umd.min.js`, committed verbatim, pinned like `extern/glad/gl.h` (exact URL + byte size + SHA-256 recorded in the provenance doc). No other JS dependencies, no build step, no CDN/external requests at runtime.
- **Dark-only page**; dataviz reference palette, dark column: page `#0d0d10`, card surface `#1a1a19`, ink `#ffffff`/`#c3c2b7`/muted `#898781`, grid `#2c2c2a`, baseline `#383835`. **Fixed categorical slots, never cycled, never repainted by filters:** strategies = slots 1–4 (sleep `#3987e5`, timer `#199e70`, timer_spin `#c98500`, spin `#008300`), uncapped = slot 8 (`#d95926`), backends = slots 5–7 (mutex `#9085e9`, bitmask `#e66767`, seqlock `#d55181`).
- **Palette validation is mandatory** (dataviz skill): from `C:\Users\tiffm\AppData\Local\Temp\claude\bundled-skills\2.1.199\b952dff9c954c561b5bcdd7bdf3bd526\dataviz`, run `node scripts/validate_palette.js "#3987e5,#199e70,#c98500,#008300,#d95926" --mode dark --surface "#1a1a19"` and `node scripts/validate_palette.js "#9085e9,#e66767,#d55181" --mode dark --surface "#1a1a19"`; paste verbatim results into the provenance doc. The dark set sits in the CVD floor band — the mandated relief is structural: titled single-series histogram panels + table chips; legend + in-ink end labels + table view for the sweep.
- **Forms:** histograms as small multiples (one titled panel per strategy at the selected rate; NEVER overlaid), bar marks, log y; percentile table as HTML (`tabular-nums`); handoff = grouped bar (cost) + log-x line (sweep, points at **achieved_hz** — the unthrottled cells land at each backend's real achieved rate, no synthetic "max" slot). Tooltips on all marks; shared index-mode tooltip on the sweep; legend for every ≥2-series chart; single-series panels titled instead.
- **Filters, one row above the dashboard:** segmented rate control (60/144/240/uncapped, default 144) scoping the histograms + pacing table only (handoff is rate-independent — its x-axis IS publish rate; the caption says so); platform combobox with exactly one option "Windows 11 · Core Ultra 7 155H · Intel Arc" + muted note "1 platform measured — more when measured".
- **The one-liner replaces** the current `<h2>What you are not seeing</h2>` + paragraph entirely. The existing Module/canvas/focus/preventDefault inline script block stays **byte-identical**.
- Python additions stdlib-only, house `FATAL:` style; `export_hist.py` has `--selftest` but is **never wired into ctest** — CI stays Python-free. `src/` untouched; native 15/15 ctest must still pass untouched (no rebuild needed, but T3 sanity-checks CI green).
- `pages.yml` unchanged (verified: it only runs `web/build.py` and uploads dist). `web/build.py` gains a FATAL-guarded 6-item staging list.
- Commit style: `feat:`/`docs:`/`fix:` with ` -- ` sub-clauses.

## File map

- Create: `bench/export_hist.py`, `web/data/frametime-hist.json` (committed derived artifact), `web/dashboard.js`, `web/vendor/chart.umd.min.js` (vendored), `bench/results/2026-07-28-web-dashboard.md`, `docs/superpowers/plans/2026-07-28-phase-8b-page.md` (plan copy, Task 1).
- Modify: `web/index.html` (full replacement below), `web/build.py` (staging block), `README.md` + `CLAUDE.md` (Task 4).
- Untouched: `src/`, `CMakeLists.txt`, `.github/workflows/*`, all existing `bench/results/*` files.

---

### Task 1: The exporter and the committed histogram JSON

**Files:**
- Create: `bench/export_hist.py`, `web/data/frametime-hist.json`, `docs/superpowers/plans/2026-07-28-phase-8b-page.md` (copy this plan verbatim)

**Interfaces:**
- Consumes: `bench/results/raw/20260727-210652/` (`<strategy>-<rate>.csv/.txt` for sleep|timer|timer_spin|spin × 60|144|240, plus `uncapped.csv/.txt`), `bench/results/2026-07-27-summary.csv` (row-count cross-check).
- Produces: `web/data/frametime-hist.json` with schema `{provenance:{generated_by,raw_dir,source_commit,run_date,machine,method,results_doc}, bin_ns:50000, cells:{"<strategy>-<rate>":{n,bins:[...],counts:[...]}}}` (sparse ascending bin indices, bin b covers `[b*50000,(b+1)*50000)` ns; key `uncapped-0` for the uncapped cell) and the parseable line `hist: cells=13 intervals_per_cell=9499 bin_ns=50000 nonzero_bins=<N> bytes=<B> out=<path>`. Task 2's renderer consumes this schema exactly.

- [ ] **Step 1: Write `bench/export_hist.py`:**

```python
#!/usr/bin/env python3
"""Phase 8b histogram exporter: raw pacing matrix -> web/data/frametime-hist.json.

Bins frame START-TO-START intervals at a fixed 50 us per bin for all 13
matrix cells and writes ONE committed JSON with embedded provenance. The
web dashboard renders this file; nothing is re-measured in the browser.

Start-to-start, deliberately NOT the CSV's frame_time_ns -- the same
reasoning and the same derivation as bench/plot_frames.py (load_starts +
consecutive deltas): deadline-to-deadline re-pins to the period after every
miss-resync, which would fake a spike for every strategy. Warmup is parsed
from each cell's bench: line and dropped.

The raw dir is git-ignored; this script runs on the bench machine and the
JSON is the committed derivative. Stdlib-only, like every runner in bench/.
Never wired into ctest -- CI stays Python-free.
"""
import argparse
import csv
import json
import re
import sys
from pathlib import Path

BIN_NS = 50_000  # 50 us -- same bin width as the committed README figure
STRATEGIES = ["sleep", "timer", "timer_spin", "spin"]
RATES = [60, 144, 240]
BENCH_RE = re.compile(r"bench: frames=(\d+) warmup=(\d+)")

DEFAULT_RAW = Path("bench") / "results" / "raw" / "20260727-210652"
SUMMARY_CSV = Path("bench") / "results" / "2026-07-27-summary.csv"
# Provenance of the run of record (bench/results/2026-07-27-pacing-matrix.md).
SOURCE_COMMIT = "45f87ee"
RUN_DATE = "2026-07-27"
MACHINE = ("Intel(R) Core(TM) Ultra 7 155H; Intel(R) Arc(TM) Graphics; "
           "Windows 11 Home; AC power, idle")
METHOD = ("start-to-start deltas of post-warmup frame_start_ns; warmup "
          "parsed from each cell's bench: line; bin index = delta // bin_ns; "
          "derivation identical to bench/plot_frames.py")


def cell_stem(strategy: str, rate: int) -> str:
    # The uncapped cell's files are uncapped.csv/.txt, not uncapped-0.*.
    return "uncapped" if strategy == "uncapped" else f"{strategy}-{rate}"


def load_starts(raw: Path, name: str) -> list:
    """Post-warmup frame_start_ns column (bench/plot_frames.py load_starts)."""
    txt_path = raw / f"{name}.txt"
    if not txt_path.is_file():
        sys.exit(f"FATAL: {txt_path} is missing -- wrong --raw dir?")
    m = BENCH_RE.search(txt_path.read_text())
    if not m:
        sys.exit(f"FATAL: no bench: line in {name}.txt -- was this a bench run?")
    warmup = int(m.group(2))
    with open(raw / f"{name}.csv", newline="") as f:
        rows = list(csv.DictReader(f))
    starts = [int(r["frame_start_ns"]) for r in rows[warmup:]]
    if len(starts) < 2:
        sys.exit(f"FATAL: {name}.csv has <2 post-warmup rows -- truncated log?")
    return starts


def bin_intervals(starts):
    """Sparse 50 us histogram: ascending bin indices + parallel counts.

    Bin b covers [b*BIN_NS, (b+1)*BIN_NS) ns."""
    acc = {}
    for a, b in zip(starts, starts[1:]):
        idx = (b - a) // BIN_NS
        acc[idx] = acc.get(idx, 0) + 1
    bins = sorted(acc)
    return bins, [acc[b] for b in bins]


def selftest() -> None:
    # deltas: 50_000, 50_000, 49_999, 200_001 -> bin 1 x2, bin 0 x1, bin 4 x1.
    # Checks the left-closed edge (a delta of exactly BIN_NS lands in bin 1).
    starts = [0, 50_000, 100_000, 149_999, 350_000]
    bins, counts = bin_intervals(starts)
    assert bins == [0, 1, 4], f"selftest bins {bins}"
    assert counts == [1, 2, 1], f"selftest counts {counts}"
    assert sum(counts) == len(starts) - 1
    print("selftest: ok")


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--raw", default=None,
                    help=f"raw pacing-matrix dir (default {DEFAULT_RAW})")
    ap.add_argument("--out", default=None,
                    help="output JSON (default web/data/frametime-hist.json)")
    ap.add_argument("--selftest", action="store_true",
                    help="run the synthetic binning check and exit")
    args = ap.parse_args()
    if args.selftest:
        selftest()
        return

    root = Path(__file__).resolve().parent.parent
    default_raw = args.raw is None
    raw = Path(args.raw) if args.raw else root / DEFAULT_RAW
    out = Path(args.out) if args.out else root / "web" / "data" / "frametime-hist.json"
    if not raw.is_dir():
        sys.exit(f"FATAL: raw dir {raw} not found -- the run of record is "
                 f"git-ignored; this script runs on the bench machine")

    summary = root / SUMMARY_CSV
    if not summary.is_file():
        sys.exit(f"FATAL: {summary} is missing -- nothing to cross-check against")
    with open(summary, newline="") as f:
        expected_n = {(r["strategy"], int(r["rate_hz"])): int(r["n"])
                      for r in csv.DictReader(f)}

    cell_list = [(s, r) for s in STRATEGIES for r in RATES] + [("uncapped", 0)]
    cells = {}
    interval_ns = set()
    for strategy, rate in cell_list:
        key = f"{strategy}-{rate}"
        if (strategy, rate) not in expected_n:
            sys.exit(f"FATAL: {key} absent from {summary.name}")
        starts = load_starts(raw, cell_stem(strategy, rate))
        if len(starts) != expected_n[(strategy, rate)]:
            sys.exit(f"FATAL: {key}: {len(starts)} post-warmup rows != "
                     f"summary n={expected_n[(strategy, rate)]} -- wrong raw dir?")
        bins, counts = bin_intervals(starts)
        n = len(starts) - 1  # intervals, one fewer than frames
        if sum(counts) != n:
            sys.exit(f"FATAL: {key}: bin counts sum {sum(counts)} != {n} intervals")
        cells[key] = {"n": n, "bins": bins, "counts": counts}
        interval_ns.add(n)
    if len(interval_ns) != 1:
        sys.exit(f"FATAL: cells have mismatched interval counts {sorted(interval_ns)}")

    doc = {
        "provenance": {
            "generated_by": "bench/export_hist.py",
            "raw_dir": raw.name if default_raw else str(raw),
            "source_commit": SOURCE_COMMIT if default_raw else "unspecified (non-default --raw)",
            "run_date": RUN_DATE if default_raw else "unspecified (non-default --raw)",
            "machine": MACHINE if default_raw else "unspecified (non-default --raw)",
            "method": METHOD,
            "results_doc": "bench/results/2026-07-27-pacing-matrix.md",
        },
        "bin_ns": BIN_NS,
        "cells": cells,
    }
    out.parent.mkdir(parents=True, exist_ok=True)
    text = json.dumps(doc, separators=(",", ":")) + "\n"
    out.write_text(text)

    nonzero = sum(len(c["bins"]) for c in cells.values())
    print(f"hist: cells={len(cells)} intervals_per_cell={interval_ns.pop()} "
          f"bin_ns={BIN_NS} nonzero_bins={nonzero} bytes={len(text)} out={out}",
          flush=True)


if __name__ == "__main__":
    main()
```

- [ ] **Step 2: Run the selftest first (TDD — the synthetic check passes before the real run):**

Run: `python bench/export_hist.py --selftest`
Expected: `selftest: ok`

- [ ] **Step 3: Real export:**

Run: `python bench/export_hist.py`
Expected: `hist: cells=13 intervals_per_cell=9499 bin_ns=50000 nonzero_bins=<N> bytes=<B> out=...frametime-hist.json` (record the verbatim line for Task 4). Expected size ≈ 8–20 KB.

- [ ] **Step 4: Spot-check against the committed figure** (Python one-liner or reading the JSON): `timer_spin-144`'s dominant bin index is 138 (6.90–6.95 ms — the README figure's ≈90% spike) with count ≈ 8,500+; `sleep-144` is **bimodal** — one cluster of immediate post-miss intervals below ~1 ms and one tick cluster ≈ 13–17.5 ms, roughly half the mass each (the README figure caption's "alternates ~15 ms frames with immediate post-miss frames — the two clusters"; plots.md documents the same shape). FATAL-stop and investigate if not. *(Gate corrected during execution: the original text expected 5–11 ms from the deadline-to-deadline percentiles — the exact start-to-start vs deadline-to-deadline confusion this project warns about.)*

- [ ] **Step 5: Copy this plan** to `docs/superpowers/plans/2026-07-28-phase-8b-page.md`, then **commit:**

```bash
git add bench/export_hist.py web/data/frametime-hist.json docs/superpowers/plans/2026-07-28-phase-8b-page.md
git commit -m "feat: committed 50 us frame-time histograms for the web dashboard -- export_hist.py replicates plot_frames.py start-to-start derivation; sparse JSON with embedded provenance; --selftest, never in ctest"
```

---

### Task 2: The page, the dashboard, the vendored library, the staging

**Files:**
- Create: `web/dashboard.js`, `web/vendor/chart.umd.min.js`
- Modify: `web/index.html` (full replacement), `web/build.py`

**Interfaces:**
- Consumes: Task 1's JSON schema; `dist/data/pacing-summary.csv` + `dist/data/handoff-summary.csv` (staged names for the two committed CSVs; handoff rows tagged `table=cost|app|sweep`, sweep unthrottled rows have `rate_hz=0` and real `achieved_hz`).
- Produces: the deployed page; the staging list in build.py; the validator PASS outputs and Chart.js hash for Task 4's provenance doc.

- [ ] **Step 1: Vendor Chart.js 4.5.0** (record URL, bytes, SHA-256):

```powershell
New-Item -ItemType Directory -Force web\vendor
Invoke-WebRequest "https://cdnjs.cloudflare.com/ajax/libs/Chart.js/4.5.0/chart.umd.min.js" -OutFile web\vendor\chart.umd.min.js
Get-FileHash web\vendor\chart.umd.min.js -Algorithm SHA256
(Get-Item web\vendor\chart.umd.min.js).Length
```

Fallback URL: `https://cdn.jsdelivr.net/npm/chart.js@4.5.0/dist/chart.umd.min.js`. If 4.5.0 is unavailable, pin the nearest 4.x actually vendored and record it.

- [ ] **Step 2: Run the palette validator** (both sets; record verbatim output). From `C:\Users\tiffm\AppData\Local\Temp\claude\bundled-skills\2.1.199\b952dff9c954c561b5bcdd7bdf3bd526\dataviz`:

```
node scripts/validate_palette.js "#3987e5,#199e70,#c98500,#008300,#d95926" --mode dark --surface "#1a1a19"
node scripts/validate_palette.js "#9085e9,#e66767,#d55181" --mode dark --surface "#1a1a19"
```

(These are the reference palette's own validated dark slots; expect PASS with the documented floor-band CVD note — the relief is structural per Global Constraints. `scripts/validate_palette.py` is the fallback if node is absent.)

- [ ] **Step 3: Replace `web/index.html`** with the following (the inline Module/canvas script block is byte-identical to the current one):

```html
<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>cube — OpenGL-Renderer: web demo + native benchmark dashboard</title>
<style>
  :root {
    color-scheme: dark;
    /* dataviz reference palette, dark column */
    --page: #0d0d10; --surface: #1a1a19;
    --ink: #ffffff; --ink-2: #c3c2b7; --muted: #898781;
    --grid: #2c2c2a; --baseline: #383835;
    --ring: rgba(255,255,255,0.10);
    /* categorical slots: strategies 1-4, backends 5-7, uncapped 8 */
    --s-sleep: #3987e5; --s-timer: #199e70;
    --s-timer-spin: #c98500; --s-spin: #008300; --s-uncapped: #d95926;
    --b-mutex: #9085e9; --b-bitmask: #e66767; --b-seqlock: #d55181;
  }
  body { margin: 0; background: var(--page); color: var(--ink-2);
         font: 15px/1.5 system-ui, -apple-system, "Segoe UI", sans-serif;
         display: flex; justify-content: center; }
  main { width: 100%; max-width: 980px; padding: 28px 16px 48px; }
  h1 { font-size: 1.3rem; font-weight: 600; color: var(--ink); margin: 0 0 4px; }
  h2 { font-size: 1.1rem; font-weight: 600; color: var(--ink);
       margin: 40px 0 4px; }
  .sub, .caption { color: var(--muted); margin: 0 0 16px; }
  .caption { font-size: 0.85rem; margin: 6px 0 0; }
  a { color: #7aa2c4; }

  /* ---- top half: the hook ---- */
  .hook { max-width: 760px; margin: 0 auto; }
  .hook canvas { display: block; width: 100%; background: #000;
                 border: 1px solid #26262c; border-radius: 4px; outline: none; }
  .hook canvas:focus { border-color: #4a6a8a; }
  .controls { color: var(--muted); margin-top: 10px; }
  kbd { background: #1c1c22; border: 1px solid #33333a; border-radius: 3px;
        padding: 0 5px; font-family: ui-monospace, "Cascadia Mono", monospace; }
  .boundary { border-left: 3px solid #33333a; padding-left: 14px;
              color: var(--ink-2); margin-top: 18px; }

  /* ---- dashboard ---- */
  .filters { display: flex; flex-wrap: wrap; align-items: center; gap: 12px;
             margin: 16px 0 8px; }
  .seg { display: inline-flex; border: 1px solid var(--baseline);
         border-radius: 6px; overflow: hidden; }
  .seg button { background: none; border: none; color: var(--ink-2);
                font: inherit; font-size: 0.85rem; padding: 5px 12px;
                cursor: pointer; }
  .seg button + button { border-left: 1px solid var(--baseline); }
  .seg button:hover { background: #232328; }
  .seg button.active { background: #2b2b31; color: var(--ink); font-weight: 600; }
  .filters select { background: var(--surface); color: var(--ink-2);
                    border: 1px solid var(--baseline); border-radius: 6px;
                    font: inherit; font-size: 0.85rem; padding: 5px 8px; }
  .filter-label { color: var(--muted); font-size: 0.85rem; }
  .filter-note { color: var(--muted); font-size: 0.8rem; }

  .card { background: var(--surface); border: 1px solid var(--ring);
          border-radius: 6px; padding: 12px 14px 10px; }
  .panel-grid { display: grid; gap: 12px; margin-top: 12px;
                grid-template-columns: repeat(auto-fit, minmax(300px, 1fr)); }
  .panel-title { margin: 0 0 6px; font-size: 0.85rem; font-weight: 600;
                 color: var(--ink); display: flex; align-items: center; gap: 7px; }
  .chip { width: 10px; height: 10px; border-radius: 3px; flex: none; }
  .chart-box { position: relative; height: 190px; }
  .chart-box.tall { height: 280px; }
  .overflow-note { color: var(--muted); font-size: 0.75rem; margin: 4px 0 0; }
  .two-col { display: grid; gap: 12px; margin-top: 12px;
             grid-template-columns: repeat(auto-fit, minmax(380px, 1fr)); }

  table { border-collapse: collapse; width: 100%; margin-top: 12px;
          font-size: 0.85rem; font-variant-numeric: tabular-nums; }
  th, td { padding: 5px 10px; text-align: right;
           border-bottom: 1px solid var(--grid); }
  th { color: var(--muted); font-weight: 500; }
  td { color: var(--ink-2); }
  th:first-child, td:first-child { text-align: left; }
  td:first-child { color: var(--ink); }

  details.table-view { margin-top: 10px; }
  details.table-view summary { color: var(--muted); font-size: 0.85rem;
                               cursor: pointer; }

  #data-error { display: none; background: #3a1d1d; border: 1px solid #7a3a3a;
                color: #f0c0c0; border-radius: 6px; padding: 12px 14px;
                margin: 16px 0; font-size: 0.9rem; }

  footer { margin-top: 44px; border-top: 1px solid var(--grid);
           padding-top: 14px; color: var(--muted); font-size: 0.8rem; }
</style>
</head>
<body>
<main>
  <!-- ============ hook: the wasm cube ============ -->
  <div class="hook">
    <h1>cube</h1>
    <p class="sub">The OpenGL-Renderer demo, compiled to WebAssembly —
    single-threaded and browser-paced, on purpose.</p>

    <canvas id="canvas" tabindex="0"></canvas>
    <p class="controls">Click the cube to give it keyboard focus, then:
      <kbd>Space</kbd> pauses the spin &nbsp;·&nbsp;
      <kbd>←</kbd> <kbd>→</kbd> yaw &nbsp;·&nbsp;
      <kbd>↑</kbd> <kbd>↓</kbd> pitch.</p>

    <p class="boundary">This build strips the two systems the project is
    actually about — the <strong>frame pacer</strong> (no high-resolution
    sleep or busy-spin on a browser main thread;
    <code>requestAnimationFrame</code> owns the frame clock) and the
    <strong>threaded render</strong> (Emscripten's offscreen-canvas paths
    don't support GLFW) — so every number below comes from the native
    Windows build:
    <a href="https://github.com/tiffany-mares/OpenGL-Renderer">full writeup
    on GitHub</a>.</p>
  </div>

  <!-- ============ dashboard: the committed native numbers ============ -->
  <div id="data-error" role="alert"></div>

  <div class="filters">
    <div class="seg" id="rate-seg" role="group" aria-label="target frame rate">
      <button data-rate="60">60 Hz</button>
      <button data-rate="144" class="active">144 Hz</button>
      <button data-rate="240">240 Hz</button>
      <button data-rate="uncapped">uncapped</button>
    </div>
    <label class="filter-label" for="platform">Platform</label>
    <select id="platform">
      <option value="win11-arc">Windows 11 · Core Ultra 7 155H · Intel Arc</option>
    </select>
    <span class="filter-note">1 platform measured — more when measured</span>
  </div>

  <h2 id="pacing-title">Frame pacing</h2>
  <p class="caption" id="pacing-caption">10,000 frames per cell, first 500
  dropped as warmup; intervals are frame <em>start-to-start</em> (the cadence
  a viewer experiences), 50&nbsp;µs bins, log count axis.</p>
  <div class="panel-grid" id="hist-grid"></div>
  <div id="pacing-table"></div>

  <h2>Input handoff: mutex vs lock-free</h2>
  <p class="caption">Rate-independent — this section's x-axis <em>is</em> the
  publish rate, so the rate filter above doesn't apply here.</p>
  <div class="two-col">
    <div class="card">
      <h4 class="panel-title">Uncontended per-op cost (ns, batched median)</h4>
      <div class="chart-box"><canvas id="cost-chart"></canvas></div>
      <p class="overflow-note">Amortized throughput from 1&nbsp;M-iteration
      batches — comparable to each other, not "what one isolated call costs."</p>
    </div>
    <div class="card">
      <h4 class="panel-title">Contended sweep — reader p99 vs publish rate</h4>
      <div class="chart-box tall"><canvas id="sweep-chart"></canvas></div>
      <p class="overflow-note">Points sit at each run's <em>achieved</em>
      publish rate; the unthrottled cells land at what each backend actually
      reached. Below ≈100&nbsp;k publishes/s the mutex's read tail is at the
      100&nbsp;ns measurement floor — the crossover from the committed
      handoff results.</p>
    </div>
  </div>
  <details class="table-view"><summary>table view</summary>
    <div id="handoff-tables"></div>
  </details>

  <footer id="provenance">
    Every number on this page is read from files committed to the repo —
    nothing is measured in the browser.
    Pacing: <a href="https://github.com/tiffany-mares/OpenGL-Renderer/blob/main/bench/results/2026-07-27-pacing-matrix.md">2026-07-27
    pacing matrix</a> (commit <code>45f87ee</code>); handoff:
    <a href="https://github.com/tiffany-mares/OpenGL-Renderer/blob/main/bench/results/2026-07-27-handoff.md">2026-07-27
    handoff benchmark</a> (commit <code>008b59a</code>); histograms binned
    from the pacing run's raw per-frame logs by
    <code>bench/export_hist.py</code>.
    Machine: Intel Core Ultra 7 155H · Intel Arc Graphics · Windows 11 Home,
    AC power, idle.
  </footer>
</main>
<script>
  var canvas = document.getElementById('canvas');
  canvas.addEventListener('click', function () { canvas.focus(); });
  window.addEventListener('keydown', function (e) { if (document.activeElement === canvas && ['ArrowLeft','ArrowRight','ArrowUp','ArrowDown',' '].indexOf(e.key) >= 0) e.preventDefault(); });
  var Module = { canvas: canvas };
</script>
<script src="vendor/chart.umd.min.js"></script>
<script src="dashboard.js"></script>
<script src="cube.js"></script>
</body>
</html>
```

(Dashboard scripts load before `cube.js` so the numbers render while the wasm streams.)

- [ ] **Step 4: Write `web/dashboard.js`:**

```js
/* Phase 8b dashboard: renders the committed native benchmark data.
 * Data of record only -- every number is fetched from dist/data/*, staged by
 * web/build.py from files committed under bench/results/ and web/data/.
 * Nothing is measured in the browser; nothing numeric is hardcoded here
 * except axis/annotation labels justified by the committed results docs
 * (the ~100k crossover line cites bench/results/2026-07-27-handoff.md). */
'use strict';

/* ---- design tokens (dataviz reference palette, dark column) ---- */
var TOKEN = {
  ink: '#ffffff', ink2: '#c3c2b7', muted: '#898781',
  grid: '#2c2c2a', baseline: '#383835', surface: '#1a1a19'
};
var STRATEGY = [ // categorical slots 1-4, fixed order, never cycled
  { key: 'sleep',      label: 'sleep_for (naive)',              color: '#3987e5' },
  { key: 'timer',      label: 'high-res timer',                 color: '#199e70' },
  { key: 'timer_spin', label: 'timer + spin (shipping default)', color: '#c98500' },
  { key: 'spin',       label: 'pure spin',                      color: '#008300' }
];
var UNCAPPED = { key: 'uncapped', label: 'uncapped (no target)', color: '#d95926' }; // slot 8
var BACKEND = [ // categorical slots 5-7
  { key: 'mutex',   label: 'mutex',   color: '#9085e9' },
  { key: 'bitmask', label: 'bitmask', color: '#e66767' },
  { key: 'seqlock', label: 'seqlock', color: '#d55181' }
];
var HIST_XCAP_MS = 18; // same cap as the committed README figure; overflow noted

var state = { rate: '144', platform: 'win11-arc' };
var DATA = null;        // { hist, pacing, handoff } after boot
var histCharts = [];    // destroyed/rebuilt on rate change

/* ---- tiny CSV splitter: simple comma CSVs with empty fields, no quoting ---- */
function parseCsv(text) {
  var lines = text.trim().split(/\r?\n/);
  var head = lines[0].split(',');
  return lines.slice(1).map(function (line) {
    var cells = line.split(',');
    var row = {};
    head.forEach(function (h, i) { row[h] = cells[i] !== undefined ? cells[i] : ''; });
    return row;
  });
}

function fatal(msg) {
  var box = document.getElementById('data-error');
  box.textContent = 'FATAL: ' + msg;
  box.style.display = 'block';
}

function el(tag, cls, text) {
  var e = document.createElement(tag);
  if (cls) e.className = cls;
  if (text !== undefined) e.textContent = text;
  return e;
}

function msFromNs(ns) { return (Number(ns) / 1e6).toFixed(3); }
function periodMs(rate) { return Math.floor(1e9 / Number(rate)) / 1e6; } // 10^9 // rate, as the pacer computes it
function fmtHz(v) {
  return v >= 1e6 ? (v / 1e6).toFixed(v >= 1e7 ? 0 : 1) + 'M'
       : v >= 1e3 ? Math.round(v / 1e3) + 'k' : String(Math.round(v));
}

function tooltipStyle(extra) {
  var base = {
    backgroundColor: '#232322', titleColor: TOKEN.ink, bodyColor: TOKEN.ink2,
    borderColor: TOKEN.baseline, borderWidth: 1, cornerRadius: 4,
    padding: 8, displayColors: false
  };
  if (extra) Object.keys(extra).forEach(function (k) { base[k] = extra[k]; });
  return base;
}

/* ---- inline plugins (no annotation plugin dependency) ---- */
var vlinePlugin = {
  id: 'vline',
  afterDraw: function (chart, _args, opts) {
    if (!opts || opts.x == null) return;
    var x = chart.scales.x.getPixelForValue(opts.x);
    var area = chart.chartArea;
    if (x < area.left || x > area.right) return;
    var ctx = chart.ctx;
    ctx.save();
    ctx.strokeStyle = TOKEN.muted;
    ctx.setLineDash([4, 3]);
    ctx.lineWidth = 1;
    ctx.beginPath(); ctx.moveTo(x, area.top); ctx.lineTo(x, area.bottom); ctx.stroke();
    if (opts.label) {
      ctx.setLineDash([]);
      ctx.fillStyle = TOKEN.ink2;
      ctx.font = '10px system-ui, sans-serif';
      ctx.textAlign = 'left'; ctx.textBaseline = 'top';
      ctx.fillText(opts.label, x + 5, area.top + 2);
    }
    ctx.restore();
  }
};
var endLabelPlugin = { // in-ink direct labels: identity never rides on color alone
  id: 'endLabels',
  afterDatasetsDraw: function (chart, _args, opts) {
    if (!opts || !opts.enabled) return;
    chart.data.datasets.forEach(function (ds, i) {
      var meta = chart.getDatasetMeta(i);
      var pt = meta.data[meta.data.length - 1];
      if (!pt) return;
      var ctx = chart.ctx;
      ctx.save();
      ctx.fillStyle = TOKEN.ink2;
      ctx.font = '10px system-ui, sans-serif';
      ctx.textAlign = 'right'; ctx.textBaseline = 'bottom';
      ctx.fillText(ds.label, pt.x - 2, pt.y - 6);
      ctx.restore();
    });
  }
};
Chart.register(vlinePlugin, endLabelPlugin);

Chart.defaults.font.family = 'system-ui, -apple-system, "Segoe UI", sans-serif';
Chart.defaults.font.size = 11;
Chart.defaults.color = TOKEN.muted;
Chart.defaults.animation = false;

function axisStyle(extra) {
  var base = { grid: { color: TOKEN.grid }, border: { color: TOKEN.baseline },
               ticks: { color: TOKEN.muted } };
  if (extra) {
    Object.keys(extra).forEach(function (k) {
      if (k === 'ticks') Object.keys(extra.ticks).forEach(function (t) { base.ticks[t] = extra.ticks[t]; });
      else base[k] = extra[k];
    });
  }
  return base;
}

/* ================= frame-pacing histograms (small multiples) ============= */
function panelsForRate(rate) {
  if (rate === 'uncapped') {
    return [{ def: UNCAPPED, cell: DATA.hist.cells['uncapped-0'] }];
  }
  return STRATEGY.map(function (s) {
    return { def: s, cell: DATA.hist.cells[s.key + '-' + rate] };
  });
}

function renderHistograms(rate) {
  var grid = document.getElementById('hist-grid');
  histCharts.forEach(function (c) { c.destroy(); });
  histCharts = [];
  grid.innerHTML = '';

  var panels = panelsForRate(rate);
  for (var i = 0; i < panels.length; i++) {
    if (!panels[i].cell) {
      return fatal('frametime-hist.json is missing cell ' +
                   panels[i].def.key + ' at rate ' + rate);
    }
  }
  var binMs = DATA.hist.bin_ns / 1e6; // 0.05

  /* shared scales across the multiples: one x window, one log-y ceiling */
  var maxEdge = 0, maxCount = 1;
  panels.forEach(function (p) {
    p.cell.bins.forEach(function (b, i) {
      var hi = (b + 1) * binMs;
      if (hi <= HIST_XCAP_MS && hi > maxEdge) maxEdge = hi;
      if (p.cell.counts[i] > maxCount) maxCount = p.cell.counts[i];
    });
  });
  var xmax = Math.min(HIST_XCAP_MS, Math.ceil(maxEdge));
  var nb = Math.round(xmax / binMs);
  var ymax = Math.pow(10, Math.ceil(Math.log10(maxCount)));
  var pline = rate === 'uncapped' ? null : periodMs(rate);

  panels.forEach(function (p) {
    var dense = new Array(nb).fill(null); // dense over the window: uniform bar ruler
    var overflow = 0;
    p.cell.bins.forEach(function (b, i) {
      if (b < nb) dense[b] = p.cell.counts[i];
      else overflow += p.cell.counts[i];
    });
    var points = dense.map(function (c, i) { return { x: (i + 0.5) * binMs, y: c }; });

    var card = el('div', 'card');
    var title = el('h4', 'panel-title');
    var chip = el('span', 'chip'); chip.style.background = p.def.color;
    title.appendChild(chip);
    title.appendChild(document.createTextNode(p.def.label));
    card.appendChild(title);
    var box = el('div', 'chart-box');
    var cv = document.createElement('canvas');
    box.appendChild(cv);
    card.appendChild(box);
    if (overflow) {
      card.appendChild(el('p', 'overflow-note',
        overflow + ' interval(s) beyond ' + HIST_XCAP_MS + ' ms not shown'));
    }
    grid.appendChild(card);

    histCharts.push(new Chart(cv, {
      type: 'bar',
      data: { datasets: [{
        label: p.def.label, data: points,
        backgroundColor: p.def.color, borderWidth: 0,
        barPercentage: 1.0, categoryPercentage: 1.0, grouped: false
      }] },
      options: {
        responsive: true, maintainAspectRatio: false,
        scales: {
          x: axisStyle({ type: 'linear', min: 0, max: xmax,
            title: { display: true, text: 'start-to-start interval (ms)',
                     color: TOKEN.muted, font: { size: 10 } },
            ticks: { maxTicksLimit: 7 } }),
          y: axisStyle({ type: 'logarithmic', min: 0.8, max: ymax,
            title: { display: true, text: 'intervals (log)',
                     color: TOKEN.muted, font: { size: 10 } },
            ticks: { callback: function (v) {
              return [1, 10, 100, 1000, 10000, 100000].indexOf(v) >= 0
                     ? v.toLocaleString() : '';
            } } })
        },
        plugins: {
          legend: { display: false }, // single series: the panel title names it
          tooltip: tooltipStyle({ callbacks: {
            title: function (items) {
              var x = items[0].raw.x;
              return (x - binMs / 2).toFixed(2) + '–' +
                     (x + binMs / 2).toFixed(2) + ' ms';
            },
            label: function (item) {
              return item.raw.y.toLocaleString() + ' of ' +
                     p.cell.n.toLocaleString() + ' intervals';
            }
          } }),
          vline: pline == null ? { x: null }
               : { x: pline, label: pline.toFixed(3) + ' ms period' }
        }
      }
    }));
  });
}

/* ======================= pacing percentile table ======================== */
function renderPacingTable(rate) {
  var host = document.getElementById('pacing-table');
  host.innerHTML = '';
  var defs = rate === 'uncapped' ? [UNCAPPED] : STRATEGY;
  var want = rate === 'uncapped' ? '0' : rate;
  var table = el('table');
  var thead = el('thead');
  var hr = el('tr');
  ['strategy', 'p50 ms', 'p95 ms', 'p99 ms', 'max ms', 'stddev ms',
   'missed', 'cpu %'].forEach(function (h) { hr.appendChild(el('th', null, h)); });
  thead.appendChild(hr);
  table.appendChild(thead);
  var tbody = el('tbody');
  defs.forEach(function (d) {
    var row = DATA.pacing.find(function (r) {
      return r.strategy === d.key && r.rate_hz === want;
    });
    if (!row) return fatal('pacing-summary.csv is missing ' + d.key + ',' + want);
    var tr = el('tr');
    var name = el('td');
    var chip = el('span', 'chip'); chip.style.background = d.color;
    chip.style.display = 'inline-block'; chip.style.marginRight = '7px';
    chip.style.verticalAlign = 'baseline';
    name.appendChild(chip);
    name.appendChild(document.createTextNode(d.label));
    tr.appendChild(name);
    [msFromNs(row.p50_ns), msFromNs(row.p95_ns), msFromNs(row.p99_ns),
     msFromNs(row.max_ns), msFromNs(row.stddev_ns),
     Number(row.missed).toLocaleString(), Number(row.cpu_pct).toFixed(1)
    ].forEach(function (v) { tr.appendChild(el('td', null, v)); });
    tbody.appendChild(tr);
  });
  table.appendChild(tbody);
  host.appendChild(table);
  document.getElementById('pacing-title').textContent =
    rate === 'uncapped' ? 'Frame pacing — uncapped baseline (no target)'
                        : 'Frame pacing at ' + rate + ' Hz';
}

/* ==================== handoff: cost + contended sweep =================== */
function renderCostChart() {
  var cost = {};
  DATA.handoff.filter(function (r) { return r.table === 'cost'; })
    .forEach(function (r) {
      cost[r.backend] = { publish: Number(r.publish_ns), read: Number(r.read_ns) };
    });
  BACKEND.forEach(function (b) {
    if (!cost[b.key]) fatal('handoff-summary.csv has no cost row for ' + b.key);
  });
  new Chart(document.getElementById('cost-chart'), {
    type: 'bar',
    data: {
      labels: ['publish', 'read'],
      datasets: BACKEND.map(function (b) {
        return { label: b.label, data: [cost[b.key].publish, cost[b.key].read],
                 backgroundColor: b.color, borderRadius: 3, maxBarThickness: 44 };
      })
    },
    options: {
      responsive: true, maintainAspectRatio: false,
      scales: {
        x: axisStyle({ grid: { display: false } }),
        y: axisStyle({ beginAtZero: true,
          title: { display: true, text: 'ns per op (amortized)',
                   color: TOKEN.muted, font: { size: 10 } } })
      },
      plugins: {
        legend: { position: 'top', align: 'end',
                  labels: { color: TOKEN.ink2, boxWidth: 9, boxHeight: 9 } },
        tooltip: tooltipStyle({ displayColors: true, callbacks: {
          label: function (item) {
            return item.dataset.label + ': ' + item.parsed.y + ' ns/op';
          }
        } })
      }
    }
  });
}

var SWEEP_TARGETS = ['1k', '10k', '100k', '1M', 'unthrottled'];
function renderSweepChart() {
  var rows = DATA.handoff.filter(function (r) { return r.table === 'sweep'; });
  var datasets = BACKEND.map(function (b) {
    var pts = rows.filter(function (r) { return r.backend === b.key; })
      .map(function (r) {
        return { x: Number(r.achieved_hz), y: Number(r.read_p99_ns),
                 target: r.rate_hz === '0' ? 'unthrottled' : fmtHz(Number(r.rate_hz)) + '/s' };
      })
      .sort(function (a, b2) { return a.x - b2.x; });
    if (pts.length !== 5) fatal('handoff-summary.csv: expected 5 sweep rows for ' +
                                b.key + ', got ' + pts.length);
    return { label: b.label, data: pts, borderColor: b.color,
             backgroundColor: b.color, pointRadius: 4, pointHitRadius: 10,
             borderWidth: 2, tension: 0 };
  });
  new Chart(document.getElementById('sweep-chart'), {
    type: 'line',
    data: { datasets: datasets },
    options: {
      responsive: true, maintainAspectRatio: false,
      interaction: { mode: 'index', intersect: false }, // crosshair-style shared tooltip
      scales: {
        x: axisStyle({ type: 'logarithmic', min: 800, max: 2e7,
          title: { display: true, text: 'achieved publishes/s (log)',
                   color: TOKEN.muted, font: { size: 10 } },
          ticks: { callback: function (v) {
            return { 1e3: '1k', 1e4: '10k', 1e5: '100k', 1e6: '1M', 1e7: '10M' }[v] || '';
          } } }),
        y: axisStyle({ beginAtZero: true,
          title: { display: true, text: 'reader p99 (ns)',
                   color: TOKEN.muted, font: { size: 10 } } })
      },
      plugins: {
        legend: { position: 'top', align: 'end',
                  labels: { color: TOKEN.ink2, boxWidth: 9, boxHeight: 9,
                            usePointStyle: true } },
        endLabels: { enabled: true },
        /* 1e5 is the measured crossover from bench/results/2026-07-27-handoff.md:
         * "first measurable departure is at ~100,000 publishes/s". */
        vline: { x: 1e5, label: '≈100k/s — mutex p99 first departs' },
        tooltip: tooltipStyle({ displayColors: true, callbacks: {
          title: function (items) {
            return 'target ' + (items[0].raw.target || SWEEP_TARGETS[items[0].dataIndex]);
          },
          label: function (item) {
            return item.dataset.label + ': ' + item.raw.y +
                   ' ns (achieved ' + fmtHz(item.raw.x) + '/s)';
          }
        } })
      }
    }
  });
}

function renderHandoffTables() {
  var host = document.getElementById('handoff-tables');
  host.innerHTML = '';
  var t1 = el('table');
  t1.innerHTML = '<thead><tr><th>backend</th><th>publish ns/op</th>' +
                 '<th>read ns/op</th></tr></thead>';
  var b1 = el('tbody');
  DATA.handoff.filter(function (r) { return r.table === 'cost'; })
    .forEach(function (r) {
      var tr = el('tr');
      [r.backend, r.publish_ns, r.read_ns].forEach(function (v) {
        tr.appendChild(el('td', null, v));
      });
      b1.appendChild(tr);
    });
  t1.appendChild(b1);
  host.appendChild(t1);

  var t2 = el('table');
  t2.innerHTML = '<thead><tr><th>backend</th><th>target Hz</th>' +
                 '<th>achieved Hz</th><th>read p99 ns</th>' +
                 '<th>retries/s</th></tr></thead>';
  var b2 = el('tbody');
  DATA.handoff.filter(function (r) { return r.table === 'sweep'; })
    .forEach(function (r) {
      var tr = el('tr');
      [r.backend, r.rate_hz === '0' ? 'unthrottled' : r.rate_hz,
       r.achieved_hz, r.read_p99_ns, r.retries_per_sec].forEach(function (v) {
        tr.appendChild(el('td', null, v));
      });
      b2.appendChild(tr);
    });
  t2.appendChild(b2);
  host.appendChild(t2);
}

/* ============================ filter wiring ============================= */
function wireFilters() {
  var seg = document.getElementById('rate-seg');
  Array.prototype.forEach.call(seg.querySelectorAll('button'), function (btn) {
    btn.addEventListener('click', function () {
      if (state.rate === btn.dataset.rate) return;
      state.rate = btn.dataset.rate;
      Array.prototype.forEach.call(seg.querySelectorAll('button'), function (b) {
        b.classList.toggle('active', b === btn);
      });
      /* rate change rebuilds ONLY the rate-scoped views; entity colors are
       * keyed by strategy/backend constants, so survivors never repaint */
      renderHistograms(state.rate);
      renderPacingTable(state.rate);
    });
  });
  document.getElementById('platform').addEventListener('change', function (e) {
    state.platform = e.target.value; // single honest option today; shape ships
  });
}

/* ================================ boot ================================== */
Promise.all([
  fetch('data/frametime-hist.json'),
  fetch('data/pacing-summary.csv'),
  fetch('data/handoff-summary.csv')
]).then(function (rs) {
  rs.forEach(function (r) {
    if (!r.ok) throw new Error(r.url + ' -> HTTP ' + r.status);
  });
  return Promise.all([rs[0].json(), rs[1].text(), rs[2].text()]);
}).then(function (loaded) {
  DATA = { hist: loaded[0], pacing: parseCsv(loaded[1]),
           handoff: parseCsv(loaded[2]) };
  if (!DATA.hist.cells || !DATA.hist.bin_ns) {
    throw new Error('frametime-hist.json has no cells/bin_ns -- wrong file staged?');
  }
  wireFilters();
  renderHistograms(state.rate);
  renderPacingTable(state.rate);
  renderCostChart();
  renderSweepChart();
  renderHandoffTables();
}).catch(function (e) {
  fatal('dashboard data failed to load — ' + e.message +
        '. (fetch needs http://; for a local preview run ' +
        '`python -m http.server -d dist`, file:// will not work)');
});
```

- [ ] **Step 5: Update `web/build.py`** — replace the current index.html guard + copy + final print with the staging list (and update the module docstring's staging sentence to name the page, dashboard, vendored Chart.js, and data):

```python
    # Phase 8b staging: page, dashboard, vendored Chart.js, committed data.
    # The two results CSVs stage under stable dashboard names; they stay
    # committed once under bench/results/, never duplicated in git.
    stage = [
        (root / "web" / "index.html", out / "index.html"),
        (root / "web" / "dashboard.js", out / "dashboard.js"),
        (root / "web" / "vendor" / "chart.umd.min.js",
         out / "vendor" / "chart.umd.min.js"),
        (root / "web" / "data" / "frametime-hist.json",
         out / "data" / "frametime-hist.json"),
        (root / "bench" / "results" / "2026-07-27-summary.csv",
         out / "data" / "pacing-summary.csv"),
        (root / "bench" / "results" / "2026-07-27-handoff-summary.csv",
         out / "data" / "handoff-summary.csv"),
    ]
    for src, dst in stage:
        if not src.is_file():
            sys.exit(f"FATAL: {src} is missing -- the site would deploy incomplete")
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(src, dst)

    print(f"wasm: js_bytes={js.stat().st_size} wasm_bytes={wasm.stat().st_size} "
          f"staged={len(stage)} out={out}", flush=True)
```

- [ ] **Step 6: Build and serve:**

Run (PowerShell): `C:\Users\tiffm\emsdk\emsdk_env.ps1` then `python web/build.py --out dist`, then `python -m http.server 8000 -d dist`
Expected: `wasm: js_bytes=... wasm_bytes=... staged=6 out=dist`.

- [ ] **Step 7: Browser verification** (controller, Chrome tools) at http://localhost:8000/:
  1. Cube still renders/spins; keys still work; console clean.
  2. Dashboard renders: 4 histogram panels at 144 Hz — timer_spin a single dominant spike at the 6.944 ms period line, sleep spread, log y floored at 0.8 (a count of 1 is visible, no zero-height lies), bars one-bin wide with visible gaps where bins are empty.
  3. Rate filter: 60/240 rebuild panels + table; uncapped shows ONE orange panel + single-row table; switching back to 144 restores; entity colors never change.
  4. Pacing table matches the committed values (spot: sleep-144 p50 7.153, timer_spin-144 cpu 12.0).
  5. Cost chart: 3-backend grouped bars, legend; sweep chart: 3 lines, log x, points at achieved rates (unthrottled points at ~5M/7.3M/15.7M), crossover line at 100k labeled, shared tooltip aligns by target, in-ink end labels present.
  6. Table view `<details>` opens with both handoff tables.
  7. Fallback if bars render hairline-thin (Chart.js excluding null points from the width ruler): set explicit `barThickness` computed as `Math.max(1, Math.floor(chartWidth / nb))` — apply only if observed.

- [ ] **Step 8: Commit:**

```bash
git add web/index.html web/dashboard.js web/vendor/chart.umd.min.js web/build.py
git commit -m "feat: rebuild the page as hook + static benchmark dashboard -- one-line boundary note; Chart.js 4.5.0 vendored; small-multiple histograms, pacing table, handoff cost + sweep with the 100k crossover; build.py stages committed data under stable names"
```

---

### Task 3: Deploy and live verification

**Files:** none (pages.yml verified unchanged — it runs build.py and uploads dist).

- [ ] **Step 1:** `git push` (plain, unchained).
- [ ] **Step 2:** Poll the `pages` and `build` workflows at the pushed head via the REST API until both complete `success` (Pages site already exists; enablement is idempotent).
- [ ] **Step 3:** Verify live at https://tiffany-mares.github.io/OpenGL-Renderer/ (cache-busting query if stale): repeat Task 2 Step 7 checks 1–5; confirm the three `data/*` fetches are HTTP 200 (network tab or absence of the FATAL banner); footer links resolve.

---

### Task 4: The record

**Files:**
- Create: `bench/results/2026-07-28-web-dashboard.md`
- Modify: `README.md`, `CLAUDE.md`

- [ ] **Step 1: Write `bench/results/2026-07-28-web-dashboard.md`** (fill every `<placeholder>` with real values from Tasks 1–2):

```markdown
# Web benchmark dashboard — 2026-07-28

## Provenance
- **Page:** https://tiffany-mares.github.io/OpenGL-Renderer/ — Phase 8b: wasm cube as hook, static dashboard below
- **Data of record:** bench/results/2026-07-27-summary.csv (pacing, commit `45f87ee`), 2026-07-27-handoff-summary.csv (handoff, commit `008b59a`), web/data/frametime-hist.json (binned from raw dir `bench/results/raw/20260727-210652/`, git-ignored — the exact run the committed pacing tables came from)
- **Nothing re-measured:** the browser fetches committed files; the WASM build is untouched.

## Exporter
Command: `python bench/export_hist.py`
Output (verbatim): `<the hist: line from Task 1>`
Method: start-to-start intervals of post-warmup frame_start_ns, warmup from each cell's bench: line — the bench/plot_frames.py derivation; 50 µs bins, sparse index/count pairs; self-check counts-sum == n per cell; cross-checked against summary.csv row counts.

## Vendored Chart.js
Version <actual>, fetched 2026-07-28 from <exact URL>; <bytes> bytes; SHA-256 `<hash>`. Committed verbatim at web/vendor/chart.umd.min.js — pinned like extern/glad/gl.h.

## Palette validation (dataviz reference palette, dark, surface #1a1a19)
<verbatim output of both validator runs; note: dark set in CVD floor band → titled single-series panels / legend + in-ink direct labels are the relief>

## Page weight
chart.umd.min.js <bytes> + dashboard.js <bytes> + frametime-hist.json <bytes> + 2 CSVs (~2 KB); cube.wasm dominates regardless.
```

- [ ] **Step 2: README retouches.** Replace the live-demo block (after the opening paragraph) with:

```markdown
**[Live demo + benchmark dashboard](https://tiffany-mares.github.io/OpenGL-Renderer/)** — the
cube compiled to WebAssembly as the hook, above a static dashboard of the
native build's committed benchmark results. The pacer and the threaded render
deliberately do not port; every number on the page is fetched from files in
this repo, none re-measured in the browser.
```

In "### The browser build", after the existing build/serve commands, add:

```markdown
The page below the cube is a static dashboard of the committed native
results: `web/build.py` stages `web/dashboard.js`, a vendored Chart.js
(`web/vendor/`, pinned like the glad header), `web/data/frametime-hist.json`
(exported from the pacing run of record by `bench/export_hist.py`), and the
two committed results CSVs under `dist/data/`. The dashboard fetches its
data, so previews need the `http.server` above — `file://` will not work.
Provenance: [bench/results/2026-07-28-web-dashboard.md](bench/results/2026-07-28-web-dashboard.md).
```

- [ ] **Step 3: CLAUDE.md** — append to the line-7 phase paragraph, before the final "update this file" convention (keep "Phases 0–8" as-is; 8b is part of Phase 8):

```
Phase 8b (complete 2026-07-28): the page as hook + dashboard — web/index.html now pairs the wasm cube (one-line boundary note replacing the long paragraph) with a static benchmark dashboard: web/dashboard.js + vendored Chart.js <version> (web/vendor/chart.umd.min.js, pinned like glad; SHA-256 in bench/results/2026-07-28-web-dashboard.md) reading dist/data/ staged by build.py's FATAL-guarded 6-item list (page, dashboard, vendor, web/data/frametime-hist.json, and the two committed results CSVs under stable names pacing-summary.csv/handoff-summary.csv — never duplicated in git). Histograms come from bench/export_hist.py (stdlib, --selftest, never in ctest — CI stays Python-free): start-to-start post-warmup intervals per plot_frames.py's derivation, sparse 50 µs bins for all 13 matrix cells from the git-ignored raw run of record 20260727-210652 (commit 45f87ee), provenance embedded in the JSON. Dashboard design follows the dataviz reference dark palette with fixed categorical slots (strategies 1–4, backends 5–7, uncapped 8; validator PASS recorded), small-multiple histograms on a log count axis with the 18 ms cap + overflow notes, pacing percentile table, handoff cost bars + log-x sweep plotted at achieved_hz with the ≈100k crossover line; rate filter (60/144/240/uncapped) scopes pacing only, platform filter ships with the single measured platform. pages.yml unchanged.
```

- [ ] **Step 4: Commit, push, verify:**

```bash
git add bench/results/2026-07-28-web-dashboard.md README.md CLAUDE.md
git commit -m "docs: record the web dashboard -- README retouch; provenance note (exporter line, Chart.js SHA-256, palette validator PASS); CLAUDE.md Phase 8b"
git push
```

Then confirm `build` (both legs) + `pages` green at the new head, and the live page still renders. Final whole-branch review per the SDD skill before this push (controller's discretion on ordering, matching prior phases: review before the docs push where practical).

---

## Verification (end-to-end)

1. `python bench/export_hist.py --selftest` → `selftest: ok`; real run prints the `hist:` line; JSON spot-check matches the README figure's known shape (timer_spin-144 spike in bin 138).
2. `python web/build.py --out dist` → `staged=6`; `http.server` + Chrome: full Task 2 Step 7 checklist (charts, filters, tooltips, annotations, table views, cube unaffected, console clean).
3. Live URL passes the same checks; both workflows green.
4. Palette validator PASS recorded; every displayed number traceable to a committed file (the reviewer re-checks the pacing table against summary.csv and the crossover annotation against handoff.md).
5. Native suite untouched — 15/15 if run (no C++ changes; only sanity).

## Risks (watch during execution)

- Chart.js linear-x bar width with null-filled dense arrays: verified approach, but Task 2 Step 7.7 carries the explicit `barThickness` fallback if bars render thin.
- Log-y bars: min 0.8 (plot_frames.py's own floor) keeps count=1 visible; empty bins are `null`, never 0-on-log.
- timer_spin-60's 214 ms outlier: beyond the 18 ms cap → per-card overflow note; still present in the committed JSON (data of record complete).
- `file://` preview: dead by design; FATAL banner + README both say to use `http.server`.
- Page weight: ~205 KB Chart.js + ~15 KB dashboard.js + 8–20 KB JSON; cube.wasm dominates regardless.

## Self-review notes

- Spec coverage: cube-as-hook + one-liner (T2 Step 3 `.boundary`), dashboard as the actual live link (T2–T3), histograms (T1 data + T2 small multiples), p50/p95/p99/max tables (T2 pacing table + handoff table view), mutex-vs-lock-free (cost + sweep + crossover), filterable by framerate (segmented control) and platform (single honest option, user-approved), static HTML + charting library reading committed CSVs, no WASM changes.
- Type consistency: JSON schema field names identical between export_hist.py (`bin_ns`, `cells`, `bins`, `counts`, `n`, key `uncapped-0`) and dashboard.js consumers; staged filenames identical between build.py and the three `fetch()` calls; slot hexes identical between index.html CSS vars, dashboard.js constants, and the validator commands.
- Deliberate choices: sweep x = achieved_hz (honest placement, no synthetic slot); sparse JSON + dense window expansion (size + uniform bar ruler + outlier handling in one move); dashboard scripts before cube.js (numbers render while wasm streams).
