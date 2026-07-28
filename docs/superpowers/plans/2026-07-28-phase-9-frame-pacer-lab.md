# Frame Pacer Lab — Real Data Wiring Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Adopt the Lovable-exported page as `lab/`, replace every fabricated number and claim with the repo's committed benchmark data, embed the real Emscripten cube, and add an input-handoff section — per `docs/superpowers/specs/2026-07-28-frame-pacer-lab-design.md`.

**Architecture:** A zero-dependency Node generator reads the committed CSVs/JSONs from the parent repo and writes a committed `lab-data.gen.json`; the page imports it statically (no runtime fetch). Prose lives in a new `lab-content.ts`; numeric accessors in a rewritten `lab-data.ts`. The wasm cube is the same two-file contract `web/index.html` uses (`window.Module = {canvas}` + `cube.js` script tag), staged from `dist/` by a prebuild script. Handoff charts are hand-rolled SVG like the existing `Histogram.tsx` — no chart library.

**Tech Stack:** TanStack Start + React 19 + Tailwind 4 (existing Lovable export), Node ≥18 stdlib for scripts. npm as package manager (`node v22.18.0` / `npm 11.14.1` verified on this machine).

## Global Constraints

- The C++ project, `web/`, `dist/`, and `.github/workflows/**` are **untouched** by this plan.
- Numbers are verbatim from committed files — the generator only converts ns→ms (3 decimals) and computes `missed_pct = missed/n*100` (2 decimals). No other derivation.
- Generator and stage script FATAL loudly (`process.exit(1)` with a `FATAL:` message) — never skip-with-warning (all inputs exist in this checkout).
- No new npm dependencies. Handoff charts are hand-rolled SVG.
- Branding: `tiffany-mares / opengl-renderer` (header breadcrumb, footer, `<title>`, og/twitter meta). Repo URL everywhere: `https://github.com/tiffany-mares/OpenGL-Renderer`. Live demo URL: `https://opengl-renderer.pages.dev/`.
- Platform ids exactly: `win11-arc`, `windows-latest`, `ubuntu-latest`. Desktop (`win11-arc`) is the default selection.
- Tuned = `timer_spin`, naive = `sleep`. The ≈100k crossover annotation renders for `win11-arc` only.
- `lab-data.gen.json` is committed; `lab/public/cube.js` + `lab/public/cube.wasm` are git-ignored build artifacts.
- All prose claims must be true of THIS project (threaded renderer, adaptive Welford margin, absolute deadlines). No macOS/VRR/capture-card/600k-frame claims.
- Commit after every task. Commit messages follow repo style: `type: summary -- detail (Phase 9)` with the `Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>` trailer.
- Run all commands from the repo root `C:\Users\tiffm\Desktop\OpenGL Renderer\OpenGL-Renderer` unless a step says otherwise. PowerShell syntax.

## File Structure

```
lab/                                  (renamed from "Frame Pacer Lab/")
  package.json                        Modify: predev/prebuild hooks, name
  .gitignore                          Modify: ignore staged cube artifacts
  scripts/
    gen-lab-data.mjs                  Create: bench data → lab-data.gen.json
    stage-cube.mjs                    Create: dist/cube.{js,wasm} → public/
  src/lib/
    lab-data.gen.json                 Generated + committed
    lab-data.ts                       Rewrite: types + accessors over gen JSON
    lab-content.ts                    Create: all prose (backstory, how-it-works,
                                      decisions, limitations, handoff, build)
  src/components/lab/
    Histogram.tsx                     Rewrite: real bins, 20 ms cap, overflow note
    Cube.tsx                          Delete (decorative wireframe)
    CubeWasm.tsx                      Create: real Emscripten embed + fps overlay
    HandoffCost.tsx                   Create: per-op cost bars (SVG)
    HandoffSweep.tsx                  Create: contended sweep, log-x (SVG)
    SectionNav.tsx                    Modify: add "input handoff" item
  src/routes/
    index.tsx                         Rewrite: real sections, branding, handoff
    __root.tsx                        Modify: meta branding
CLAUDE.md                             Modify: one Phase 9 paragraph (final task)
```

Data sources consumed by the generator (paths relative to repo root):
`bench/results/2026-07-27-summary.csv`, `bench/results/2026-07-27-handoff-summary.csv`,
`web/data/frametime-hist.json`, and for each CI platform `p` in
{`windows-latest`, `ubuntu-latest`}: `bench/results/ci/<p>/pacing-summary.csv`,
`bench/results/ci/<p>/handoff-summary.csv`, `bench/results/ci/<p>/frametime-hist.json`,
`bench/results/ci/<p>/provenance.json`.

---

### Task 1: Adopt the folder as `lab/`, install, baseline build

**Files:**
- Rename: `Frame Pacer Lab/` → `lab/`
- Modify: `lab/.gitignore`, `lab/package.json` (name only in this task)

**Interfaces:**
- Produces: a tracked `lab/` directory whose `npm run build` passes — every later task builds on it.

- [ ] **Step 1: Rename the folder**

```powershell
Rename-Item "Frame Pacer Lab" "lab"
```

- [ ] **Step 2: Append artifact ignores to `lab/.gitignore`**

Append these lines (keep whatever Lovable already put there):

```
# staged Emscripten artifacts (built by web/build.py, copied by scripts/stage-cube.mjs)
public/cube.js
public/cube.wasm
```

- [ ] **Step 3: Set the package name**

In `lab/package.json` change `"name": "tanstack_start_ts"` to `"name": "opengl-renderer-lab"`.

- [ ] **Step 4: Install and verify the untouched export builds**

```powershell
cd lab; npm install; npm run build; cd ..
```

Expected: install completes; `vite build` exits 0. (This is the baseline gate — if the pristine Lovable export doesn't build, stop and report before changing anything.)

- [ ] **Step 5: Verify node_modules is ignored, then commit**

```powershell
git status --short lab | Select-Object -First 20
```

Expected: `lab/` files listed as untracked, **no** `lab/node_modules/` entries (Lovable's `.gitignore` covers it; if not, add `node_modules/` to `lab/.gitignore` first).

```powershell
git add lab "docs/superpowers/plans/2026-07-28-phase-9-frame-pacer-lab.md"
git commit -m @'
feat: adopt the Lovable lab page as lab/ -- pristine export, builds clean (Phase 9)

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>
'@
```

---

### Task 2: Data generator + committed `lab-data.gen.json`

**Files:**
- Create: `lab/scripts/gen-lab-data.mjs`
- Create (generated): `lab/src/lib/lab-data.gen.json`
- Modify: `lab/package.json` (add `gen-data` script)

**Interfaces:**
- Produces: `lab/src/lib/lab-data.gen.json` with this exact shape (Task 3 types it):

```
{ platforms: { "<id>": {
    label,            // chip label, e.g. "Windows 11 · Arc"
    sublabel,         // long machine/runner string
    provenance: { run_date, commit, source, results_url, measurement_class },
                      // measurement_class: "" for win11-arc
    pacing:  [ { strategy, hz, n, p50_ms, p95_ms, p99_ms, max_ms, stddev_ms,
                 missed, missed_pct, cpu_pct } × 13 ],
    hist:    { bin_ms: 0.05, cells: { "sleep-60": {n, bins, counts},
               "sleep-144": …, "sleep-240": …, "timer_spin-60": …,
               "timer_spin-144": …, "timer_spin-240": … } },
    handoff: { cost:  [ { backend, publish_ns, read_ns } × 3 ],
               sweep: [ { backend, target_hz, achieved_hz, read_p99_ns,
                          retries_per_sec, torn } × 15 ],
               app:   [ { backend, poll_hz, lat_p50_ns, lat_p99_ns,
                          achieved_hz, retries_per_sec } ] }  // 6 rows desktop, [] CI
} } }
```

- [ ] **Step 1: Write the generator**

Create `lab/scripts/gen-lab-data.mjs`:

```js
// Reads the repo's committed benchmark results and writes src/lib/lab-data.gen.json.
// Zero dependencies; FATALs on any missing/malformed input (all inputs are committed
// in this checkout -- silence would ship a wrong page). ns->ms is the only derivation.
import { readFileSync, writeFileSync } from "node:fs";
import { join, dirname } from "node:path";
import { fileURLToPath } from "node:url";

const ROOT = join(dirname(fileURLToPath(import.meta.url)), "..", "..");
const OUT = join(dirname(fileURLToPath(import.meta.url)), "..", "src", "lib", "lab-data.gen.json");

function fatal(msg) { console.error(`FATAL: ${msg}`); process.exit(1); }

function read(rel) {
  try { return readFileSync(join(ROOT, rel), "utf8"); }
  catch { fatal(`${rel} is missing or unreadable`); }
}

function num(v, ctx) {
  const n = Number(v);
  if (v === "" || v === undefined || Number.isNaN(n)) fatal(`non-numeric "${v}" in ${ctx}`);
  return n;
}
const ms = (ns) => Number((ns / 1e6).toFixed(3));

function parseCsv(text, rel) {
  const lines = text.trim().split(/\r?\n/);
  const header = lines[0].split(",");
  return lines.slice(1).map((line) => {
    const cells = line.split(",");
    if (cells.length !== header.length) fatal(`ragged row in ${rel}: ${line}`);
    return Object.fromEntries(header.map((h, i) => [h, cells[i]]));
  });
}

const PACING_COLS = ["strategy", "rate_hz", "n", "p50_ns", "p95_ns", "p99_ns",
  "max_ns", "stddev_ns", "missed", "cpu_pct"];
function loadPacing(rel) {
  const rows = parseCsv(read(rel), rel);
  if (rows.length !== 13) fatal(`${rel}: expected 13 pacing rows, got ${rows.length}`);
  for (const c of PACING_COLS) if (!(c in rows[0])) fatal(`${rel}: missing column ${c}`);
  return rows.map((r) => {
    const n = num(r.n, rel), missed = num(r.missed, rel);
    return {
      strategy: r.strategy, hz: num(r.rate_hz, rel), n,
      p50_ms: ms(num(r.p50_ns, rel)), p95_ms: ms(num(r.p95_ns, rel)),
      p99_ms: ms(num(r.p99_ns, rel)), max_ms: ms(num(r.max_ns, rel)),
      stddev_ms: ms(num(r.stddev_ns, rel)),
      missed, missed_pct: Number(((missed / n) * 100).toFixed(2)),
      cpu_pct: num(r.cpu_pct, rel),
    };
  });
}

const HIST_CELLS = ["sleep-60", "sleep-144", "sleep-240",
  "timer_spin-60", "timer_spin-144", "timer_spin-240"];
function loadHist(rel) {
  const j = JSON.parse(read(rel));
  if (j.bin_ns !== 50000) fatal(`${rel}: bin_ns ${j.bin_ns}, expected 50000`);
  const cells = {};
  for (const key of HIST_CELLS) {
    const c = j.cells?.[key];
    if (!c || !Array.isArray(c.bins) || !Array.isArray(c.counts)
        || c.bins.length !== c.counts.length) fatal(`${rel}: bad/missing cell ${key}`);
    cells[key] = { n: c.n, bins: c.bins, counts: c.counts };
  }
  return { provenance: j.provenance, hist: { bin_ms: 0.05, cells } };
}

function loadHandoff(rel, expectApp) {
  const rows = parseCsv(read(rel), rel);
  const by = (t) => rows.filter((r) => r.table === t);
  const cost = by("cost").map((r) => ({
    backend: r.backend, publish_ns: num(r.publish_ns, rel), read_ns: num(r.read_ns, rel),
  }));
  const sweep = by("sweep").map((r) => ({
    backend: r.backend, target_hz: num(r.rate_hz, rel),
    achieved_hz: num(r.achieved_hz, rel), read_p99_ns: num(r.read_p99_ns, rel),
    retries_per_sec: num(r.retries_per_sec, rel), torn: num(r.torn, rel),
  }));
  const app = by("app").map((r) => ({
    backend: r.backend, poll_hz: num(r.rate_hz, rel),
    lat_p50_ns: r.lat_p50_ns === "" ? null : num(r.lat_p50_ns, rel),
    lat_p99_ns: r.lat_p99_ns === "" ? null : num(r.lat_p99_ns, rel),
    achieved_hz: num(r.achieved_hz, rel), retries_per_sec: num(r.retries_per_sec, rel),
  }));
  if (cost.length !== 3) fatal(`${rel}: expected 3 cost rows, got ${cost.length}`);
  if (sweep.length !== 15) fatal(`${rel}: expected 15 sweep rows, got ${sweep.length}`);
  if (expectApp && app.length !== 6) fatal(`${rel}: expected 6 app rows, got ${app.length}`);
  if (!expectApp && app.length !== 0) fatal(`${rel}: expected 0 app rows, got ${app.length}`);
  return { cost, sweep, app };
}

// ---- desktop (run of record) ----
const desktopHist = loadHist("web/data/frametime-hist.json");
const dp = desktopHist.provenance;
const desktop = {
  label: "Windows 11 · Arc",
  sublabel: dp.machine,
  provenance: {
    run_date: dp.run_date, commit: dp.source_commit,
    source: dp.results_doc,
    results_url: `https://github.com/tiffany-mares/OpenGL-Renderer/blob/main/${dp.results_doc}`,
    measurement_class: "",
  },
  pacing: loadPacing("bench/results/2026-07-27-summary.csv"),
  hist: desktopHist.hist,
  handoff: loadHandoff("bench/results/2026-07-27-handoff-summary.csv", true),
};

