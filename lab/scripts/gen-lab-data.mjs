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
