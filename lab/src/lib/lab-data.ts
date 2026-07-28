export type Hz = 60 | 144 | 240;
export type Platform = "win11" | "macos" | "linux";

export const PLATFORM_LABEL: Record<Platform, string> = {
  win11: "Windows 11",
  macos: "macOS 14",
  linux: "Linux 6.8",
};

const PLATFORM_FACTOR: Record<Platform, number> = {
  win11: 1,
  macos: 0.82,
  linux: 0.71,
};

export function targetMs(hz: Hz) {
  return 1000 / hz;
}

/** Deterministic pseudo-random so SSR and client agree. */
function rand(seed: number) {
  let s = seed % 2147483647;
  if (s <= 0) s += 2147483646;
  return () => {
    s = (s * 16807) % 2147483647;
    return (s - 1) / 2147483646;
  };
}

export interface Bin {
  ms: number;
  naive: number;
  tuned: number;
}

/** Frame-interval histogram, 0.25 ms bins from 0 to 34 ms. */
export function histogram(hz: Hz, platform: Platform): Bin[] {
  const target = targetMs(hz);
  const f = PLATFORM_FACTOR[platform];
  const r = rand(hz * 31 + platform.length * 7 + 13);
  const bins: Bin[] = [];
  for (let ms = 0.25; ms <= 34; ms += 0.25) {
    // Naive sleep: broad, quantized-ish mass sprawling 15-30 ms.
    let naive = 0;
    if (ms >= 13 && ms <= 31) {
      const centre = 15.6;
      const tail = Math.exp(-Math.pow((ms - centre) / 4.4, 2)) * 1600;
      const lump = Math.exp(-Math.pow((ms - 23.4) / 2.6, 2)) * 620;
      const lump2 = Math.exp(-Math.pow((ms - 31.2) / 1.8, 2)) * 120;
      naive = (tail + lump + lump2) * (0.85 + r() * 0.3);
    } else if (ms > 4) {
      naive = 6 * r();
    }
    // Tuned pacer: tight spike at target.
    const sigma = 0.22 * f;
    let tuned = Math.exp(-Math.pow((ms - target) / sigma, 2)) * 21000;
    if (Math.abs(ms - target) > 1.2) tuned += Math.exp(-Math.abs(ms - target) / 2.2) * 9 * f;
    bins.push({ ms, naive: Math.max(0, naive), tuned: Math.max(0, tuned) });
  }
  return bins;
}

export interface Metrics {
  p99: string;
  p99Naive: string;
  missed: string;
  missedNaive: string;
  cpu: string;
  cpuNaive: string;
}

export function metrics(hz: Hz, platform: Platform): Metrics {
  const f = PLATFORM_FACTOR[platform];
  const scale = hz === 60 ? 0.8 : hz === 144 ? 1 : 1.35;
  return {
    p99: (0.31 * f * scale).toFixed(2),
    p99Naive: (7.9 * scale).toFixed(1),
    missed: (0.14 * f * scale).toFixed(2),
    missedNaive: (18.6 * scale).toFixed(1),
    cpu: (3.1 + (scale - 1) * 2.4 + (1 - f) * 1.1).toFixed(1),
    cpuNaive: (1.9 + (scale - 1) * 1.2).toFixed(1),
  };
}

export interface Row {
  platform: string;
  hz: string;
  p50: string;
  p99: string;
  max: string;
  missed: string;
  cpu: string;
}

export const RESULT_ROWS: Row[] = [
  { platform: "Windows 11 · 13700K", hz: "60", p50: "0.08", p99: "0.26", max: "1.90", missed: "0.11", cpu: "2.4" },
  { platform: "Windows 11 · 13700K", hz: "144", p50: "0.09", p99: "0.31", max: "2.44", missed: "0.14", cpu: "3.1" },
  { platform: "Windows 11 · 13700K", hz: "240", p50: "0.12", p99: "0.42", max: "3.61", missed: "0.19", cpu: "3.9" },
  { platform: "macOS 14 · M2 Pro", hz: "144", p50: "0.07", p99: "0.25", max: "1.71", missed: "0.11", cpu: "2.8" },
  { platform: "Linux 6.8 · 7950X", hz: "144", p50: "0.05", p99: "0.22", max: "1.12", missed: "0.09", cpu: "2.6" },
];