// ---- CI platforms ----
function loadCi(id, chipLabel) {
  const base = `bench/results/ci/${id}`;
  const prov = JSON.parse(read(`${base}/provenance.json`));
  const h = loadHist(`${base}/frametime-hist.json`);
  return {
    label: chipLabel,
    sublabel: prov.label,
    provenance: {
      run_date: prov.run_date, commit: prov.commit,
      source: `run ${prov.run_id}`, results_url: prov.run_url,
      measurement_class: prov.measurement_class,
    },
    pacing: loadPacing(`${base}/pacing-summary.csv`),
    hist: h.hist,
    handoff: loadHandoff(`${base}/handoff-summary.csv`, false),
  };
}

const out = {
  platforms: {
    "win11-arc": desktop,
    "windows-latest": loadCi("windows-latest", "CI · Windows"),
    "ubuntu-latest": loadCi("ubuntu-latest", "CI · Ubuntu"),
  },
};
writeFileSync(OUT, JSON.stringify(out, null, 1) + "\n");
console.log(`wrote ${OUT}`);
```

- [ ] **Step 2: Add the npm script**

In `lab/package.json` `"scripts"`, add:

```json
"gen-data": "node scripts/gen-lab-data.mjs",
```

- [ ] **Step 3: Run it and verify the FATAL path**

```powershell
cd lab; npm run gen-data
```

Expected: `wrote ...lab-data.gen.json`, exit 0.

Then prove FATAL behavior by pointing at a broken root:

```powershell
Copy-Item ..\bench\results\2026-07-27-summary.csv ..\bench\results\2026-07-27-summary.csv.bak
Set-Content ..\bench\results\2026-07-27-summary.csv "strategy,rate_hz`nsleep,60"
node scripts/gen-lab-data.mjs; echo "exit=$LASTEXITCODE"
Move-Item ..\bench\results\2026-07-27-summary.csv.bak ..\bench\results\2026-07-27-summary.csv -Force
```

Expected: the middle run prints `FATAL: ... expected 13 pacing rows ...` (or missing-column) and `exit=1`. Restore step must leave `git status` clean for `bench/`.

- [ ] **Step 4: Spot-check generated values against the committed MD tables**

```powershell
node -e "const d=require('./src/lib/lab-data.gen.json').platforms; const w=d['win11-arc']; const row=(p,s,h)=>d[p].pacing.find(r=>r.strategy===s&&r.hz===h); const a=(c,m)=>{if(!c){console.error('SPOT-CHECK FAIL: '+m);process.exit(1)}}; a(row('win11-arc','sleep',144).missed===4750,'desktop sleep-144 missed'); a(row('win11-arc','sleep',144).p99_ms===9.792,'desktop sleep-144 p99'); a(row('win11-arc','sleep',144).cpu_pct===6.34,'desktop sleep-144 cpu'); a(row('win11-arc','timer_spin',144).cpu_pct===11.96,'desktop timer_spin-144 cpu'); a(row('windows-latest','sleep',144).missed===2852,'ci-win sleep-144 missed'); a(d['ubuntu-latest'].pacing.filter(r=>r.strategy==='timer_spin').every(r=>r.missed===0),'ubuntu timer_spin zero missed'); const c=w.handoff.cost.find(r=>r.backend==='mutex'); a(c.publish_ns===16.6&&c.read_ns===15.9,'desktop mutex cost'); const sq=w.handoff.sweep.filter(r=>r.backend==='seqlock'); a(Math.round(Math.max(...sq.map(r=>r.achieved_hz))/1e5)===157,'desktop seqlock unthrottled ~15.7M'); a(w.handoff.app.find(r=>r.backend==='mutex'&&r.poll_hz===1000).lat_p50_ns===639500,'desktop app mutex@1k p50'); a(w.handoff.app.find(r=>r.backend==='bitmask'&&r.poll_hz===1000).lat_p50_ns===null,'bitmask null latency'); console.log('spot-checks PASS')"
```

Expected: `spot-checks PASS`.

- [ ] **Step 5: Determinism check + commit**

```powershell
npm run gen-data; git -C .. status --short lab/src/lib/lab-data.gen.json
```

Expected: second run produces no diff (file already staged/unchanged content).

```powershell
cd ..
git add lab/scripts/gen-lab-data.mjs lab/src/lib/lab-data.gen.json lab/package.json
git commit -m @'
feat: lab data generator -- committed bench CSVs/JSONs to lab-data.gen.json, FATAL-guarded (Phase 9)

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>
'@
```

---

### Task 3: `lab-data.ts` — typed accessors over the generated JSON

**Files:**
- Rewrite: `lab/src/lib/lab-data.ts` (full replacement)

**Interfaces:**
- Consumes: `lab/src/lib/lab-data.gen.json` (shape from Task 2).
- Produces (used by Tasks 5, 7, 8):
  - `type Hz = 60 | 144 | 240`, `type Platform = "win11-arc" | "windows-latest" | "ubuntu-latest"`
  - `PLATFORM_LABEL: Record<Platform, string>` (chip labels), `PLATFORM_SUBLABEL`, `provenance(platform)`
  - `targetMs(hz: Hz): number`
  - `metrics(hz, platform): Metrics` — `{p99, p99Naive, missed, missedNaive, cpu, cpuNaive}` (display strings)
  - `histogram(hz, platform): HistogramData` — `{bins: Bin[], overflow: {naive: Overflow, tuned: Overflow}}` with `Bin = {ms, naive, tuned}` (0.25 ms display bins, capped at 20 ms) and `Overflow = {count, maxMs}`
  - `RESULT_ROWS: Row[]`, `BASELINE_ROWS: Row[]` — 9 rows each (3 platforms × 3 rates), `Row = {platform, hz, p50, p95, p99, max, missed, cpu}` (strings)
  - `handoffCost(platform)`, `handoffSweep(platform)`, `handoffApp(platform)` — raw arrays from the JSON
  - `HIST_X_MAX_MS = 20`, `DESKTOP: Platform = "win11-arc"`

- [ ] **Step 1: Replace `lab/src/lib/lab-data.ts` with:**

```ts
import gen from "./lab-data.gen.json";

export type Hz = 60 | 144 | 240;
export type Platform = "win11-arc" | "windows-latest" | "ubuntu-latest";

export const DESKTOP: Platform = "win11-arc";
export const HIST_X_MAX_MS = 20;
const DISPLAY_BIN_MS = 0.25; // 5 source bins of 50 µs

interface PacingRow {
  strategy: string; hz: number; n: number;
  p50_ms: number; p95_ms: number; p99_ms: number; max_ms: number;
  stddev_ms: number; missed: number; missed_pct: number; cpu_pct: number;
}
interface HistCell { n: number; bins: number[]; counts: number[] }
export interface HandoffCostRow { backend: string; publish_ns: number; read_ns: number }
export interface HandoffSweepRow {
  backend: string; target_hz: number; achieved_hz: number;
  read_p99_ns: number; retries_per_sec: number; torn: number;
}
export interface HandoffAppRow {
  backend: string; poll_hz: number; lat_p50_ns: number | null;
  lat_p99_ns: number | null; achieved_hz: number; retries_per_sec: number;
}
interface PlatformData {
  label: string; sublabel: string;
  provenance: { run_date: string; commit: string; source: string;
    results_url: string; measurement_class: string };
  pacing: PacingRow[];
  hist: { bin_ms: number; cells: Record<string, HistCell> };
  handoff: { cost: HandoffCostRow[]; sweep: HandoffSweepRow[]; app: HandoffAppRow[] };
}

const DATA = gen.platforms as unknown as Record<Platform, PlatformData>;
export const PLATFORMS = Object.keys(DATA) as Platform[];

export const PLATFORM_LABEL = Object.fromEntries(
  PLATFORMS.map((p) => [p, DATA[p].label]),
) as Record<Platform, string>;
export const PLATFORM_SUBLABEL = Object.fromEntries(
  PLATFORMS.map((p) => [p, DATA[p].sublabel]),
) as Record<Platform, string>;

export function provenance(platform: Platform) {
  return DATA[platform].provenance;
}

export function targetMs(hz: Hz) {
  return 1000 / hz;
}

function pacingRow(platform: Platform, strategy: string, hz: Hz): PacingRow {
  const row = DATA[platform].pacing.find((r) => r.strategy === strategy && r.hz === hz);
  if (!row) throw new Error(`no pacing row ${platform}/${strategy}/${hz}`);
  return row;
}

export interface Metrics {
  p99: string; p99Naive: string;
  missed: string; missedNaive: string;
  cpu: string; cpuNaive: string;
}

export function metrics(hz: Hz, platform: Platform): Metrics {
  const tuned = pacingRow(platform, "timer_spin", hz);
  const naive = pacingRow(platform, "sleep", hz);
  return {
    p99: tuned.p99_ms.toFixed(3),
    p99Naive: naive.p99_ms.toFixed(3),
    missed: tuned.missed_pct.toFixed(2),
    missedNaive: naive.missed_pct.toFixed(1),
    cpu: tuned.cpu_pct.toFixed(1),
    cpuNaive: naive.cpu_pct.toFixed(1),
  };
}

export interface Bin { ms: number; naive: number; tuned: number }
export interface Overflow { count: number; maxMs: number }
export interface HistogramData { bins: Bin[]; overflow: { naive: Overflow; tuned: Overflow } }

/** Aggregate one 50 µs cell into 0.25 ms display bins, tracking >cap overflow. */
function fold(cell: HistCell, into: Bin[], key: "naive" | "tuned"): Overflow {
  const overflow: Overflow = { count: 0, maxMs: 0 };
  cell.bins.forEach((binIndex, i) => {
    const count = cell.counts[i];
    const startMs = binIndex * 0.05;
    if (startMs >= HIST_X_MAX_MS) {
      overflow.count += count;
      overflow.maxMs = Math.max(overflow.maxMs, (binIndex + 1) * 0.05);
      return;
    }
    into[Math.floor(startMs / DISPLAY_BIN_MS)][key] += count;
  });
  return overflow;
}

