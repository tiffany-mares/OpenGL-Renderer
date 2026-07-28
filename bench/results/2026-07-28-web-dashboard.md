# Web benchmark dashboard — 2026-07-28

## Provenance
- **Page:** https://opengl-renderer.pages.dev/ (moved from GitHub Pages in Phase 8d; the old URL redirects) — Phase 8b: wasm cube as hook, static dashboard below
- **Data of record:** bench/results/2026-07-27-summary.csv (pacing, commit `45f87ee`), 2026-07-27-handoff-summary.csv (handoff, commit `008b59a`), web/data/frametime-hist.json (binned from raw dir `bench/results/raw/20260727-210652/`, git-ignored — the exact run the committed pacing tables came from)
- **Nothing re-measured:** the browser fetches committed files; the WASM build is untouched.
- This matrix run's `timer_spin-144` concentration (9,361/9,499 ≈ 98.6% in bin 138) differs from the README figure's ≈90% because the figure comes from a separate run (the 6d 5-cell protocol, raw dir `20260728-022118`) — per-run shapes differ, and the dashboard's pacing caption states the run identity so the discrepancy isn't mistaken for an error. Two execution-time deviations from the brief are recorded on the page itself: that run-identity caption sentence, and the histogram overflow note's "(up to X.X ms)" enrichment giving the extent of each panel's out-of-window tail.

## Exporter
Command: `python bench/export_hist.py`
Output (verbatim): `hist: cells=13 intervals_per_cell=9499 bin_ns=50000 nonzero_bins=535 bytes=4527 out=C:\Users\tiffm\Desktop\OpenGL Renderer\OpenGL-Renderer\web\data\frametime-hist.json`
Method: start-to-start intervals of post-warmup frame_start_ns, warmup from each cell's bench: line — the bench/plot_frames.py derivation; 50 µs bins, sparse index/count pairs; self-check counts-sum == n per cell; cross-checked against summary.csv row counts.

## Vendored Chart.js
Version 4.5.0, fetched 2026-07-28 from `https://cdnjs.cloudflare.com/ajax/libs/Chart.js/4.5.0/chart.umd.min.js`; 208341 bytes; SHA-256 `2f27bcf471b2d69dd78494f6e2172fb28470eb843820e2f96bb85d39f9618d30`. Committed verbatim at web/vendor/chart.umd.min.js — pinned like extern/glad/gl.h.

## Palette validation (dataviz reference palette, dark, surface #1a1a19)

Command 1:
```
node scripts/validate_palette.js "#3987e5,#199e70,#c98500,#008300,#d95926" --mode dark --surface "#1a1a19"
```
Output:
```
Palette (dark, surface #1a1a19, categorical): 5 slots
  [PASS] Lightness band         all 5 inside L 0.48–0.67
  [PASS] Chroma floor           all 5 >= 0.1
  [WARN] CVD separation         worst adjacent #008300↔#c98500 ΔE 10.3 (protan) · tritan 15.7 · normal 73.5
  [PASS] Contrast vs surface    all 5 >= 3:1

  → ALL CHECKS PASS  (CVD in the 8–12 floor band is legal ONLY with secondary encoding: direct labels, gaps, or texture)
  scope: categorical palettes only. For a lone status/text color check WCAG text contrast; for a sequential ramp, lightness monotonicity.
```

Command 2:
```
node scripts/validate_palette.js "#9085e9,#e66767,#d55181" --mode dark --surface "#1a1a19"
```
Output:
```
Palette (dark, surface #1a1a19, categorical): 3 slots
  [PASS] Lightness band         all 3 inside L 0.48–0.67
  [PASS] Chroma floor           all 3 >= 0.1
  [PASS] CVD separation         worst adjacent #d55181↔#e66767 ΔE 23.7 (deutan) · tritan 7.9 · normal 25.4
  [PASS] Contrast vs surface    all 3 >= 3:1

  → ALL CHECKS PASS  (CVD in the 8–12 floor band is legal ONLY with secondary encoding: direct labels, gaps, or texture)
  scope: categorical palettes only. For a lone status/text color check WCAG text contrast; for a sequential ramp, lightness monotonicity.
```

Both palettes: ALL CHECKS PASS (node v22.18.0). The 5-slot set carries the documented floor-band CVD WARN, structural per the dataviz reference's global constraints, not a failure — the relief is titled single-series panels / legend plus in-ink direct labels, which is how the dashboard's small-multiple histograms and legends are built.

## Page weight
chart.umd.min.js 208341 bytes + dashboard.js 18094 bytes + frametime-hist.json 4527 bytes + 2 CSVs (~2 KB); cube.wasm dominates regardless.
