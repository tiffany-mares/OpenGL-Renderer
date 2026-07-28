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
