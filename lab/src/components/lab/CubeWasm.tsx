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
    if (!canvas) return;

    if (!booted) {
      booted = true;
      window.Module = { canvas, onAbort: () => setFailed(true) };
      const script = document.createElement("script");
      script.src = "/cube.js";
      script.onerror = () => setFailed(true);
      document.body.appendChild(script);
    }

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
          demo failed to load, this build needs WebGL2. The numbers below are unaffected.
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