export function histogram(hz: Hz, platform: Platform): HistogramData {
  const cells = DATA[platform].hist.cells;
  const naiveCell = cells[`sleep-${hz}`];
  const tunedCell = cells[`timer_spin-${hz}`];
  const bins: Bin[] = Array.from(
    { length: HIST_X_MAX_MS / DISPLAY_BIN_MS },
    (_, i) => ({ ms: i * DISPLAY_BIN_MS, naive: 0, tuned: 0 }),
  );
  return {
    bins,
    overflow: {
      naive: fold(naiveCell, bins, "naive"),
      tuned: fold(tunedCell, bins, "tuned"),
    },
  };
}

export interface Row {
  platform: string; hz: string; p50: string; p95: string;
  p99: string; max: string; missed: string; cpu: string;
}

function rows(strategy: string): Row[] {
  const out: Row[] = [];
  for (const p of PLATFORMS) {
    for (const hz of [60, 144, 240] as Hz[]) {
      const r = pacingRow(p, strategy, hz);
      out.push({
        platform: DATA[p].label, hz: String(hz),
        p50: r.p50_ms.toFixed(3), p95: r.p95_ms.toFixed(3),
        p99: r.p99_ms.toFixed(3), max: r.max_ms.toFixed(3),
        missed: r.missed_pct.toFixed(2), cpu: r.cpu_pct.toFixed(1),
      });
    }
  }
  return out;
}

export const RESULT_ROWS: Row[] = rows("timer_spin");
export const BASELINE_ROWS: Row[] = rows("sleep");

export function handoffCost(platform: Platform): HandoffCostRow[] {
  return DATA[platform].handoff.cost;
}
export function handoffSweep(platform: Platform): HandoffSweepRow[] {
  return DATA[platform].handoff.sweep;
}
export function handoffApp(platform: Platform): HandoffAppRow[] {
  return DATA[platform].handoff.app;
}
```

- [ ] **Step 2: Typecheck (build will fail on `index.tsx` — expected)**

```powershell
cd lab; npx tsc --noEmit 2>&1 | Select-Object -First 30
```

Expected: errors ONLY in `src/routes/index.tsx` and `src/components/lab/Histogram.tsx` (they still import removed exports like `BACKSTORY`, old `histogram` return shape). Zero errors inside `lab-data.ts` itself. If `tsc` reports the JSON import, ensure `resolveJsonModule` is on in `tsconfig.json` (add `"resolveJsonModule": true` under `compilerOptions` if missing).

- [ ] **Step 3: Runtime sanity of the accessors**

```powershell
node -e "const gen=require('./src/lib/lab-data.gen.json');const cells=gen.platforms['win11-arc'].hist.cells;const c=cells['timer_spin-144'];const total=c.counts.reduce((a,b)=>a+b,0);console.log('timer_spin-144 n',c.n,'sum',total);if(total!==c.n)process.exit(1);console.log('OK')"
```

Expected: `sum` equals `n` (9499) and `OK`.

- [ ] **Step 4: Commit**

```powershell
cd ..
git add lab/src/lib/lab-data.ts
git commit -m @'
feat: lab-data.ts reads the generated JSON -- real metrics/histogram/table accessors (Phase 9)

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>
'@
```

(Interim state: `index.tsx` doesn't compile until Tasks 4–8 land. Acceptable — each commit is a reviewable unit; the build gate returns in Task 8.)

---

### Task 4: `lab-content.ts` — the real prose

**Files:**
- Create: `lab/src/lib/lab-content.ts`

**Interfaces:**
- Consumes: nothing (pure constants).
- Produces (Task 8 imports these): `BACKSTORY`, `HOW_IT_WORKS`, `DECISIONS: Decision[]`, `LIMITATIONS: string[]`, `HANDOFF`, `BUILD_SNIPPET`, `interface Decision {title, reasoning, changeMind}` — same shapes the old `lab-data.ts` exported, so `index.tsx` changes stay mechanical.

**Content rules:** every sentence below is checked against the README/results docs — do not "improve" claims while transcribing. All numbers appear exactly as written here.

- [ ] **Step 1: Create `lab/src/lib/lab-content.ts` with:**

```ts
export interface Decision {
  title: string;
  reasoning: string;
  changeMind: string;
}

export const BACKSTORY = {
  title: "Backstory",
  paragraphs: [
    `This is the writeup of a C++20 OpenGL renderer whose real subject was never the cube — it was three systems problems: a graphics context that must live on one thread, a lock-free input handoff between two, and hitting a frame deadline on a general-purpose OS. The cube is the demo. The pacer is the point.`,
    `A render loop is simple on paper: do the work, wait for the next deadline, repeat. The "wait" is where the OS gets a say. A request to sleep for a few milliseconds is honored at the scheduler's convenience — on a stock Windows CI runner that means waking on the 15.6 ms timer tick, and even on a desktop with a high-resolution timer request the wake lands 2–3 ms late often enough to eat the frame. The OS is optimized for throughput and battery, not sub-millisecond wake-up precision.`,
    `Everything below is measured, not asserted: 10,000 frames per configuration (first 500 discarded as warmup), per-frame CSV logging with no file IO inside a timed frame, and a synthetic ~100 µs CPU workload per frame so the numbers characterize the pacer rather than the GPU. Three platforms: the desktop run of record (Windows 11, Intel Arc) and two weekly CI runners (windows-latest and ubuntu-latest, both Mesa llvmpipe software GL — honestly a different measurement class, labeled as such).`,
  ],
  context: [
    `Threaded renderer: the main thread owns the window and polls input at ~1 kHz; a render thread owns the GL context and every GL object.`,
    `Four pacing strategies behind one flag (--pace=sleep|timer|timer_spin|spin); timer_spin is the shipping default.`,
    `9,500 measured frames per cell, three refresh targets (60 / 144 / 240 Hz), three platforms.`,
    `The goal is hitting the deadline, not minimizing average frame time — misses are counted and resynced, never chased.`,
  ],
};

export const HOW_IT_WORKS = {
  title: "How it works",
  paragraphs: [
    `The pacer schedules absolute deadlines: next += period, never now + period. When a deadline is missed, the miss is counted and the schedule re-anchored at the current time — the debt is dropped, never repaid with a burst of short frames. (The repo carries a --resched=relative flag purely to measure the classic drift bug: a schedule restarted from "now" leaks every frame's work time into the timeline and cannot observe itself being late.)`,
    `The wait is split in two. First the thread sleeps to the deadline minus a margin, using each platform's sharpest timer — CreateWaitableTimerExW with the high-resolution flag on Windows, clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME) on Linux. Then it spins the remainder on the monotonic clock. The margin is not a constant: it is estimated online from the measured overshoot of every sleep — Welford mean + 3σ, clamped to half the period — because the quantity it must cover is machine- and power-plan-dependent. A constant tuned on one machine is wrong on the next, in either direction.`,
    `Every frame is measured against the same monotonic clock: deadline-to-deadline frame time, requested vs actual sleep, and end-to-end input latency (consume time minus the publish timestamp carried in the input snapshot). The per-run CSV is preallocated and written only at exit — file IO never happens inside a timed frame.`,
  ],
  steps: [
    `Main thread polls keys and publishes an input snapshot at ~1 kHz — it never touches GL.`,
    `Render thread consumes the latest snapshot and submits the frame's GL work.`,
    `Swap buffers (vsync off — glfwSwapInterval(0); the pacer owns the frame clock).`,
    `Sleep to the deadline minus the Welford-estimated margin, then spin the last stretch.`,
    `On a miss: count it, re-anchor the schedule, keep the frame period honest.`,
  ],
  uniqueFeatures: {
    intro:
      "Most OpenGL renderers stop at vsync: they hand the frame cadence to the driver and live with whatever comes back. This project treats frame delivery as a measured systems problem.",
    items: [
      {
        title: "Explicit frame pacer instead of swap interval",
        body: "The render loop owns its own deadline and measures its own interval. VSync is off at context creation; the pacer decides when the next frame starts, and reports every miss instead of hiding it in the driver.",
      },
      {
        title: "An adaptive spin margin, measured per machine",
        body: "The sleep-short margin is a Welford mean + 3σ estimate of this machine's actual wake overshoot, updated every frame. The tables below show what it buys: the bare high-res timer hands its wake jitter straight to frame starts; timer + spin absorbs it for a measured CPU cost.",
      },
      {
        title: "Input decoupled from the frame rate",
        body: "Input is published at ~1 kHz from the main thread through a swappable handoff (mutex, atomic bitmask, or seqlock) and consumed by the render thread — so a frame cap no longer sets input latency. Measured end-to-end: p50 ≈ 0.64 ms against a 6.9 ms frame period.",
      },
      {
        title: "Misses are counted, never chased",
        body: "Absolute deadlines with re-anchoring on a miss. No catch-up bursts, no silent drift — the missed-deadline column in every table is the pacer grading itself.",
      },
      {
        title: "Instrumentation with no observer cost",
        body: "Per-frame records go into a preallocated buffer, flushed only at exit. Render-thread CPU time comes from cycle-accurate counters (QueryThreadCycleTime calibrated against the pacer clock; CLOCK_THREAD_CPUTIME_ID on POSIX), not tick-sampled APIs.",
      },
    ],
  },
};

export const DECISIONS: Decision[] = [
  {
    title: "Render on a worker thread, input on main",
    reasoning:
      "The main thread owns the window and event queue only — it polls keys and publishes a snapshot at ~1 kHz and never touches GL; a dedicated render thread makes the context current and owns every GL object. The decoupled rates are the payoff: ~970 Hz achieved input publishing against a 144 Hz frame consumer, with measured end-to-end input latency of p50 ≈ 0.64 ms against a 6.9 ms frame period. The price was paid once, in shutdown ordering: signal stop → render thread deletes its GL objects and detaches → join → destroy the window on main.",
    changeMind:
      "would change my mind: a windowing layer without the main-thread event constraint, or a target where second-thread contexts don't exist — the browser build already collapses to single-threaded for exactly that reason.",
  },
  {
    title: "Absolute deadlines, never relative",
    reasoning:
      "The pacer schedules next += period, never now + period. Two otherwise-identical 60-second runs differ only in this rule: absolute ends 0.345 ms from ideal; relative leaks every frame's work time and wake overshoot into the schedule permanently — +0.664 ms per frame, 5.7 s behind after a minute — while reporting zero missed deadlines the whole way, because a schedule restarted from now cannot observe itself being late.",
    changeMind:
      "would change my mind: a workload whose frames routinely exceed the period — but an absolute schedule fails that loudly (counted misses), and the fix is lowering the target rate, not a policy that hides the same failure as silent drift.",
  },
  {
    title: "An adaptive spin margin, not a constant",
    reasoning:
      "The margin the sleep must undershoot by is machine- and power-plan-dependent: this desktop's naive sleep wakes 2–3 ms late, while a stock Windows CI runner sleeps in full 15.6 ms scheduler ticks — a constant tuned on either machine is wrong on the other, in either direction. So the pacer estimates it online from every sleep's measured overshoot: Welford mean + 3σ, clamped to half the period, with a 1.5 ms bootstrap until 16 samples.",
    changeMind:
      "would change my mind: an overshoot distribution heavy-tailed enough that mean + 3σ under-covers — it would show up as rising missed-deadline counts — would argue for a quantile tracker; a hard-real-time platform with bounded overshoot would argue for a small constant.",
  },
  {
    title: "Mutex by default, lock-free behind a flag",
    reasoning:
      "A std::mutex around a small POD is the shipping input handoff; the lock-free backends exist, are tested, and are not the default. At this app's real rates — 1,000 publishes/s against 144 reads/s — the expected number of reads that ever meet a held lock is ≈0.002 per second, and the measured zeros agree: five of six app cells logged exactly zero reader retries. The contended sweep puts the crossover where the seqlock first measurably wins at ≈100,000 publishes/s — two orders of magnitude above this app. Below that, choosing the seqlock is a design statement, not a performance win.",
    changeMind:
      "would change my mind: publish rates approaching the measured crossover, a writer that must never block (an audio callback), or multiple readers — none of which this app has.",
  },
  {
    title: "Hand-rolled mat4, no glm",
    reasoning:
      "The project's subject is threads, handoff, and pacing; the matrix code exists to put a cube on screen and to be read. One small column-major header with exactly the four operations the demo needs, a dependency-free test suite including a 16-element perspective reference check, and its two sharp edges documented where they cut (transpose=GL_FALSE, and zNear/zFar because Windows headers #define near and far).",
    changeMind:
      "would change my mind: the first feature needing quaternions, SIMD, or more than a handful of ops — the moment the matrix code stops being trivially reviewable, glm goes in.",
  },
];

