import { createFileRoute } from "@tanstack/react-router";
import { useState } from "react";
import { Github, Linkedin, Globe } from "lucide-react";
import { Cube } from "@/components/lab/Cube";
import { Histogram } from "@/components/lab/Histogram";
import { SectionNav } from "@/components/lab/SectionNav";
import {
  BACKSTORY,
  BASELINE_ROWS,
  DECISIONS,
  HOW_IT_WORKS,
  LIMITATIONS,
  PLATFORM_LABEL,
  RESULT_ROWS,
  metrics,
  type Hz,
  type Platform,
  type Row,
} from "@/lib/lab-data";

export const Route = createFileRoute("/")({
  head: () => ({
    meta: [
      { title: "frame-pacer — hitting a frame deadline on an unwilling OS" },
      {
        name: "description",
        content:
          "Measurements and decisions from a frame pacer: p99 jitter, missed deadlines and render-thread CPU across 60, 144 and 240 Hz on three platforms.",
      },
      { property: "og:title", content: "frame-pacer — hitting a frame deadline on an unwilling OS" },
      {
        property: "og:description",
        content:
          "A lab writeup: sub-millisecond p99 frame jitter versus a naive sleep baseline, with the CPU cost stated plainly.",
      },
      { property: "og:type", content: "article" },
      { name: "twitter:card", content: "summary_large_image" },
    ],
  }),
  component: Index,
});

