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
  const barH = (c: number) => (c > 0 ? Math.max(3, H - PAD.bottom - y(c)) : 0);
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
            y={H - PAD.bottom - barH(b.naive)}
            width={barW}
            height={barH(b.naive)}
            fill="var(--series-naive)"
            opacity={0.75}
          />
        ))}
        {bins.map((b) => (
          <rect
            key={`t${b.ms}`}
            x={x(b.ms)}
            y={H - PAD.bottom - barH(b.tuned)}
            width={barW}
            height={barH(b.tuned)}
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