export const LIMITATIONS = [
  `The seqlock's payload read is formally a data race — undefined behavior under the C++ memory model. The retry loop discards every torn copy on real hardware (torn=0 in all 15 contended-sweep rows) and the construction is the standard practical one, but it is still bending a rule of the abstract machine — naming that is the point.`,
  `macOS timer precision is untuned: the mach_wait_until path compiles but real precision there wants THREAD_TIME_CONSTRAINT_POLICY, which is left unset. Numbers here are Windows and Linux only; the macOS path is compiled, not measured.`,
  `No GPU-side timing. Every number is CPU-side by design (the ~100 µs synthetic workload characterizes the pacer, not the GPU), so driver behavior is visible only by its side effects — on battery this machine's Arc driver frame-limits GL inside SwapBuffers, which is why every run of record demands AC power.`,
  `The CI platforms run Mesa llvmpipe software rasterization on shared virtualized runners with no AC/idle control — a different measurement class. Compare CI platforms to each other and across weeks, not to the desktop tables.`,
  `The browser build on this page is not the pacer: it is single-threaded and paced by requestAnimationFrame, because neither high-resolution sleep nor a second GL thread exists on a browser main thread.`,
];

export const HANDOFF = {
  title: "Input handoff: mutex vs lock-free",
  intro: [
    `The second system: input crosses from the main thread (publishing at ~1 kHz) to the render thread through one of three interchangeable backends — a std::mutex around a small POD (the default), an atomic key bitmask, and a seqlock carrying the full payload with its publish timestamp. Selected at startup with --input=mutex|bitmask|seqlock.`,
    `This section is rate-independent — its x-axis IS the publish rate, so the Hz filter above doesn't apply here.`,
  ],
  costNote: `Amortized throughput from 1 M-iteration batches — comparable across backends, not "what one isolated call costs."`,
  sweepNote: `Reader p99 vs achieved publish rate. Points sit at each run's achieved rate; the unthrottled cells land where each backend actually reached. Below the crossover the mutex's read tail sits at the 100 ns measurement floor.`,
  sweepCrossover: `≈100 k/s — the crossover`,
  appNote: `In-app end-to-end latency (consume time − publish timestamp), desktop run of record. The bitmask cannot carry the timestamp — 32 bits of keys is all it holds — so its latency is unmeasurable by construction: the Phase-4 asymmetry made visible. App cells are a desktop protocol; CI runs measure only the micro-benchmark.`,
};

export const BUILD_SNIPPET = `git clone https://github.com/tiffany-mares/OpenGL-Renderer
cd OpenGL-Renderer
cmake -B build            # Linux: add -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure

# run it (Windows: build\\Release\\cube.exe, Linux: build/cube)
cube --fps 144                          # paced, timer_spin default
cube --fps 144 --pace=sleep             # feel the naive baseline
cube --fps 144 --bench-frames 10000 --log out.csv
python bench/run_matrix.py              # the full 13-cell matrix`;
```

- [ ] **Step 2: Typecheck the new module in isolation**

```powershell
cd lab; npx tsc --noEmit src/lib/lab-content.ts 2>&1 | Select-Object -First 10; cd ..
```

Expected: no errors from `lab-content.ts` (running tsc on one file may surface unrelated config noise — what matters is zero errors pointing at this file).

- [ ] **Step 3: Commit**

```powershell
git add lab/src/lib/lab-content.ts
git commit -m @'
feat: lab-content.ts -- real backstory, decision log, limitations, handoff prose, build snippet (Phase 9)

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>
'@
```

---

### Task 5: `Histogram.tsx` — real bins, 20 ms cap, overflow note

**Files:**
- Rewrite: `lab/src/components/lab/Histogram.tsx` (full replacement)

**Interfaces:**
- Consumes: `histogram(hz, platform): HistogramData`, `targetMs`, `HIST_X_MAX_MS`, types `Hz`/`Platform` from Task 3.
- Produces: `<Histogram hz={hz} platform={platform} />` — same props as before; Task 8 keeps using it unchanged.

- [ ] **Step 1: Replace `lab/src/components/lab/Histogram.tsx` with:**

```tsx
import { histogram, targetMs, HIST_X_MAX_MS, type Hz, type Platform } from "@/lib/lab-data";

const W = 1000;
const H = 340;
const PAD = { top: 16, right: 16, bottom: 40, left: 52 };

export function Histogram({ hz, platform }: { hz: Hz; platform: Platform }) {
  const { bins, overflow } = histogram(hz, platform);
  const target = targetMs(hz);
  const maxMs = HIST_X_MAX_MS;
  const maxCount = Math.max(1, ...bins.map((b) => Math.max(b.naive, b.tuned)));

  const x = (ms: number) => PAD.left + (ms / maxMs) * (W - PAD.left - PAD.right);
  const y = (c: number) => {
    const v = Math.max(c, 1);
    const logMax = Math.log10(maxCount * 1.2);
    return H - PAD.bottom - (Math.log10(v) / logMax) * (H - PAD.top - PAD.bottom);
  };

  const barW = Math.max(1.5, x(0.25) - x(0));
  const yTicks = [1, 10, 100, 1000, 10000].filter((t) => t <= maxCount * 1.2);
  const xTicks = [0, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20];

  const overflowParts: string[] = [];
  if (overflow.naive.count > 0)
    overflowParts.push(
      `naive sleep: ${overflow.naive.count.toLocaleString()} intervals beyond ${maxMs} ms (max ${overflow.naive.maxMs.toFixed(1)} ms)`,
    );
  if (overflow.tuned.count > 0)
    overflowParts.push(
      `tuned pacer: ${overflow.tuned.count.toLocaleString()} beyond ${maxMs} ms (max ${overflow.tuned.maxMs.toFixed(1)} ms)`,
    );

  return (
    <div>
      <svg viewBox={`0 0 ${W} ${H}`} className="w-full" role="img" aria-label="Frame interval histogram, log scale">
        {yTicks.map((t) => (
          <g key={t}>
            <line
              x1={PAD.left}
              x2={W - PAD.right}
              y1={y(t)}
              y2={y(t)}
              stroke="var(--hairline)"
              strokeWidth={1}
              opacity={0.5}
            />
            <text
              x={PAD.left - 8}
              y={y(t) + 4}
              textAnchor="end"
              fill="var(--muted-foreground)"
              fontSize={11}
              fontFamily="var(--font-mono)"
            >
              {t >= 1000 ? `${t / 1000}k` : t}
            </text>
          </g>
        ))}

        {bins.map((b) => (
          <rect
            key={`n${b.ms}`}
            x={x(b.ms)}
            y={y(b.naive)}
            width={barW}
            height={b.naive > 0 ? Math.max(0, H - PAD.bottom - y(b.naive)) : 0}
            fill="var(--series-naive)"
            opacity={0.75}
          />
        ))}
        {bins.map((b) => (
          <rect
            key={`t${b.ms}`}
            x={x(b.ms)}
            y={y(b.tuned)}
            width={barW}
            height={b.tuned > 0 ? Math.max(0, H - PAD.bottom - y(b.tuned)) : 0}
            fill="var(--series-tuned)"
          />
        ))}

        <line
          x1={x(target)}
          x2={x(target)}
          y1={PAD.top}
          y2={H - PAD.bottom}
          stroke="var(--series-target)"
          strokeWidth={1}
          strokeDasharray="4 4"
        />
        <text
          x={x(target) + 6}
          y={PAD.top + 12}
          fill="var(--muted-foreground)"
          fontSize={11}
          fontFamily="var(--font-mono)"
        >
          target {target.toFixed(2)} ms
        </text>

        <line
          x1={PAD.left}
          x2={W - PAD.right}
          y1={H - PAD.bottom}
          y2={H - PAD.bottom}
          stroke="var(--hairline)"
        />
        {xTicks.map((t) => (
          <text
            key={t}
            x={x(t)}
            y={H - PAD.bottom + 18}
            textAnchor="middle"
            fill="var(--muted-foreground)"
            fontSize={11}
            fontFamily="var(--font-mono)"
          >
            {t}
          </text>
        ))}
        <text
          x={(W + PAD.left) / 2}
          y={H - 6}
          textAnchor="middle"
          fill="var(--muted-foreground)"
          fontSize={11}
        >
          frame interval, start-to-start (milliseconds)
        </text>
      </svg>

      <div className="mt-3 flex flex-wrap gap-x-6 gap-y-2 font-mono text-xs text-muted-foreground">
        <Legend color="var(--series-naive)" label="naive sleep (--pace=sleep)" />
        <Legend color="var(--series-tuned)" label="tuned pacer (--pace=timer_spin)" />
        <Legend color="var(--series-target)" label={`target interval (${target.toFixed(2)} ms)`} dashed />
      </div>
      {overflowParts.length > 0 && (
        <p className="mt-2 font-mono text-xs text-muted-foreground">
          off-scale: {overflowParts.join(" · ")}
        </p>
      )}
    </div>
  );
}

function Legend({ color, label, dashed }: { color: string; label: string; dashed?: boolean }) {
  return (
    <span className="flex items-center gap-2">
      <span
        className="inline-block h-[3px] w-6"
        style={dashed ? { backgroundImage: `repeating-linear-gradient(90deg, ${color} 0 4px, transparent 4px 8px)` } : { backgroundColor: color }}
      />
      {label}
    </span>
  );
}
```

Notes for the implementer: zero-count bins must render nothing (`height 0`), otherwise the log floor paints a bar in every empty bin. Bars are drawn from the bin's left edge (`x(b.ms)`), not centered — display bins are contiguous ranges.

- [ ] **Step 2: Typecheck**

```powershell
cd lab; npx tsc --noEmit 2>&1 | Select-String "Histogram" | Select-Object -First 5; cd ..
```

Expected: no `Histogram.tsx` errors (remaining `index.tsx` errors are expected until Task 8).

- [ ] **Step 3: Commit**

```powershell
git add lab/src/components/lab/Histogram.tsx
git commit -m @'
feat: Histogram renders the real 50us bins -- 0.25ms display bins, 20ms cap, off-scale note (Phase 9)

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>
'@
```

---

### Task 6: Real wasm cube — stage script + `CubeWasm.tsx`

**Files:**
- Create: `lab/scripts/stage-cube.mjs`
- Create: `lab/src/components/lab/CubeWasm.tsx`
- Delete: `lab/src/components/lab/Cube.tsx`
- Modify: `lab/package.json` (predev/prebuild)

**Interfaces:**
- Consumes: `dist/cube.js` + `dist/cube.wasm` at the repo root (built by `python web/build.py --out dist`).
- Produces: `<CubeWasm />` (no props) — Task 8 renders it in the `#demo` section. Emscripten contract (proven by `web/index.html`): a `<canvas id="canvas">` plus `window.Module = { canvas }` set *before* `/cube.js` loads; the module sizes the canvas backing store to 960×540 itself.

- [ ] **Step 1: Create `lab/scripts/stage-cube.mjs`**