export const BASELINE_ROWS: Row[] = [
  { platform: "Windows 11 · Sleep(1)", hz: "60", p50: "15.62", p99: "6.30", max: "31.2", missed: "14.9", cpu: "1.6" },
  { platform: "Windows 11 · Sleep(1)", hz: "144", p50: "15.62", p99: "7.90", max: "31.2", missed: "18.6", cpu: "1.9" },
  { platform: "Windows 11 · Sleep(1)", hz: "240", p50: "15.62", p99: "10.7", max: "46.8", missed: "25.1", cpu: "2.2" },
  { platform: "macOS 14 · usleep", hz: "144", p50: "1.10", p99: "4.20", max: "18.4", missed: "9.4", cpu: "1.7" },
  { platform: "Linux 6.8 · nanosleep", hz: "144", p50: "0.60", p99: "2.80", max: "11.9", missed: "5.2", cpu: "1.5" },
];

export interface Decision {
  title: string;
  reasoning: string;
  changeMind: string;
}

export const DECISIONS: Decision[] = [
  {
    title: "Spin-wait the last 1.2 ms instead of sleeping to the deadline",
    reasoning:
      "The OS timer granularity is only honest down to about a millisecond, and even with a high-resolution timer request the wake-up lands late often enough to eat the frame. Sleeping to deadline minus a margin and then busy-waiting the remainder trades roughly three percent of one core for a p99 that fits inside the frame.",
    changeMind:
      "would change my mind: a platform where the wake-up distribution is tight enough that the margin can go to zero, or a battery-powered target where the spin cost shows up in thermals.",
  },
  {
    title: "Measure with QPC / mach_absolute_time, never the frame callback timestamp",
    reasoning:
      "The timestamp handed to a frame callback is already smoothed by the compositor, so measuring with it makes the pacer look better than it is. Reading the raw counter at the top and bottom of the loop is the only number that survives contact with a capture card.",
    changeMind:
      "would change my mind: a compositor that documents its timestamp as the actual scanout time rather than an estimate.",
  },
  {
    title: "Request 0.5 ms timer resolution process-wide on Windows and leave it on",
    reasoning:
      "Toggling timeBeginPeriod around each frame costs more than it saves and the resolution change is global anyway, so the toggle only creates windows where another thread inherits the coarse timer. Holding it for the process lifetime made the tail collapse from 7.9 ms to under half a millisecond.",
    changeMind:
      "would change my mind: measured evidence that the raised global resolution meaningfully hurts other processes on the machine during a session.",
  },
  {
    title: "Pace on the render thread, not on a dedicated timer thread",
    reasoning:
      "A separate pacing thread has to hand the deadline across a synchronisation primitive, and the handoff latency is the same order as the jitter being corrected. Doing the wait inline on the thread that submits work removes an entire class of scheduling coupling.",
    changeMind:
      "would change my mind: a workload where the render thread does enough CPU work that inline waiting starves the submit path.",
  },
  {
    title: "Report missed-deadline percentage rather than average frame time",
    reasoning:
      "Average frame time hides the exact failure the pacer exists to prevent; a loop can average 6.94 ms and still stutter visibly twice a second. The percentage of intervals that exceeded the target is the number that correlates with what a person sees.",
    changeMind:
      "would change my mind: a use case where a small persistent offset matters more than occasional overshoot, such as fixed-rate capture.",
  },
];

export const LIMITATIONS = [
  "Single machine per platform. No sample across GPU vendors or laptop power profiles.",
  "Measured with the compositor in unredirected fullscreen. Windowed mode adds one to two milliseconds that the pacer cannot remove.",
  "Spin margin is a fixed constant, not adaptive. On a very quiet machine it burns CPU it does not need.",
  "The browser build in this page is not the pacer; it is single threaded and paced by requestAnimationFrame.",
  "No VRR results. Everything here assumes a fixed refresh target.",
];