function Index() {
  const [hz, setHz] = useState<Hz>(144);
  const [platform, setPlatform] = useState<Platform>("win11");
  const m = metrics(hz, platform);

  return (
    <main className="mx-auto max-w-5xl px-6 pb-24 pt-10 sm:px-8">
      {/* Top strip */}
      <header className="flex flex-wrap items-start justify-between gap-4 border-b border-hairline pb-8">
        <div className="max-w-2xl">
          <p className="font-mono text-xs">
            <span className="text-metric-1">nine</span>
            <span className="text-muted-foreground">/</span>
            <span className="text-metric-3">frame-pacer</span>
          </p>
          <h1 className="mt-2 text-balance text-2xl font-medium leading-snug sm:text-3xl">
            Hitting a <Key accent="metric-3">frame deadline</Key> on an <Key accent="series-naive">OS</Key> that doesn&apos;t want you to.
          </h1>
          <p className="mt-3 text-sm leading-relaxed text-muted-foreground">
            A <Key>render-loop pacer</Key> that holds the interval to within a <Key accent="metric-1">third of a millisecond</Key> at <Key accent="metric-1">144 Hz</Key>,
            and what it costs to do that.
          </p>
        </div>
        <div className="flex shrink-0 flex-col items-end gap-1 font-mono text-xs text-primary">
          <a href="https://github.com/tiffany-mares/OpenGL-Renderer" className="underline underline-offset-4 hover:no-underline">
            github ↗
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
        <Cube />
        <p className="mt-3 text-xs leading-relaxed text-muted-foreground">
          This browser build is <Key>single threaded</Key> and paced by <Key accent="metric-3">requestAnimationFrame</Key>,
          not by the pacer. There is no <Key accent="metric-1">high-resolution timer</Key>, no <Key accent="metric-1">spin margin</Key> and no control over presentation here,
          so treat the readout as a liveness check — every number below comes from the <Key accent="metric-2">native build</Key>.
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
          label={`p99 jitter · ${hz} Hz`}
          value={m.p99}
          unit="ms"
          sub={`naive sleep: ${m.p99Naive} ms`}
        />
        <Stat accent={2} label="missed deadlines" value={m.missed} unit="%" sub={`naive sleep: ${m.missedNaive} %`} />
        <Stat accent={3} label="render thread CPU" value={m.cpu} unit="%" sub={`naive sleep: ${m.cpuNaive} % — the spin costs this`} />
      </section>

      {/* Filters */}
      <section className="mt-8 flex flex-wrap items-center gap-2">
        {([60, 144, 240] as Hz[]).map((v) => (
          <Chip key={v} active={hz === v} onClick={() => setHz(v)}>
            {v} Hz
          </Chip>
        ))}
        <span className="mx-2 h-4 w-px bg-hairline" />
        {(Object.keys(PLATFORM_LABEL) as Platform[]).map((p) => (
          <Chip key={p} tone="platform" active={platform === p} onClick={() => setPlatform(p)}>
            {PLATFORM_LABEL[p]}
          </Chip>
        ))}
      </section>

      {/* Histogram */}
      <section id="histogram" className="mt-6 scroll-mt-20 rounded-lg border border-border bg-card p-5 sm:p-6">
        <div className="mb-4 flex flex-wrap items-baseline justify-between gap-2">
          <h2 className="text-sm font-medium">
            Frame interval distribution — {PLATFORM_LABEL[platform]}, {hz} Hz
          </h2>
          <span className="font-mono text-xs text-muted-foreground">
            <Key accent="metric-3">log y</Key> · <Key accent="metric-1">0.25 ms bins</Key> · <Key>600 000 frames</Key>
          </span>
        </div>
        <Histogram hz={hz} platform={platform} />
      </section>

      {/* Tables */}
      <section id="results" className="mt-14 scroll-mt-20">
        <h2 className="flex items-center gap-2 text-sm font-medium"><span className="h-2 w-2 rounded-full bg-metric-2" />Results — <Key accent="metric-2">tuned pacer</Key></h2>
        <ResultTable rows={RESULT_ROWS} />
        <h2 className="mt-10 flex items-center gap-2 text-sm font-medium"><span className="h-2 w-2 rounded-full bg-series-naive" />Baseline — <Key accent="series-naive">naive sleep loop</Key></h2>
        <ResultTable rows={BASELINE_ROWS} />
        <p className="mt-3 text-xs text-muted-foreground">
          Jitter columns are <Key>absolute deviation</Key> from the <Key accent="metric-1">target interval</Key>, in milliseconds. Missed is the share of
          intervals longer than the target.
        </p>
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
              <p className="mt-3 text-sm leading-relaxed text-muted-foreground">
                {highlight(d.changeMind)}
              </p>
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
{`git clone https://github.com/nine/frame-pacer
cd frame-pacer
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j

./build/pacer --hz 144 --frames 600000 --csv out.csv
./build/pacer --hz 144 --baseline sleep --csv naive.csv
python tools/hist.py out.csv naive.csv`}
          </pre>
        </div>
      </section>

      {/* Bottom bar */}
      <footer className="mt-16 flex flex-wrap items-center justify-between gap-4 border-t border-hairline pt-6">
        <p className="font-mono text-xs text-muted-foreground">
          <span className="text-metric-1">nine</span>
          <span className="text-muted-foreground">/</span>
          <span className="text-metric-3">frame-pacer</span>
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
      <table className="w-full min-w-[640px] text-sm">
        <thead>
          <tr className="border-b border-hairline text-xs text-muted-foreground">
            <th className="py-2 text-left font-normal">configuration</th>
            <th className="py-2 text-right font-normal">Hz</th>
            <th className="py-2 text-right font-normal">p50 jitter</th>
            <th className="py-2 text-right font-normal">p99 jitter</th>
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
  { term: "missed-deadline percentage", accent: "metric-2" },
  { term: "average frame time", accent: "metric-2" },
  { term: "mach_absolute_time", accent: "metric-3" },
  { term: "requestAnimationFrame", accent: "metric-3" },
  { term: "high-resolution timer", accent: "metric-1" },
  { term: "timeBeginPeriod", accent: "metric-2" },
  { term: "timer resolution", accent: "metric-2" },
  { term: "render thread", accent: "metric-3" },
  { term: "timer thread", accent: "metric-3" },
  { term: "spin margin", accent: "metric-1" },
  { term: "Spin margin", accent: "metric-1" },
  { term: "Spin-wait", accent: "metric-1" },
  { term: "native build", accent: "metric-2" },
  { term: "single threaded", accent: "metric-2" },
  { term: "target interval", accent: "metric-1" },
  { term: "absolute deviation", accent: "metric-2" },
  { term: "tuned pacer", accent: "metric-2" },
  { term: "naive sleep loop", accent: "series-naive" },
  { term: "QPC", accent: "metric-3" },
  { term: "1.2 ms", accent: "metric-1" },
  { term: "p99", accent: "metric-1" },
  { term: "0.5 ms", accent: "metric-2" },
  { term: "7.9 ms", accent: "series-naive" },
  { term: "half a millisecond", accent: "metric-1" },
  { term: "compositor", accent: "metric-3" },
  { term: "capture card", accent: "metric-2" },
  { term: "render-loop pacer", accent: "metric-2" },
  { term: "frame deadline", accent: "metric-3" },
  { term: "third of a millisecond", accent: "metric-1" },
  { term: "600 000 frames", accent: "metric-2" },
  { term: "0.25 ms bins", accent: "metric-1" },
  { term: "log y", accent: "metric-3" },
  { term: "144 Hz", accent: "metric-1" },
  { term: "OS", accent: "series-naive" },
  { term: "unredirected fullscreen", accent: "metric-3" },
  { term: "Windowed mode", accent: "series-naive" },
  { term: "fixed refresh target", accent: "metric-2" },
  { term: "VRR", accent: "series-naive" },
  { term: "GPU vendors", accent: "metric-3" },
  { term: "laptop power profiles", accent: "metric-3" },
  { term: "raw hardware counter", accent: "metric-1" },
  { term: "raw counter", accent: "metric-1" },
  { term: "spin-busy-waits", accent: "metric-1" },
  { term: "small margin", accent: "metric-1" },
  { term: "OS scheduler", accent: "series-naive" },
  { term: "present exactly at the deadline", accent: "metric-2" },
  { term: "process timer resolution", accent: "metric-2" },
  { term: "timer granules", accent: "metric-2" },
  { term: "compositor's timestamp", accent: "metric-3" },
  { term: "vsync", accent: "series-naive" },
  { term: "VSync", accent: "series-naive" },
  { term: "glfwSwapBuffers", accent: "series-naive" },
  { term: "swap interval", accent: "series-naive" },
  { term: "triple-buffering", accent: "series-naive" },
  { term: "Explicit frame pacer", accent: "metric-2" },
  { term: "Sub-millisecond timing control", accent: "metric-1" },
  { term: "Measurable jitter and misses", accent: "metric-2" },
  { term: "Adaptive to work load", accent: "metric-3" },
  { term: "Portable native timing", accent: "metric-1" },
  { term: "OS compositor", accent: "series-naive" },
  { term: "high-resolution counter", accent: "metric-1" },
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