```js
// Copies the Emscripten cube artifacts from the repo's dist/ into public/.
// dist/ is built by `python web/build.py --out dist` at the repo root (needs an
// activated emsdk). FATAL when absent: a lab build without the cube is incomplete.
import { copyFileSync, existsSync, mkdirSync } from "node:fs";
import { join, dirname } from "node:path";
import { fileURLToPath } from "node:url";

const HERE = dirname(fileURLToPath(import.meta.url));
const DIST = join(HERE, "..", "..", "dist");
const PUBLIC = join(HERE, "..", "public");

for (const name of ["cube.js", "cube.wasm"]) {
  const src = join(DIST, name);
  if (!existsSync(src)) {
    console.error(
      `FATAL: ${src} is missing -- run "python web/build.py --out dist" at the repo root first (requires an activated emsdk; local install at C:\\Users\\tiffm\\emsdk).`,
    );
    process.exit(1);
  }
  mkdirSync(PUBLIC, { recursive: true });
  copyFileSync(src, join(PUBLIC, name));
}
console.log("staged cube.js + cube.wasm into public/");
```

- [ ] **Step 2: Wire it into `lab/package.json` scripts**

```json
"predev": "node scripts/stage-cube.mjs",
"prebuild": "node scripts/stage-cube.mjs",
```

(`build:dev` intentionally not wired — Lovable-only path.)

- [ ] **Step 3: Build the artifacts once so dev/build work**

```powershell
& "C:\Users\tiffm\emsdk\emsdk_env.ps1"; python web/build.py --out dist
```

Expected: build.py stages `dist/` and prints its staged-file count. Then:

```powershell
cd lab; node scripts/stage-cube.mjs; cd ..
```

Expected: `staged cube.js + cube.wasm into public/`. Verify the FATAL path too: temporarily `Rename-Item dist dist_x`, run the script (expect `FATAL: ... web/build.py ...`, exit 1), rename back.

- [ ] **Step 4: Create `lab/src/components/lab/CubeWasm.tsx`**

```tsx
import { useEffect, useRef, useState } from "react";

declare global {
  interface Window {
    Module?: { canvas: HTMLCanvasElement | null; onAbort?: (what: unknown) => void };
  }
}

// Emscripten modules cannot be torn down and re-instantiated; boot exactly once
// per document, even across React strict-mode remounts and dev hot reloads.
let booted = false;

export function CubeWasm() {
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const [failed, setFailed] = useState(false);
  const [fps, setFps] = useState(0);
  const [frameMs, setFrameMs] = useState(0);

  // fps/ms overlay: measures this page's rAF cadence -- honest for a
  // browser build that is itself paced by requestAnimationFrame.
  useEffect(() => {
    let raf = 0;
    let last = performance.now();
    let acc = 0;
    let frames = 0;
    const tick = (now: number) => {
      const dt = now - last;
      last = now;
      frames += 1;
      acc += dt;
      if (acc >= 400) {
        setFps(Math.round((frames * 1000) / acc));
        setFrameMs(acc / frames);
        frames = 0;
        acc = 0;
      }
      raf = requestAnimationFrame(tick);
    };
    raf = requestAnimationFrame(tick);
    return () => cancelAnimationFrame(raf);
  }, []);

  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas || booted) return;
    booted = true;

    window.Module = { canvas, onAbort: () => setFailed(true) };
    const script = document.createElement("script");
    script.src = "/cube.js";
    script.onerror = () => setFailed(true);
    document.body.appendChild(script);

    const onKeyDown = (e: KeyboardEvent) => {
      if (
        document.activeElement === canvas &&
        ["ArrowLeft", "ArrowRight", "ArrowUp", "ArrowDown", " "].includes(e.key)
      ) {
        e.preventDefault();
      }
    };
    window.addEventListener("keydown", onKeyDown);
    return () => window.removeEventListener("keydown", onKeyDown);
  }, []);

  return (
    <div className="relative rounded-lg border border-border bg-panel">
      <canvas
        ref={canvasRef}
        id="canvas"
        tabIndex={0}
        onClick={() => canvasRef.current?.focus()}
        className="block aspect-[16/9] w-full rounded-lg bg-black outline-none focus:ring-1 focus:ring-metric-1"
      />
      {failed ? (
        <p className="absolute inset-x-3 top-3 font-mono text-xs text-series-naive">
          demo failed to load — this build needs WebGL2. The numbers below are unaffected.
        </p>
      ) : (
        <div className="pointer-events-none absolute right-3 top-3 text-right font-mono text-xs text-muted-foreground">
          <div className="text-foreground">{fps.toString().padStart(3, " ")} fps</div>
          <div>{frameMs.toFixed(2)} ms/frame</div>
        </div>
      )}
    </div>
  );
}
```

- [ ] **Step 5: Delete the decorative cube**

```powershell
Remove-Item lab/src/components/lab/Cube.tsx
```

(`index.tsx` still imports `Cube` — it breaks compile until Task 8, same interim state as Task 3. If you want a green check now, temporarily change the `index.tsx` import/usage from `Cube` to `CubeWasm`; Task 8 replaces the file wholesale anyway.)

- [ ] **Step 6: Commit**

```powershell
git add lab/scripts/stage-cube.mjs lab/src/components/lab/CubeWasm.tsx lab/package.json
git add -u lab/src/components/lab   # records the Cube.tsx deletion
git commit -m @'
feat: real Emscripten cube in the lab page -- stage script + boot-once CubeWasm, decorative wireframe retired (Phase 9)

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>
'@
```

---

### Task 7: Handoff chart components (SVG, no chart library)

**Files:**
- Create: `lab/src/components/lab/HandoffCost.tsx`
- Create: `lab/src/components/lab/HandoffSweep.tsx`

**Interfaces:**
- Consumes: `HandoffCostRow`, `HandoffSweepRow` types from Task 3.
- Produces (Task 8 renders): `<HandoffCost rows={handoffCost(platform)} />` and `<HandoffSweep rows={handoffSweep(platform)} showCrossover={platform === DESKTOP} crossoverLabel={HANDOFF.sweepCrossover} />`.

Backend colors, fixed across both charts: mutex `var(--metric-1)`, bitmask `var(--metric-2)`, seqlock `var(--metric-3)` (raw CSS vars — they exist on `:root` in `styles.css`).

- [ ] **Step 1: Create `lab/src/components/lab/HandoffCost.tsx`**

```tsx
import type { HandoffCostRow } from "@/lib/lab-data";

const W = 1000;
const H = 260;
const PAD = { top: 24, right: 16, bottom: 34, left: 52 };

export const BACKEND_COLOR: Record<string, string> = {
  mutex: "var(--metric-1)",
  bitmask: "var(--metric-2)",
  seqlock: "var(--metric-3)",
};

export function HandoffCost({ rows }: { rows: HandoffCostRow[] }) {
  const maxNs = Math.max(...rows.flatMap((r) => [r.publish_ns, r.read_ns])) * 1.2;
  const y = (ns: number) => H - PAD.bottom - (ns / maxNs) * (H - PAD.top - PAD.bottom);

  const groupW = (W - PAD.left - PAD.right) / rows.length;
  const barW = groupW * 0.22;

  const yTicks = [0, 5, 10, 15].filter((t) => t <= maxNs);

  return (
    <div>
      <svg viewBox={`0 0 ${W} ${H}`} className="w-full" role="img" aria-label="Uncontended per-op handoff cost in nanoseconds">
        {yTicks.map((t) => (
          <g key={t}>
            <line x1={PAD.left} x2={W - PAD.right} y1={y(t)} y2={y(t)} stroke="var(--hairline)" strokeWidth={1} opacity={0.5} />
            <text x={PAD.left - 8} y={y(t) + 4} textAnchor="end" fill="var(--muted-foreground)" fontSize={11} fontFamily="var(--font-mono)">
              {t}
            </text>
          </g>
        ))}
        {rows.map((r, i) => {
          const cx = PAD.left + groupW * (i + 0.5);
          const color = BACKEND_COLOR[r.backend] ?? "var(--metric-1)";
          return (
            <g key={r.backend}>
              <rect x={cx - barW - 3} y={y(r.publish_ns)} width={barW} height={H - PAD.bottom - y(r.publish_ns)} fill={color} />
              <rect x={cx + 3} y={y(r.read_ns)} width={barW} height={H - PAD.bottom - y(r.read_ns)} fill={color} opacity={0.45} />
              <text x={cx - barW / 2 - 3} y={y(r.publish_ns) - 5} textAnchor="middle" fill="var(--muted-foreground)" fontSize={10} fontFamily="var(--font-mono)">
                {r.publish_ns}
              </text>
              <text x={cx + barW / 2 + 3} y={y(r.read_ns) - 5} textAnchor="middle" fill="var(--muted-foreground)" fontSize={10} fontFamily="var(--font-mono)">
                {r.read_ns}
              </text>
              <text x={cx} y={H - PAD.bottom + 18} textAnchor="middle" fill="var(--foreground)" fontSize={12} fontFamily="var(--font-mono)">
                {r.backend}
              </text>
            </g>
          );
        })}
        <line x1={PAD.left} x2={W - PAD.right} y1={H - PAD.bottom} y2={H - PAD.bottom} stroke="var(--hairline)" />
        <text x={16} y={PAD.top - 8} fill="var(--muted-foreground)" fontSize={11}>
          ns/op
        </text>
      </svg>
      <div className="mt-2 flex gap-x-6 font-mono text-xs text-muted-foreground">
        <span className="flex items-center gap-2">
          <span className="inline-block h-3 w-3 rounded-sm bg-foreground/80" /> publish
        </span>
        <span className="flex items-center gap-2">
          <span className="inline-block h-3 w-3 rounded-sm bg-foreground/30" /> read
        </span>
      </div>
    </div>
  );
}
```

- [ ] **Step 2: Create `lab/src/components/lab/HandoffSweep.tsx`**