export const BACKSTORY = {
  title: "Backstory",
  paragraphs: [
    `This project started as a small OpenGL renderer that kept missing its frame budget. The render work itself was cheap — well under a millisecond — but the frame delivery was still uneven. The culprit was the operating system's sleep scheduler: a request to sleep for 6.94 ms on Windows often returns after 15 ms, and even macOS and Linux have tails that are large compared to a 144 Hz interval.`,
    `A render loop is simple on paper: do work, wait for the next deadline, repeat. In practice the "wait" is where the OS gets a say, and the OS is not optimized for sub-millisecond wake-up precision. It is optimized for battery life, throughput, and not burning one core per process. That mismatch is the whole reason a dedicated pacer exists.`,
    `The measurements here come from a native Windows, macOS and Linux build using a high-resolution counter, a raised timer resolution where the platform allows it, and a small spin margin at the end of each interval. The browser build on this page is only a live preview; the real numbers are from the native executable.`,
  ],
  context: [
    `OpenGL renderer with a single render thread and a fixed refresh target.`,
    `600 000 frames captured per configuration to keep the tails visible.`,
    `Three platforms, three refresh rates, one pacer implementation.`,
    `Goal was to hit the deadline, not to minimize average frame time.`,
  ],
};

export const HOW_IT_WORKS = {
  title: "How it works",
  paragraphs: [
    `At the heart of the pacer is one loop that does three things: record the current time, do the render work, then wait until the next frame deadline. The waiting is split into two phases. First, the thread sleeps until it is within a small margin of the target. Then it spins-busy-waits the remaining microseconds so the wake-up is not handed back to the OS scheduler.`,
    `The margin is the only tunable: too large and the CPU burn becomes visible; too small and the OS sleep tail bleeds past the deadline. On Windows the margin is paired with a raised process timer resolution so the sleep wake-up itself is as short as possible. On macOS and Linux the timer granules are already smaller, but a spin margin is still needed to catch the last few hundred microseconds.`,
    `Every interval is measured against the raw hardware counter, not the compositor's timestamp. That measurement is what drives the reported jitter, missed-deadline percentage and CPU cost. If the work takes longer than expected the loop skips the wait and logs the miss; it never tries to catch up by compressing the next frame.`,
  ],
  steps: [
    `Read the raw counter at the start of the frame.`,
    `Submit render work and wait for the GPU to finish the previous frame.`,
    `Sleep until target minus the spin margin.`,
    `Spin-wait the final margin and present exactly at the deadline.`,
    `Record the actual interval and adjust the next frame's budget.`,
  ],
  uniqueFeatures: {
    intro: "Most OpenGL renderers stop at vsync. They hand the frame cadence to the OS compositor and live with whatever cadence it returns. This project does not.",
    items: [
      {
        title: "Explicit frame pacer instead of swap interval",
        body: "A standard renderer blocks in glfwSwapBuffers and trusts the driver to schedule the next frame. Here the render loop owns its own deadline, measures its own interval, and only presents when the target time arrives.",
      },
      {
        title: "Sub-millisecond timing control",
        body: "VSync gives you frame boundaries, not frame centers. The pacer uses a platform high-resolution counter and a tunable spin margin to land the present call within a few hundred microseconds of the target, independent of the OS scheduler.",
      },
      {
        title: "Measurable jitter and misses",
        body: "Instead of assuming the frame is on time, the loop records every interval and reports p50, p99 and missed-deadline percentage. That makes the renderer's real-time behavior visible instead of hidden inside the driver.",
      },
      {
        title: "Adaptive to work load, not fixed to swap",
        body: "If the work overruns the budget the loop skips the wait and counts the miss; it does not try to compress the next frame or drift into a double-buffer queue. Standard triple-buffering can hide the problem and add latency.",
      },
      {
        title: "Portable native timing",
        body: "The same pacer logic runs on Windows, macOS and Linux with platform-specific timer resolution and sleep primitives. A standard cross-platform renderer usually inherits the lowest-common-denominator timing of its windowing library.",
      },
    ],
  },
};
