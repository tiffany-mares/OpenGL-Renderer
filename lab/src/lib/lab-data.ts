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

/** Aggregate one 50 µ cell into 0.25 ms display bins, tracking >cap overflow. */
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