```tsx
import type { HandoffSweepRow } from "@/lib/lab-data";
import { BACKEND_COLOR } from "./HandoffCost";

const W = 1000;
const H = 340;
const PAD = { top: 20, right: 24, bottom: 44, left: 60 };

const X_MIN = 1e3;
const X_MAX = 2e7;
const X_TICKS = [1e3, 1e4, 1e5, 1e6, 1e7];
const fmtHz = (v: number) => (v >= 1e6 ? `${v / 1e6}M` : `${v / 1e3}k`);

export function HandoffSweep({
  rows,
  showCrossover,
  crossoverLabel,
}: {
  rows: HandoffSweepRow[];
  showCrossover: boolean;
  crossoverLabel: string;
}) {
  const maxNs = Math.max(...rows.map((r) => r.read_p99_ns)) * 1.15;
  const x = (hz: number) =>
    PAD.left +
    ((Math.log10(hz) - Math.log10(X_MIN)) / (Math.log10(X_MAX) - Math.log10(X_MIN))) *
      (W - PAD.left - PAD.right);
  const y = (ns: number) => H - PAD.bottom - (ns / maxNs) * (H - PAD.top - PAD.bottom);

  const backends = [...new Set(rows.map((r) => r.backend))];
  const yStep = maxNs > 1500 ? 500 : 100;
  const yTicks = Array.from({ length: Math.floor(maxNs / yStep) + 1 }, (_, i) => i * yStep);

  return (
    <div>
      <svg viewBox={`0 0 ${W} ${H}`} className="w-full" role="img" aria-label="Contended sweep: reader p99 vs achieved publish rate, log x">
        {yTicks.map((t) => (
          <g key={t}>
            <line x1={PAD.left} x2={W - PAD.right} y1={y(t)} y2={y(t)} stroke="var(--hairline)" strokeWidth={1} opacity={0.5} />
            <text x={PAD.left - 8} y={y(t) + 4} textAnchor="end" fill="var(--muted-foreground)" fontSize={11} fontFamily="var(--font-mono)">
              {t}
            </text>
          </g>
        ))}
        {X_TICKS.map((t) => (
          <g key={t}>
            <line x1={x(t)} x2={x(t)} y1={PAD.top} y2={H - PAD.bottom} stroke="var(--hairline)" strokeWidth={1} opacity={0.3} />
            <text x={x(t)} y={H - PAD.bottom + 18} textAnchor="middle" fill="var(--muted-foreground)" fontSize={11} fontFamily="var(--font-mono)">
              {fmtHz(t)}
            </text>
          </g>
        ))}

        {showCrossover && (
          <g>
            <line x1={x(1e5)} x2={x(1e5)} y1={PAD.top} y2={H - PAD.bottom} stroke="var(--series-target)" strokeWidth={1} strokeDasharray="4 4" />
            <text x={x(1e5) + 6} y={PAD.top + 12} fill="var(--muted-foreground)" fontSize={11} fontFamily="var(--font-mono)">
              {crossoverLabel}
            </text>
          </g>
        )}

        {backends.map((backend) => {
          const pts = rows
            .filter((r) => r.backend === backend)
            .sort((a, b) => a.achieved_hz - b.achieved_hz);
          const color = BACKEND_COLOR[backend] ?? "var(--metric-1)";
          const path = pts.map((p, i) => `${i === 0 ? "M" : "L"}${x(p.achieved_hz)},${y(p.read_p99_ns)}`).join(" ");
          return (
            <g key={backend}>
              <path d={path} fill="none" stroke={color} strokeWidth={1.6} />
              {pts.map((p) => (
                <circle key={p.target_hz} cx={x(p.achieved_hz)} cy={y(p.read_p99_ns)} r={3.5} fill={color} />
              ))}
            </g>
          );
        })}

        <line x1={PAD.left} x2={W - PAD.right} y1={H - PAD.bottom} y2={H - PAD.bottom} stroke="var(--hairline)" />
        <text x={(W + PAD.left) / 2} y={H - 6} textAnchor="middle" fill="var(--muted-foreground)" fontSize={11}>
          achieved publishes per second (log)
        </text>
        <text x={16} y={PAD.top - 6} fill="var(--muted-foreground)" fontSize={11}>
          reader p99 (ns)
        </text>
      </svg>
      <div className="mt-2 flex flex-wrap gap-x-6 gap-y-2 font-mono text-xs text-muted-foreground">
        {backends.map((b) => (
          <span key={b} className="flex items-center gap-2">
            <span className="inline-block h-[3px] w-6" style={{ backgroundColor: BACKEND_COLOR[b] }} />
            {b}
          </span>
        ))}
      </div>
    </div>
  );
}
```

- [ ] **Step 3: Typecheck**

```powershell
cd lab; npx tsc --noEmit 2>&1 | Select-String "Handoff" | Select-Object -First 5; cd ..
```

Expected: no `HandoffCost.tsx`/`HandoffSweep.tsx` errors.

- [ ] **Step 4: Commit**

```powershell
git add lab/src/components/lab/HandoffCost.tsx lab/src/components/lab/HandoffSweep.tsx
git commit -m @'
feat: handoff SVG charts -- per-op cost bars + log-x contended sweep with desktop-only crossover (Phase 9)

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>
'@
```

---

### Task 8: Page rewrite — `index.tsx`, `SectionNav.tsx`, `__root.tsx`

**Files:**
- Rewrite: `lab/src/routes/index.tsx` (full replacement below)
- Modify: `lab/src/components/lab/SectionNav.tsx` (one array entry)
- Modify: `lab/src/routes/__root.tsx` (meta block only)

**Interfaces:**
- Consumes: everything produced by Tasks 3–7 (exact names as declared there).
- Produces: the finished page; `npm run build` goes green again here.

- [ ] **Step 1: In `SectionNav.tsx`, add the handoff item**

In the `ITEMS` array, insert between `results` and `decisions`:

```ts
  { id: "handoff", label: "input handoff" },
```

- [ ] **Step 2: In `__root.tsx`, replace the Lovable meta entries**

Replace the five Lovable-branded entries inside `head.meta` (keep charSet/viewport/og:type/twitter:card):

```ts
      { title: "tiffany-mares / opengl-renderer — hitting a frame deadline on an unwilling OS" },
      { name: "description", content: "A threaded OpenGL renderer's frame pacer, measured: zero missed deadlines out of 9,500 at 144 Hz where naive sleep misses half, across a desktop run of record and weekly CI runners." },
      { name: "author", content: "Tiffany Mares" },
      { property: "og:title", content: "tiffany-mares / opengl-renderer" },
      { property: "og:description", content: "Hitting a frame deadline on an OS that doesn't want you to — measurements, decisions, and costs." },
```

Also delete the `{ name: "twitter:site", content: "@Lovable" }` entry.

- [ ] **Step 3: Replace `lab/src/routes/index.tsx` wholesale with:**

