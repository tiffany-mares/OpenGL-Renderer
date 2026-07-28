import { useEffect, useRef, useState } from "react";

const VERTS: [number, number, number][] = [
  [-1, -1, -1],
  [1, -1, -1],
  [1, 1, -1],
  [-1, 1, -1],
  [-1, -1, 1],
  [1, -1, 1],
  [1, 1, 1],
  [-1, 1, 1],
];

const EDGES: [number, number][] = [
  [0, 1],
  [1, 2],
  [2, 3],
  [3, 0],
  [4, 5],
  [5, 6],
  [6, 7],
  [7, 4],
  [0, 4],
  [1, 5],
  [2, 6],
  [3, 7],
];

export function Cube() {
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const [fps, setFps] = useState(0);
  const [frameMs, setFrameMs] = useState(0);

  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas) return;
    const ctx = canvas.getContext("2d");
    if (!ctx) return;

    let raf = 0;
    let t = 0;
    let last = performance.now();
    let acc = 0;
    let frames = 0;
    let msAcc = 0;

    let front = "#7fe8e0";
    let back = "#e8c07a";
    const readColors = () => {
      const cs = getComputedStyle(document.documentElement);
      front = cs.getPropertyValue("--metric-1").trim() || front;
      back = cs.getPropertyValue("--metric-3").trim() || back;
    };
    readColors();
    const colorTimer = window.setInterval(readColors, 500);

    const draw = (now: number) => {
      const dt = now - last;
      last = now;
      t += dt / 1000;
      frames += 1;
      acc += dt;
      msAcc += dt;
      if (acc >= 400) {
        setFps(Math.round((frames * 1000) / acc));
        setFrameMs(msAcc / frames);
        frames = 0;
        acc = 0;
        msAcc = 0;
      }

      const dpr = Math.min(window.devicePixelRatio || 1, 2);
      const w = canvas.clientWidth;
      const h = canvas.clientHeight;
      if (canvas.width !== w * dpr || canvas.height !== h * dpr) {
        canvas.width = w * dpr;
        canvas.height = h * dpr;
      }
      ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
      ctx.clearRect(0, 0, w, h);

      const ax = t * 0.6;
      const ay = t * 0.85;
      const scale = Math.min(w, h) * 0.2;
      const proj = VERTS.map(([x, y, z]) => {
        let y1 = y * Math.cos(ax) - z * Math.sin(ax);
        let z1 = y * Math.sin(ax) + z * Math.cos(ax);
        const x1 = x * Math.cos(ay) + z1 * Math.sin(ay);
        z1 = -x * Math.sin(ay) + z1 * Math.cos(ay);
        const p = 4 / (4 + z1);
        return [w / 2 + x1 * scale * p, h / 2 + y1 * scale * p, z1] as const;
      });

      for (const [a, b] of EDGES) {
        const pa = proj[a];
        const pb = proj[b];
        const depth = (pa[2] + pb[2]) / 2;
        ctx.strokeStyle = depth > 0 ? back : front;
        ctx.globalAlpha = depth > 0 ? 0.45 : 1;
        ctx.lineWidth = depth > 0 ? 1 : 1.6;
        ctx.beginPath();
        ctx.moveTo(pa[0], pa[1]);
        ctx.lineTo(pb[0], pb[1]);
        ctx.stroke();
      }
      ctx.globalAlpha = 1;

      raf = requestAnimationFrame(draw);
    };

    raf = requestAnimationFrame(draw);
    return () => {
      cancelAnimationFrame(raf);
      clearInterval(colorTimer);
    };
  }, []);

  return (
    <div className="relative rounded-lg border border-border bg-panel">
      <canvas ref={canvasRef} className="block h-[clamp(220px,30vh,300px)] w-full" />
      <div className="pointer-events-none absolute right-3 top-3 font-mono text-xs text-muted-foreground">
        <div className="text-foreground">{fps.toString().padStart(3, " ")} fps</div>
        <div>{frameMs.toFixed(2)} ms/frame</div>
      </div>
    </div>
  );
}
