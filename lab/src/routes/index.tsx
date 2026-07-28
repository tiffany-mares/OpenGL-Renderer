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

const KEY_ACCENT_CLASSES = {
  "metric-1": "bg-metric-1-tint text-metric-1",
  "metric-2": "bg-metric-2-tint text-metric-2",
  "metric-3": "bg-metric-3-tint text-metric-3",
  "series-naive": "bg-series-naive-tint text-series-naive",
  "series-target": "bg-series-target-tint text-series-target",
} as const;

function Key({
  children,
  accent = "metric-2",
}: {
  children: React.ReactNode;
  accent?: "metric-1" | "metric-2" | "metric-3" | "series-naive" | "series-target";
}) {
  return (
    <span className={`inline-block rounded px-1 py-0 font-mono text-xs leading-tight ${KEY_ACCENT_CLASSES[accent]}`}>
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
  { term: "frame pacer", accent: "metric-2" },
  { term: "frame deadline", accent: "metric-3" },
  { term: "9,500 measured frames", accent: "metric-2" },
  { term: "144 Hz", accent: "metric-1" },
  { term: "AC power", accent: "metric-3" },
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
