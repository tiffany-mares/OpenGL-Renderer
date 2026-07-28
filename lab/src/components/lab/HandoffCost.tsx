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
