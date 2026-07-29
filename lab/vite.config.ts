// @lovable.dev/vite-tanstack-config already includes the following — do NOT add them manually
// or the app will break with duplicate plugins:
//   - TanStack devtools (dev-only, first), tanstackStart, viteReact, tailwindcss, tsConfigPaths,
//     nitro (build-only using cloudflare as a default target), VITE_* env injection, @ path alias,
//     React/TanStack dedupe, error logger plugins, and sandbox detection (port/host/strictPort).
// You can pass additional config via defineConfig({ vite: { ... }, etc... }) if needed.
import { defineConfig } from "@lovable.dev/vite-tanstack-config";

export default defineConfig({
  // No server runtime: the deploy is wrangler Direct Upload of static files
  // (pages.yml deploys dist/client). nitro's cloudflare-module worker is
  // unnecessary, and its output relocation breaks TanStack Start's prerender
  // preview server (it expects dist/server/server.js).
  nitro: false,
  tanstackStart: {
    // Redirect TanStack Start's bundled server entry to src/server.ts (our SSR error wrapper).
    // The prerender pass builds from this
    server: { entry: "server" },
    // Static prerender: the single route bakes to dist/client/index.html.
    prerender: { enabled: true, crawlLinks: true },
  },
});
