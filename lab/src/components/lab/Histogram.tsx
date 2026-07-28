import { histogram, targetMs, type Hz, type Platform } from "@/lib/lab-data";

const W = 1000;
const H = 340;
const PAD = { top: 16, right: 16, bottom: 40, left: 52 };

export function Histogram({ hz, platform }: { hz: Hz; platform: Platform }) {
  const bins = histogram(hz, platform);
  const target = targetMs(hz);
  const maxMs = 34;
  const maxCount = 25000;

  const x = (ms: number) => PAD.left + (ms / maxMs) * (W - PAD.left - PAD.right);
  const y = (c: number) => {
    const v = Math.max(c, 1);
    const logMax = Math.log10(maxCount);
    return H - PAD.bottom - (Math.log10(v) / logMax) * (H - PAD.top - PAD.bottom);
  };

  const barW = Math.max(1.5, x(0.25) - x(0));

  const yTicks = [1, 10, 100, 1000, 10000];
  const xTicks = [0, 4, 8, 12, 16, 20, 24, 28, 32];

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
            x={x(b.ms) - barW / 2}
            y={y(b.naive)}
            width={barW}
            height={Math.max(0, H - PAD.bottom - y(b.naive))}
            fill="var(--series-naive)"
            opacity={0.75}
          />
        ))}
        {bins.map((b) => (
          <rect
            key={`t${b.ms}`}
            x={x(b.ms) - barW / 2}
            y={y(b.tuned)}
            width={barW}
            height={Math.max(0, H - PAD.bottom - y(b.tuned))}
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
          frame interval (milliseconds)
        </text>
      </svg>

      <div className="mt-3 flex flex-wrap gap-x-6 gap-y-2 font-mono text-xs text-muted-foreground">
        <Legend color="var(--series-naive)" label="naive sleep baseline" />
        <Legend color="var(--series-tuned)" label="tuned pacer" />
        <Legend color="var(--series-target)" label={`target interval (${target.toFixed(2)} ms)`} dashed />
      </div>
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