```tsx
import { createFileRoute } from "@tanstack/react-router";
import { useState } from "react";
import { Github, Linkedin, Globe } from "lucide-react";
import { CubeWasm } from "@/components/lab/CubeWasm";
import { Histogram } from "@/components/lab/Histogram";
import { SectionNav } from "@/components/lab/SectionNav";
import { HandoffCost } from "@/components/lab/HandoffCost";
import { HandoffSweep } from "@/components/lab/HandoffSweep";
import {
  BASELINE_ROWS,
  DESKTOP,
  PLATFORMS,
  PLATFORM_LABEL,
  PLATFORM_SUBLABEL,
  RESULT_ROWS,
  handoffApp,
  handoffCost,
  handoffSweep,
  metrics,
  provenance,
  type Hz,
  type Platform,
  type Row,
} from "@/lib/lab-data";
import {
  BACKSTORY,
  BUILD_SNIPPET,
  DECISIONS,
  HANDOFF,
  HOW_IT_WORKS,
  LIMITATIONS,
} from "@/lib/lab-content";

export const Route = createFileRoute("/")({
  head: () => ({
    meta: [
      { title: "tiffany-mares / opengl-renderer — hitting a frame deadline on an unwilling OS" },
      {
        name: "description",
        content:
          "A threaded OpenGL renderer's frame pacer, measured: zero missed deadlines out of 9,500 at 144 Hz where naive sleep misses half — plus the input-handoff numbers that justify a mutex.",
      },
      { property: "og:title", content: "tiffany-mares / opengl-renderer — hitting a frame deadline on an unwilling OS" },
      {
        property: "og:description",
        content:
          "A lab writeup: real pacing and input-handoff measurements from a threaded OpenGL renderer, with the CPU cost stated plainly.",
      },
      { property: "og:type", content: "article" },
      { name: "twitter:card", content: "summary_large_image" },
    ],
  }),
  component: Index,
});

function Index() {
  const [hz, setHz] = useState<Hz>(144);
  const [platform, setPlatform] = useState<Platform>(DESKTOP);
  const m = metrics(hz, platform);
  const prov = provenance(platform);
  const appRows = handoffApp(platform);

  return (
    <main className="mx-auto max-w-5xl px-6 pb-24 pt-10 sm:px-8">
      {/* Top strip */}
      <header className="flex flex-wrap items-start justify-between gap-4 border-b border-hairline pb-8">
        <div className="max-w-2xl">
          <p className="font-mono text-xs">
            <span className="text-metric-1">tiffany-mares</span>
            <span className="text-muted-foreground">/</span>
            <span className="text-metric-3">opengl-renderer</span>
          </p>
          <h1 className="mt-2 text-balance text-2xl font-medium leading-snug sm:text-3xl">
            Hitting a <Key accent="metric-3">frame deadline</Key> on an <Key accent="series-naive">OS</Key> that doesn&apos;t want you to.
          </h1>
          <p className="mt-3 text-sm leading-relaxed text-muted-foreground">
            A threaded OpenGL renderer&apos;s <Key>frame pacer</Key>, measured: <Key accent="metric-1">zero missed deadlines</Key> out
            of 9,500 at <Key accent="metric-1">144 Hz</Key> where a <Key accent="series-naive">naive sleep loop</Key> misses half —
            and what that costs.
          </p>
        </div>
        <div className="flex shrink-0 flex-col items-end gap-1 font-mono text-xs text-primary">
          <a href="https://github.com/tiffany-mares/OpenGL-Renderer" className="underline underline-offset-4 hover:no-underline">
            github ↗
          </a>
          <a href="https://opengl-renderer.pages.dev/" className="underline underline-offset-4 hover:no-underline">
            live demo ↗
          </a>
          <a href="https://www.linkedin.com/in/tiffany-mares" className="underline underline-offset-4 hover:no-underline">
            linkedin ↗
          </a>
          <a href="https://tiffanymares.com/" className="underline underline-offset-4 hover:no-underline">
            contact ↗
          </a>
        </div>
      </header>

      <SectionNav />

      {/* Cube */}
      <section id="demo" className="scroll-mt-20 pt-10">
        <CubeWasm />
        <p className="mt-3 text-xs leading-relaxed text-muted-foreground">
          The real renderer, compiled to WebAssembly (<Key>WebGL2</Key>). Click the cube for keyboard focus:
          arrows yaw/pitch, SPACE pauses. This build is <Key>single-threaded</Key> and paced by{" "}
          <Key accent="metric-3">requestAnimationFrame</Key> on purpose — neither high-resolution sleep nor a
          second GL thread exists on a browser main thread, so the pacer and the threaded render deliberately
          do not port. Every number below comes from the <Key accent="metric-2">native build</Key>.
        </p>
      </section>

      {/* Backstory */}
      <section id="backstory" className="mt-14 scroll-mt-20">
        <h2 className="text-sm font-medium">{BACKSTORY.title}</h2>
        <div className="mt-4 space-y-4 text-sm leading-relaxed text-foreground/80">
          {BACKSTORY.paragraphs.map((p, i) => (
            <p key={i}>{highlight(p)}</p>
          ))}
        </div>
        <ul className="mt-5 space-y-2 text-sm leading-relaxed text-muted-foreground">
          {BACKSTORY.context.map((item) => (
            <li key={item} className="flex gap-2">
              <span aria-hidden className="text-metric-2">—</span>
              <span>{highlight(item)}</span>
            </li>
          ))}
        </ul>
      </section>

      {/* How it works */}
      <section id="how-it-works" className="mt-14 scroll-mt-20">
        <h2 className="text-sm font-medium">{HOW_IT_WORKS.title}</h2>
        <div className="mt-3 space-y-3 text-sm leading-relaxed text-muted-foreground">
          {HOW_IT_WORKS.paragraphs.map((p) => (
            <p key={p}>{highlight(p)}</p>
          ))}
        </div>
        <ol className="mt-4 space-y-1 text-sm leading-relaxed text-muted-foreground">
          {HOW_IT_WORKS.steps.map((step, i) => (
            <li key={step} className="flex gap-3">
              <span className="font-mono text-xs text-metric-1">{String(i + 1).padStart(2, "0")}</span>
              <span>{highlight(step)}</span>
            </li>
          ))}
        </ol>
        <div className="mt-10 rounded-lg border border-border bg-card p-5 sm:p-6">
          <h3 className="text-sm font-medium">Why this is different from a standard OpenGL renderer</h3>
          <p className="mt-2 text-sm leading-relaxed text-muted-foreground">{highlight(HOW_IT_WORKS.uniqueFeatures.intro)}</p>
          <ul className="mt-4 space-y-4">
            {HOW_IT_WORKS.uniqueFeatures.items.map((item) => (
              <li key={item.title} className="flex gap-3">
                <span className="mt-0.5 h-2 w-2 shrink-0 rounded-full bg-metric-2" />
                <div>
                  <h4 className="text-sm font-medium">{highlight(item.title)}</h4>
                  <p className="mt-1 text-sm leading-relaxed text-muted-foreground">{highlight(item.body)}</p>
                </div>
              </li>
            ))}
          </ul>
        </div>
      </section>

      {/* Three numbers */}
      <section id="metrics" className="mt-14 scroll-mt-20 grid gap-px overflow-hidden rounded-lg bg-hairline sm:grid-cols-3">
        <Stat
          accent={1}
          label={`p99 frame time · ${hz} Hz`}
          value={m.p99}
          unit="ms"
          sub={`naive sleep: ${m.p99Naive} ms`}
        />
        <Stat accent={2} label="missed deadlines" value={m.missed} unit="%" sub={`naive sleep: ${m.missedNaive} %`} />
        <Stat accent={3} label="render thread CPU" value={m.cpu} unit="%" sub={`naive sleep: ${m.cpuNaive} % — the spin costs this`} />
      </section>

      {/* Filters */}
      <section className="mt-8">
        <div className="flex flex-wrap items-center gap-2">
          {([60, 144, 240] as Hz[]).map((v) => (
            <Chip key={v} active={hz === v} onClick={() => setHz(v)}>
              {v} Hz
            </Chip>
          ))}
          <span className="mx-2 h-4 w-px bg-hairline" />
          {PLATFORMS.map((p) => (
            <Chip key={p} tone="platform" active={platform === p} onClick={() => setPlatform(p)}>
              {PLATFORM_LABEL[p]}
            </Chip>
          ))}
        </div>
        <p className="mt-3 font-mono text-xs text-muted-foreground">
          {PLATFORM_SUBLABEL[platform]} · {prov.run_date} ·{" "}
          <a href={prov.results_url} className="underline underline-offset-4 hover:no-underline">
            {prov.source}
          </a>{" "}
          (commit {prov.commit})
        </p>
        {prov.measurement_class !== "" && (
          <p className="mt-2 max-w-3xl text-xs leading-relaxed text-muted-foreground">{prov.measurement_class}</p>
        )}
      </section>

      {/* Histogram */}
      <section id="histogram" className="mt-6 scroll-mt-20 rounded-lg border border-border bg-card p-5 sm:p-6">
        <div className="mb-4 flex flex-wrap items-baseline justify-between gap-2">
          <h2 className="text-sm font-medium">
            Frame interval distribution — {PLATFORM_LABEL[platform]}, {hz} Hz
          </h2>
          <span className="font-mono text-xs text-muted-foreground">
            <Key accent="metric-3">log y</Key> · <Key accent="metric-1">50 µs data, 0.25 ms display bins</Key> · <Key>9,500 frames per cell</Key>
          </span>
        </div>
        <Histogram hz={hz} platform={platform} />
      </section>

      {/* Tables */}
      <section id="results" className="mt-14 scroll-mt-20">
        <h2 className="flex items-center gap-2 text-sm font-medium"><span className="h-2 w-2 rounded-full bg-metric-2" />Results — <Key accent="metric-2">tuned pacer</Key> (--pace=timer_spin)</h2>
        <ResultTable rows={RESULT_ROWS} />
        <h2 className="mt-10 flex items-center gap-2 text-sm font-medium"><span className="h-2 w-2 rounded-full bg-series-naive" />Baseline — <Key accent="series-naive">naive sleep loop</Key> (--pace=sleep)</h2>
        <ResultTable rows={BASELINE_ROWS} />
        <p className="mt-3 text-xs leading-relaxed text-muted-foreground">
          Frame time is <Key>deadline-to-deadline</Key>, in milliseconds — a zero-miss cell sits exactly at the
          period <Key accent="metric-1">by construction</Key>, and the wake jitter each strategy absorbs is what
          the histogram above shows (start-to-start). Missed is the share of the 9,500 measured frames whose
          deadline was missed. The desktop tuned 60&nbsp;Hz max of 214.352&nbsp;ms is a single OS/driver stall
          caught mid-run (3 misses of 9,500), left in the data.
        </p>
      </section>

      {/* Input handoff */}
      <section id="handoff" className="mt-14 scroll-mt-20">
        <h2 className="text-sm font-medium">{HANDOFF.title}</h2>
        <div className="mt-3 max-w-3xl space-y-3 text-sm leading-relaxed text-muted-foreground">
          {HANDOFF.intro.map((p) => (
            <p key={p}>{highlight(p)}</p>
          ))}
        </div>
        <div className="mt-6 grid gap-4 lg:grid-cols-2">
          <div className="rounded-lg border border-border bg-card p-5 sm:p-6">
            <h3 className="text-sm font-medium">Uncontended per-op cost — {PLATFORM_LABEL[platform]}</h3>
            <div className="mt-4">
              <HandoffCost rows={handoffCost(platform)} />
            </div>
            <p className="mt-3 text-xs leading-relaxed text-muted-foreground">{HANDOFF.costNote}</p>
          </div>
          <div className="rounded-lg border border-border bg-card p-5 sm:p-6">
            <h3 className="text-sm font-medium">Contended sweep — {PLATFORM_LABEL[platform]}</h3>
            <div className="mt-4">
              <HandoffSweep
                rows={handoffSweep(platform)}
                showCrossover={platform === DESKTOP}
                crossoverLabel={HANDOFF.sweepCrossover}
              />
            </div>
            <p className="mt-3 text-xs leading-relaxed text-muted-foreground">{HANDOFF.sweepNote}</p>
          </div>
        </div>
        {appRows.length > 0 ? (
          <div className="mt-6 rounded-lg border border-border bg-card p-5 sm:p-6">
            <h3 className="text-sm font-medium">In-app input latency — desktop run of record</h3>
            <div className="mt-3 overflow-x-auto">
              <table className="w-full min-w-[560px] text-sm">
                <thead>
                  <tr className="border-b border-hairline text-xs text-muted-foreground">
                    <th className="py-2 text-left font-normal">backend</th>
                    <th className="py-2 text-right font-normal">poll target</th>
                    <th className="py-2 text-right font-normal">achieved Hz</th>
                    <th className="py-2 text-right font-normal">p50 latency</th>
                    <th className="py-2 text-right font-normal">p99 latency</th>
                    <th className="py-2 text-right font-normal">retries/s</th>
                  </tr>
                </thead>
                <tbody>
                  {appRows.map((r) => (
                    <tr key={r.backend + r.poll_hz} className="border-b border-hairline/60">
                      <td className="py-2 pr-4 text-left">{r.backend}</td>
                      <td className="py-2 text-right font-mono">{r.poll_hz.toLocaleString()}</td>
                      <td className="py-2 text-right font-mono">{r.achieved_hz.toFixed(1)}</td>
                      <td className="py-2 text-right font-mono text-metric-1">
                        {r.lat_p50_ns === null ? "—" : `${(r.lat_p50_ns / 1e6).toFixed(3)} ms`}
                      </td>
                      <td className="py-2 text-right font-mono text-metric-2">
                        {r.lat_p99_ns === null ? "—" : `${(r.lat_p99_ns / 1e6).toFixed(3)} ms`}
                      </td>
                      <td className="py-2 text-right font-mono text-metric-3">{r.retries_per_sec.toFixed(1)}</td>
                    </tr>
                  ))}
                </tbody>
              </table>
            </div>
            <p className="mt-3 text-xs leading-relaxed text-muted-foreground">{highlight(HANDOFF.appNote)}</p>
          </div>
        ) : (
          <p className="mt-4 text-xs text-muted-foreground">
            In-app latency cells are a desktop protocol — CI runs measure only the micro-benchmark above.
          </p>
        )}
      </section>

      {/* Decision log */}
      <section id="decisions" className="mt-14 scroll-mt-20">
        <h2 className="text-sm font-medium">Decision log</h2>
        <div className="mt-4 space-y-3">
          {DECISIONS.map((d, i) => (
            <article
              key={d.title}
              className={`rounded-lg border border-border border-l-2 bg-card p-5 ${
                ["border-l-metric-1", "border-l-metric-2", "border-l-metric-3", "border-l-series-naive", "border-l-series-target"][i % 5]
              }`}
            >
              <h3 className="text-sm font-medium">{highlight(d.title)}</h3>
              <p className="mt-2 text-sm leading-relaxed text-foreground/80">{highlight(d.reasoning)}</p>
              <p className="mt-3 text-sm leading-relaxed text-muted-foreground">{highlight(d.changeMind)}</p>
            </article>
          ))}
        </div>
      </section>

      {/* Bottom */}
      <section id="notes" className="mt-14 scroll-mt-20 grid gap-10 border-t border-hairline pt-8 sm:grid-cols-2">
        <div>
          <h2 className="text-sm font-medium">Known limitations</h2>
          <ul className="mt-3 space-y-2 text-sm leading-relaxed text-muted-foreground">
            {LIMITATIONS.map((l) => (
              <li key={l} className="flex gap-2">
                <span aria-hidden className="text-series-naive">—</span>
                <span>{highlight(l)}</span>
              </li>
            ))}
          </ul>
        </div>
        <div>
          <h2 className="text-sm font-medium">Build</h2>
          <pre className="mt-3 overflow-x-auto rounded-lg border border-border bg-panel p-4 font-mono text-xs leading-relaxed text-muted-foreground">
{BUILD_SNIPPET}
          </pre>
        </div>
      </section>

      {/* Bottom bar */}
      <footer className="mt-16 flex flex-wrap items-center justify-between gap-4 border-t border-hairline pt-6">
        <p className="font-mono text-xs text-muted-foreground">
          <span className="text-metric-1">tiffany-mares</span>
          <span className="text-muted-foreground">/</span>
          <span className="text-metric-3">opengl-renderer</span>
        </p>
        <div className="flex items-center gap-4">
          <a
            href="https://github.com/tiffany-mares/OpenGL-Renderer"
            className="flex items-center gap-1.5 font-mono text-xs text-muted-foreground transition-colors hover:text-metric-1"
          >
            <Github size={14} strokeWidth={1.5} />
            github
          </a>
          <a
            href="https://www.linkedin.com/in/tiffany-mares"
            className="flex items-center gap-1.5 font-mono text-xs text-muted-foreground transition-colors hover:text-metric-2"
          >
            <Linkedin size={14} strokeWidth={1.5} />
            linkedin
          </a>
          <a
            href="https://tiffanymares.com/"
            className="flex items-center gap-1.5 font-mono text-xs text-muted-foreground transition-colors hover:text-metric-3"
          >
            <Globe size={14} strokeWidth={1.5} />
            contact
          </a>
        </div>
      </footer>
    </main>
  );
}

const ACCENTS = {
  1: { bg: "bg-metric-1-tint", fg: "text-metric-1", bar: "bg-metric-1" },
  2: { bg: "bg-metric-2-tint", fg: "text-metric-2", bar: "bg-metric-2" },
  3: { bg: "bg-metric-3-tint", fg: "text-metric-3", bar: "bg-metric-3" },
} as const;

function Key({
  children,
  accent = "metric-2",
}: {
  children: React.ReactNode;
  accent?: "metric-1" | "metric-2" | "metric-3" | "series-naive" | "series-target";
}) {
  return (
    <span className={`inline-block rounded px-1 py-0 font-mono text-xs leading-tight bg-${accent}-tint text-${accent}`}>
      {children}
    </span>
  );
}

function Stat({
  label,
  value,
  unit,
  sub,
  accent,
}: {
  label: string;
  value: string;
  unit: string;
  sub: string;
  accent: 1 | 2 | 3;
}) {
  const a = ACCENTS[accent];
  return (
    <div className={`relative overflow-hidden px-5 py-6 ${a.bg}`}>
      <span className={`absolute inset-x-0 top-0 h-[2px] ${a.bar}`} />
      <p className="font-mono text-xs uppercase tracking-wide text-muted-foreground">{label}</p>
      <p className={`mt-3 font-mono text-4xl leading-none sm:text-5xl ${a.fg}`}>
        {value}
        <span className="ml-1 text-xl opacity-60">{unit}</span>
      </p>
      <p className="mt-3 font-mono text-xs text-muted-foreground">{sub}</p>
    </div>
  );
}

function Chip({
  active,
  onClick,
  children,
  tone = "hz",
}: {
  active: boolean;
  onClick: () => void;
  children: React.ReactNode;
  tone?: "hz" | "platform";
}) {
  return (
    <button
      type="button"
      onClick={onClick}
      aria-pressed={active}
      className={
        "rounded-full px-3 py-1 font-mono text-xs transition-colors " +
        (active
          ? tone === "hz"
            ? "bg-metric-1 text-primary-foreground"
            : "bg-metric-3 text-primary-foreground"
          : "border border-border text-muted-foreground hover:border-metric-2 hover:text-metric-2")
      }
    >
      {children}
    </button>
  );
}

function ResultTable({ rows }: { rows: Row[] }) {
  return (
    <div className="mt-3 overflow-x-auto">
      <table className="w-full min-w-[720px] text-sm">
        <thead>
          <tr className="border-b border-hairline text-xs text-muted-foreground">
            <th className="py-2 text-left font-normal">platform</th>
            <th className="py-2 text-right font-normal">Hz</th>
            <th className="py-2 text-right font-normal">p50</th>
            <th className="py-2 text-right font-normal">p95</th>
            <th className="py-2 text-right font-normal">p99</th>
            <th className="py-2 text-right font-normal">max</th>
            <th className="py-2 text-right font-normal">missed %</th>
            <th className="py-2 text-right font-normal">CPU %</th>
          </tr>
        </thead>
        <tbody>
          {rows.map((r) => (
            <tr key={r.platform + r.hz} className="border-b border-hairline/60">
              <td className="py-2 pr-4 text-left">{r.platform}</td>
              <td className="py-2 text-right font-mono">{r.hz}</td>
              <td className="py-2 text-right font-mono">{r.p50}</td>
              <td className="py-2 text-right font-mono">{r.p95}</td>
              <td className="py-2 text-right font-mono text-metric-1">{r.p99}</td>
              <td className="py-2 text-right font-mono">{r.max}</td>
              <td className="py-2 text-right font-mono text-metric-2">{r.missed}</td>
              <td className="py-2 text-right font-mono text-metric-3">{r.cpu}</td>
            </tr>
          ))}
        </tbody>
      </table>
    </div>
  );
}

const KEYWORDS: { term: string; accent: Parameters<typeof Key>[0]["accent"] }[] = [
  { term: "would change my mind:", accent: "series-target" },
  { term: "--pace=sleep|timer|timer_spin|spin", accent: "metric-2" },
  { term: "--input=mutex|bitmask|seqlock", accent: "metric-2" },
  { term: "--resched=relative", accent: "series-naive" },
  { term: "CreateWaitableTimerExW", accent: "metric-3" },
  { term: "clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME)", accent: "metric-3" },
  { term: "CLOCK_THREAD_CPUTIME_ID", accent: "metric-3" },
  { term: "QueryThreadCycleTime", accent: "metric-3" },
  { term: "requestAnimationFrame", accent: "metric-3" },
  { term: "THREAD_TIME_CONSTRAINT_POLICY", accent: "metric-3" },
  { term: "glfwSwapInterval(0)", accent: "metric-2" },
  { term: "Welford mean + 3σ", accent: "metric-1" },
  { term: "next += period", accent: "metric-2" },
  { term: "now + period", accent: "series-naive" },
  { term: "≈100,000 publishes/s", accent: "metric-1" },
  { term: "≈0.002 per second", accent: "metric-1" },
  { term: "15.6 ms timer tick", accent: "series-naive" },
  { term: "15.6 ms scheduler ticks", accent: "series-naive" },
  { term: "2–3 ms late", accent: "series-naive" },
  { term: "p50 ≈ 0.64 ms", accent: "metric-1" },
  { term: "0.345 ms", accent: "metric-1" },
  { term: "+0.664 ms per frame", accent: "series-naive" },
  { term: "5.7 s behind after a minute", accent: "series-naive" },
  { term: "zero missed deadlines", accent: "metric-1" },
  { term: "missed-deadline", accent: "metric-2" },
  { term: "absolute deadlines", accent: "metric-2" },
  { term: "deadline-to-deadline", accent: "metric-2" },
  { term: "start-to-start", accent: "metric-2" },
  { term: "std::mutex", accent: "metric-1" },
  { term: "seqlock", accent: "metric-3" },
  { term: "bitmask", accent: "metric-2" },
  { term: "timer_spin", accent: "metric-2" },
  { term: "~100 µs synthetic", accent: "metric-2" },
  { term: "~1 kHz", accent: "metric-1" },
  { term: "1,000 publishes/s", accent: "metric-1" },
  { term: "llvmpipe", accent: "metric-3" },
  { term: "data race", accent: "series-naive" },
  { term: "undefined behavior", accent: "series-naive" },
  { term: "torn=0", accent: "metric-2" },
  { term: "single-threaded", accent: "metric-2" },
  { term: "WebGL2", accent: "metric-3" },
  { term: "native build", accent: "metric-2" },
  { term: "frame pacer", accent: "metric-2" },
  { term: "frame deadline", accent: "metric-3" },
  { term: "naive sleep loop", accent: "series-naive" },
  { term: "9,500 measured frames", accent: "metric-2" },
  { term: "144 Hz", accent: "metric-1" },
  { term: "AC power", accent: "metric-3" },
  { term: "OS scheduler", accent: "series-naive" },
];

function highlight(text: string): React.ReactNode {
  const pattern = new RegExp(
    `(${KEYWORDS.map((k) => k.term.replace(/[.*+?^${}()|[\]\\]/g, "\\$&")).join("|")})`,
    "g"
  );
  const parts = text.split(pattern);
  return parts.map((part, i) => {
    const match = KEYWORDS.find((k) => k.term === part);
    return match ? <Key key={i} accent={match.accent}>{part}</Key> : <span key={i}>{part}</span>;
  });
}
```

Implementer notes:
- `KEYWORDS` is ordered longest/most-specific first where terms overlap (e.g. `15.6 ms timer tick` before any shorter `15.6` term) — the regex alternation takes the first match.
- The dynamic `bg-${accent}-tint` classes in `Key` are the existing Lovable pattern; all five accent tint/text utilities are already emitted by the current page, and this file keeps using the same five accent names, so no Tailwind safelist work is needed. If a tint class ever renders unstyled, add the ten class names in a comment block to keep them in Tailwind's content scan.
- If `tsc` flags a `KEYWORDS` term entry, check the string matches EXACTLY what `lab-content.ts` contains (en-dashes and ≈ signs included).

- [ ] **Step 4: Build green + visual pass**

```powershell
cd lab; npx tsc --noEmit; npm run build; npm run dev
```

Expected: zero tsc errors, build exits 0, dev server serves the page. Visual pass (browser):
1. Cube renders and spins; click → focus ring; arrows yaw/pitch; SPACE pauses; console clean; fps overlay ticking.
2. Cycle all 3 platform chips × 3 Hz chips: tiles, histogram, tables, handoff charts all update; no NaN/undefined/empty panels.
3. `win11-arc`: crossover line visible in sweep; app latency table present. CI platforms: no crossover line; desktop-protocol note instead of app table; measurement-class caveat under the chips.
4. Histogram at `windows-latest` + 144 Hz shows the naive mass near 16 ms; off-scale note appears for `windows-latest` 240 Hz (max 44.4 ms) and `win11-arc` 60 Hz tuned (214 ms stall).
5. Hot-reload once (touch a file) — cube must not double-boot (console shows no second Emscripten banner).

- [ ] **Step 5: Commit**

```powershell
cd ..
git add lab/src/routes/index.tsx lab/src/routes/__root.tsx lab/src/components/lab/SectionNav.tsx
git commit -m @'
feat: lab page tells the real story -- branding, real tiles/tables/histogram, handoff section, wasm cube wired (Phase 9)

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>
'@
```

---

### Task 9: Final verification + CLAUDE.md note

**Files:**
- Modify: `CLAUDE.md` (append one paragraph to the Project section)

- [ ] **Step 1: Full re-verify from clean**

```powershell
cd lab
npm run gen-data
git -C .. diff --stat lab/src/lib/lab-data.gen.json   # expect: no diff (deterministic)
npm run build                                          # stage-cube + vite build, exit 0
cd ..
git status --short                                     # expect: only CLAUDE.md pending below
```

- [ ] **Step 2: Prose audit (grep the fabrications are gone)**

```powershell
Get-ChildItem lab/src -Recurse -Include *.ts,*.tsx | Select-String -Pattern "macOS 14|M2 Pro|7950X|Linux 6.8|600 000|capture card|nine/frame-pacer|unredirected|VRR"
```

Expected: zero matches. (`macOS` alone still appears — legitimately — in the limitations entry about `mach_wait_until`.)

- [ ] **Step 3: Append to `CLAUDE.md`'s Project section**

Append after the Phase 8d sentence, same run-on style as the rest of the paragraph:

```
Phase 9 (2026-07-28): the lab page — `lab/` (Lovable-exported TanStack Start/React/Tailwind app, adopted into the repo) is the designed replacement-in-waiting for `web/index.html`: `lab/scripts/gen-lab-data.mjs` (Node stdlib, FATAL-guarded) regenerates the committed `lab/src/lib/lab-data.gen.json` verbatim from the committed bench CSVs/JSONs for all three platforms (win11-arc, windows-latest, ubuntu-latest — re-run it after a weekly CI data refresh), `lab/scripts/stage-cube.mjs` (predev/prebuild) copies `dist/cube.{js,wasm}` into `lab/public/` (git-ignored; FATALs pointing at `web/build.py` when missing), `CubeWasm.tsx` boots the real Emscripten cube once per document via the `window.Module = {canvas}` contract, and the page covers pacing (tuned=timer_spin vs naive=sleep) plus the input-handoff section (cost bars, log-x contended sweep with desktop-only ≈100k crossover, desktop-only app-latency table). The Cloudflare deployment swap (lab build into pages.yml, retiring web/index.html + dashboard.js) is the deliberate next step, gated on user approval of the local page — see docs/superpowers/specs/2026-07-28-frame-pacer-lab-design.md.
```

- [ ] **Step 4: Commit + finish**

```powershell
git add CLAUDE.md
git commit -m @'
docs: CLAUDE.md Phase 9 -- lab page wired to real data, swap step pending (Phase 9)

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>
'@
```

Then invoke `superpowers:finishing-a-development-branch` conventions as applicable (this repo works directly on `main`; work is already committed — confirm with the user before pushing).

## Self-Review Notes (completed)

- **Spec coverage:** folder rename+adoption (T1), generator+validation+determinism (T2), typed accessors incl. histogram fold/overflow (T3), all prose rewritten with real claims (T4), histogram component (T5), wasm cube + stage script + boot-once + focus + fps overlay + runtime error note (T6), handoff cost/sweep components with desktop-only crossover (T7), page/branding/meta/nav/handoff section/app table/build snippet (T8), CLAUDE.md + prose audit + determinism re-check (T9). Deployment swap intentionally absent (spec: out of scope).
- **Known interim states:** Tasks 3–7 leave `index.tsx` red on tsc; each task's typecheck step scopes expectations accordingly; build gate returns in Task 8 Step 4.
- **Type consistency:** `Metrics`/`Bin`/`Row`/`HandoffCostRow`/`HandoffSweepRow`/`HandoffAppRow` defined once in Task 3 and consumed by name in Tasks 5/7/8; `BACKEND_COLOR` exported from `HandoffCost.tsx` and imported by `HandoffSweep.tsx`; content shapes in Task 4 mirror the old `lab-data.ts` exports that `index.tsx` (Task 8) imports from `lab-content` instead.





