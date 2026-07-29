# Phase 9b — Cloudflare Deployment Swap Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make https://opengl-renderer.pages.dev/ serve the lab page — `pages.yml` builds `lab/` as a static prerender and deploys it, `web/build.py` slims to a cube-wasm builder, and `web/index.html` + `dashboard.js` + vendored Chart.js + `web/_headers` retire — per `docs/superpowers/specs/2026-07-28-frame-pacer-lab-design.md` resolved decision #1 (user approved the local page 2026-07-29).

**Architecture:** The lab builds fully static: `nitro: false` (no worker — Direct Upload serves files) plus TanStack Start's `prerender: {enabled, crawlLinks}`, which emits `lab/dist/client/` = prerendered `index.html` + hashed assets + everything from `lab/public/` (staged cube, new `_headers`, favicon, robots). **Verified locally 2026-07-29:** with nitro's default cloudflare-module preset the prerender FAILS (`Cannot find module lab/dist/server/server.js` — nitro relocates output to `.output/`, TanStack's prerender preview server expects the default layout), and with `nitro: false` it succeeds (`[prerender] Prerendered 1 pages: /`, exit 0, ~90 KB index.html containing the real content). CI ordering in `pages.yml`: emsdk → `web/build.py` (cube into repo-root `dist/`) → node → `npm ci` → `npm run gen-data` (fresh weekly CSVs flow onto the page) → `npm run build` (prebuild stages the cube from `dist/`) → wrangler deploys `lab/dist/client`. The bench.yml → explicit pages.yml dispatch contract is untouched.

**Tech Stack:** GitHub Actions (`actions/setup-node@v4`, existing `mymindstorm/setup-emsdk@v14` + `cloudflare/wrangler-action@v3` pinned 4.114.0), TanStack Start static prerender, Python stdlib (`web/build.py`).

## Global Constraints

- The C++ project, tests, `bench.yml`, `build.yml`, and everything under `bench/` are untouched.
- `web/data/frametime-hist.json` STAYS — it is a `gen-lab-data.mjs` input and `bench/export_hist.py`'s output. Only `web/index.html`, `web/dashboard.js`, `web/vendor/chart.umd.min.js`, `web/_headers` retire.
- Deploy target stays the same Cloudflare project/URL: project `opengl-renderer`, https://opengl-renderer.pages.dev/, wrangler `4.114.0`, `--branch=main --commit-dirty=true`. GH Pages redirect untouched (stays enabled).
- **Do NOT `git push` until every task is committed.** `pages.yml` fires on every push to main; an interim push (build.py slimmed but pages.yml still deploying repo-root `dist/`) would put a broken site live. One final push → one deploy of the finished swap.
- Distinguish the two `dist` directories everywhere: repo-root `dist/` = emcc output (cube.js/cube.wasm, git-ignored), `lab/dist/` = the lab's vite build output (git-ignored via `lab/.gitignore` line `dist`); the deployable site is `lab/dist/client/`.
- Commit messages follow repo style: `type: summary -- detail (Phase 9b)` with the `Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>` trailer.
- Run all commands from the repo root `C:\Users\tiffm\Desktop\OpenGL Renderer\OpenGL-Renderer` unless a step says otherwise. PowerShell syntax.
- Acceptable loss (spec, confirmed by page approval): the lab page tells the tuned-vs-naive story only; the dashboard's full four-strategy histograms/tables (`timer`, `spin`, uncapped) leave the live site and remain in the committed results docs.

## File Structure

```
lab/vite.config.ts                    Modify: nitro:false + prerender
lab/public/_headers                   Create: Cloudflare header rules (moves from web/)
.github/workflows/pages.yml           Rewrite steps: lab build + deploy lab/dist/client
web/build.py                          Rewrite: cube-only builder (staging lists removed)
web/index.html                        Delete
web/dashboard.js                      Delete
web/vendor/chart.umd.min.js           Delete
web/_headers                          Delete (rules move to lab/public/_headers)
README.md                             Modify: intro line + "The browser build" section
CLAUDE.md                             Modify: one Phase 9b paragraph (final task)
```

---

### Task 1: Lab builds fully static (prerender on, nitro off, `_headers` in public)

**Files:**
- Modify: `lab/vite.config.ts`
- Create: `lab/public/_headers`

**Interfaces:**
- Produces: `npm run build` in `lab/` emits `lab/dist/client/` containing `index.html`, `assets/*`, `cube.js`, `cube.wasm`, `_headers`, `favicon.ico`, `robots.txt` — the exact directory Task 3's workflow deploys.

- [ ] **Step 1: Update `lab/vite.config.ts`**

Replace the `export default` block with:

```ts
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
```

(Keep the file's header comment block unchanged.)

- [ ] **Step 2: Create `lab/public/_headers`**

```
# Cloudflare Pages header rules -- parsed by Cloudflare at the deploy root,
# never served as an asset (https://developers.cloudflare.com/pages/configuration/headers/).
# Lives in lab/public/ so vite copies it into dist/client/, the directory
# pages.yml deploys. (Phase 8d kept this in web/_headers; Phase 9b moved it.)

/*
  Cache-Control: public, max-age=3600

/*.wasm
  Content-Type: application/wasm

# Vite emits content-hashed filenames under /assets/ -- cache forever.
# Later rules override earlier ones for the same header name.
/assets/*
  Cache-Control: public, max-age=31536000, immutable

# Threaded-WASM stretch goal: SharedArrayBuffer needs cross-origin isolation
# (COOP + COEP). The build is single-threaded today (no -pthread, no SAB), and
# enabling COEP without need would break future third-party embeds -- so the
# hook ships commented out. Uncomment when a threaded build actually lands.
# /*
#   Cross-Origin-Opener-Policy: same-origin
#   Cross-Origin-Embedder-Policy: require-corp
```

- [ ] **Step 3: Build and verify the static output**

```powershell
cd lab; npm run build; cd ..
```

Expected: exit 0; log shows `[prerender] Prerendered 1 pages:` with `- /`.

```powershell
Get-ChildItem lab/dist/client | Select-Object Name
Select-String -Path lab/dist/client/index.html -Pattern "Windows 11 · Arc" -SimpleMatch -Quiet
```

Expected: `index.html`, `assets`, `cube.js`, `cube.wasm`, `_headers`, `favicon.ico`, `robots.txt`; the Select-String prints `True` (real prerendered content, not an empty shell).

- [ ] **Step 4: Browser-verify the static site (no dev server)**

```powershell
python -m http.server 8123 -d lab/dist/client
```

In the browser at `http://localhost:8123/`: page renders with real numbers, cube boots and spins, platform/Hz chips work, console clean. Stop the server after.

- [ ] **Step 5: Commit**

```powershell
git add lab/vite.config.ts lab/public/_headers
git commit -m @'
feat: lab builds fully static -- prerender on, nitro off, _headers rides in public/ (Phase 9b)

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>
'@
```

---

### Task 2: Slim `web/build.py` to a cube builder; retire the dashboard files

**Files:**
- Rewrite: `web/build.py` (full replacement)
- Delete: `web/index.html`, `web/dashboard.js`, `web/vendor/chart.umd.min.js`, `web/_headers`

**Interfaces:**
- Consumes: nothing new.
- Produces: `python web/build.py --out dist` writes exactly `dist/cube.js` + `dist/cube.wasm` — the contract `lab/scripts/stage-cube.mjs` already depends on (it copies those two names from repo-root `dist/`).

- [ ] **Step 1: Replace `web/build.py` with:**

```python
#!/usr/bin/env python3
"""Cube wasm builder (Phase 8; slimmed in Phase 9b).

Compiles the whole app (src/*.cpp) to WebAssembly with Emscripten and writes
cube.js + cube.wasm into --out. The page that serves them is lab/ -- its
build stages these artifacts via lab/scripts/stage-cube.mjs and deploys a
static prerender (see .github/workflows/pages.yml). The same three
translation units as the native build; __EMSCRIPTEN__ guards select the
single-threaded browser path (no render thread, no pacer in the loop --
requestAnimationFrame owns the frame clock). Stdlib-only, like every runner
in bench/.

Requires an activated emsdk: emcc must be on PATH (run emsdk_env first).
"""
import argparse
import shutil
import subprocess
import sys
from pathlib import Path

# The Phase 8 build flags, verbatim from the spec (only -o moves to --out).
# EXPORTED_RUNTIME_METHODS=ccall is spec-mandated; nothing calls ccall today.
EMCC_FLAGS = [
    "-std=c++20",
    "-O3",
    "-sUSE_GLFW=3",
    "-sMIN_WEBGL_VERSION=2",
    "-sMAX_WEBGL_VERSION=2",
    "-sALLOW_MEMORY_GROWTH=1",
    "-sEXPORTED_RUNTIME_METHODS=ccall",
]


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--out", default="dist",
                    help="output directory for cube.js + cube.wasm (git-ignored)")
    args = ap.parse_args()

    root = Path(__file__).resolve().parent.parent

    # shutil.which honors PATHEXT, so this resolves emcc.bat on Windows.
    emcc = shutil.which("emcc")
    if emcc is None:
        sys.exit("FATAL: emcc not found on PATH -- activate emsdk first "
                 "(emsdk_env.bat on Windows, `source emsdk_env.sh` on POSIX)")

    sources = sorted((root / "src").glob("*.cpp"))
    if not sources:
        sys.exit(f"FATAL: no sources matched {root / 'src'}/*.cpp")

    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    js = out / "cube.js"
    wasm = out / "cube.wasm"

    cmd = ([emcc] + [str(s) for s in sources] + ["-o", str(js)] + EMCC_FLAGS +
           [f"-I{root / 'extern'}", f"-I{root / 'src'}"])
    print(" ".join(cmd), flush=True)
    result = subprocess.run(cmd)
    if result.returncode != 0:
        sys.exit(f"FATAL: emcc exited {result.returncode}")
    for artifact in (js, wasm):
        if not artifact.is_file():
            sys.exit(f"FATAL: emcc exited 0 but {artifact} was not produced")

    print(f"wasm: js_bytes={js.stat().st_size} wasm_bytes={wasm.stat().st_size} "
          f"out={out}", flush=True)


if __name__ == "__main__":
    main()
```

- [ ] **Step 2: Delete the retired files**

```powershell
git rm web/index.html web/dashboard.js web/vendor/chart.umd.min.js web/_headers
```

Expected: `web/` now contains only `build.py` and `data/frametime-hist.json`.

- [ ] **Step 3: Verify the cube build + the lab chain end-to-end**

```powershell
& "C:\Users\tiffm\emsdk\emsdk_env.ps1"; python web/build.py --out dist
```

Expected: `wasm: js_bytes=... wasm_bytes=... out=dist`, exit 0, and `dist/` contains exactly cube.js + cube.wasm (stale staged files from the old build.py may remain locally — `dist/` is git-ignored; optionally `Remove-Item dist -Recurse` first for a clean check).

```powershell
cd lab; node scripts/stage-cube.mjs; npm run gen-data; cd ..
```

Expected: `staged cube.js + cube.wasm into public/` and `wrote ...lab-data.gen.json`; `git status --short` shows no change to `lab/src/lib/lab-data.gen.json` (deterministic).

- [ ] **Step 4: Commit**

```powershell
git add web/build.py
git commit -m @'
refactor: web/build.py slims to a cube-wasm builder -- page/dashboard/vendor/_headers staging retired with the dashboard (Phase 9b)

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>
'@
```

(The `git rm` deletions are already staged and land in this commit.)

---

### Task 3: `pages.yml` builds and deploys the lab

**Files:**
- Modify: `.github/workflows/pages.yml` (steps of the `deploy-cloudflare` job)

**Interfaces:**
- Consumes: Task 1's `lab/dist/client` output contract; Task 2's slimmed `python web/build.py --out dist`.
- Produces: on push to main / dispatch, the live site at https://opengl-renderer.pages.dev/ is the lab page. The `bench.yml → gh workflow run pages.yml` dispatch contract is untouched (name, triggers, permissions, concurrency all unchanged).

- [ ] **Step 1: Replace the job's steps**

Keep `name/on/permissions/concurrency/env` and the job header exactly as they are. Replace the steps of `deploy-cloudflare` with:

```yaml
    steps:
      - uses: actions/checkout@v4
      - uses: mymindstorm/setup-emsdk@v14
        with:
          version: 6.0.4
          actions-cache-folder: emsdk-cache
      - name: Build cube wasm
        run: python web/build.py --out dist
      - uses: actions/setup-node@v4
        with:
          node-version: 22
          cache: npm
          cache-dependency-path: lab/package-lock.json
      - name: Install lab dependencies
        working-directory: lab
        run: npm ci
      - name: Regenerate lab data from committed bench results
        working-directory: lab
        run: npm run gen-data
      - name: Build lab site (static prerender; prebuild stages the cube from dist/)
        working-directory: lab
        run: npm run build
      - name: Preflight -- require Cloudflare secrets
        env:
          HAVE_TOKEN: ${{ secrets.CLOUDFLARE_API_TOKEN != '' }}
          HAVE_ACCOUNT: ${{ secrets.CLOUDFLARE_ACCOUNT_ID != '' }}
        run: |
          fail=0
          if [ "$HAVE_TOKEN" != "true" ]; then
            echo "FATAL: repo secret CLOUDFLARE_API_TOKEN is not set." >&2
            echo "  Create a Custom API token (permission: Cloudflare Pages -- Edit," >&2
            echo "  scoped to your account) at https://dash.cloudflare.com/profile/api-tokens" >&2
            echo "  and add it under repo Settings -> Secrets and variables -> Actions." >&2
            fail=1
          fi
          if [ "$HAVE_ACCOUNT" != "true" ]; then
            echo "FATAL: repo secret CLOUDFLARE_ACCOUNT_ID is not set." >&2
            echo "  Copy the Account ID from the Cloudflare dashboard (Workers & Pages" >&2
            echo "  -> Overview, right sidebar) and add it under repo Settings ->" >&2
            echo "  Secrets and variables -> Actions." >&2
            fail=1
          fi
          exit $fail
      - name: Deploy lab/dist/client to Cloudflare Pages (Direct Upload)
        id: cf
        uses: cloudflare/wrangler-action@v3
        with:
          apiToken: ${{ secrets.CLOUDFLARE_API_TOKEN }}
          accountId: ${{ secrets.CLOUDFLARE_ACCOUNT_ID }}
          wranglerVersion: "4.114.0"
          command: pages deploy lab/dist/client --project-name=${{ env.CF_PAGES_PROJECT }} --branch=main --commit-dirty=true
      - name: Report deployment URL
        run: echo "deployed ${{ steps.cf.outputs.deployment-url }} (production alias ${{ env.CF_PAGES_URL }})"
```

(The preflight block is byte-identical to the current one — only its position among the steps moves.)

- [ ] **Step 2: Syntax-check the YAML**

```powershell
python -c "import yaml,sys; d=yaml.safe_load(open('.github/workflows/pages.yml',encoding='utf-8')); steps=d['jobs']['deploy-cloudflare']['steps']; print('steps:',len(steps)); print('deploy cmd:',[s for s in steps if s.get('id')=='cf'][0]['with']['command'])"
```

Expected: `steps: 10` and the deploy command echoing `pages deploy lab/dist/client ...`. (If PyYAML is missing locally: `pip install pyyaml` — dev-only, or eyeball the indentation against the current committed file.)

- [ ] **Step 3: Commit**

```powershell
git add .github/workflows/pages.yml
git commit -m @'
feat: pages.yml deploys the lab -- npm ci + gen-data + static prerender, wrangler uploads lab/dist/client (Phase 9b)

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>
'@
```

---

### Task 4: README + CLAUDE.md tell the new story

**Files:**
- Modify: `README.md` (intro bold line + "The browser build" section)
- Modify: `CLAUDE.md` (append one Phase 9b passage to the Project paragraph)

- [ ] **Step 1: README intro line**

Replace the paragraph at lines 8–12:

```markdown
**[Live demo + frame pacer lab](https://opengl-renderer.pages.dev/)** — the
cube compiled to WebAssembly as the hook, above an interactive writeup of the
native build's committed benchmark results. The pacer and the threaded render
deliberately do not port; every number on the page is generated at build time
from files committed in this repo, none re-measured in the browser.
```

- [ ] **Step 2: README "The browser build" section**

Replace the three paragraphs currently at lines 315–335 (from "The page below the cube is a static dashboard…" through "…ci-pipeline.md).") with:

```markdown
The page below the cube is the frame pacer lab (`lab/`, a TanStack
Start/React app): backstory, how the pacer works, the tuned-vs-naive
histograms and tables, the input-handoff benchmarks, and a decision log.
It is fully static — `lab/scripts/gen-lab-data.mjs` bakes the committed
bench CSVs/JSONs into the page at build time (FATAL on any missing input),
and `npm run build` prerenders it into `lab/dist/client`:

    cd lab
    npm ci
    npm run gen-data
    npm run build
    python -m http.server 8000 -d dist/client

Deployed automatically to Cloudflare Pages (wrangler Direct Upload of
`lab/dist/client` from `.github/workflows/pages.yml`) on every push to
main, with emsdk pinned to the version recorded there. The original GitHub
Pages URL serves a permanent redirect here.

The lab also carries weekly CI results from windows-latest and
ubuntu-latest (both Mesa llvmpipe software GL), bot-committed under
`bench/results/ci/<platform>/` by `bench.yml`, which then re-dispatches
the deploy so `gen-data` bakes the fresh numbers in. They are honestly
labeled as a different measurement class from the desktop numbers — a
shared virtualized runner with no AC/idle control. The desktop run of
record stays the default view; the CI platforms are a switchable
comparison, not a replacement. Pipeline details:
[bench/results/2026-07-28-ci-pipeline.md](bench/results/2026-07-28-ci-pipeline.md).
```

Note the first code block in that section (lines ~312–313, `python web/build.py --out dist` + `python -m http.server 8000 -d dist`) also changes: the `http.server -d dist` line drops (repo-root `dist/` is no longer a servable site), leaving:

```markdown
    python web/build.py --out dist
```

with its surrounding sentence adjusted to: "Build the wasm cube locally with an activated emsdk (the lab build stages it from `dist/`):"

- [ ] **Step 3: Sweep for stale references**

```powershell
Select-String -Path README.md -Pattern "dashboard|chart\.js|web/index" -AllMatches
```

Expected: no remaining claims that the live page is the Chart.js dashboard (the provenance doc name `2026-07-28-web-dashboard.md` may legitimately remain only if still referenced — after Step 2 it should not be referenced from README).

- [ ] **Step 4: CLAUDE.md Phase 9b passage**

Append to the Project-section paragraph, directly after the Phase 9 sentence ending "…see docs/superpowers/specs/2026-07-28-frame-pacer-lab-design.md.":

```
Phase 9b (2026-07-29): the deployment swap — pages.yml now builds and deploys the lab (emsdk → `python web/build.py --out dist` for the cube → setup-node 22 → `npm ci` → `npm run gen-data` → `npm run build` → wrangler Direct Upload of `lab/dist/client` to the same opengl-renderer project/URL; bench.yml's explicit dispatch contract unchanged, so weekly CI data flows onto the page via gen-data at build time). The lab builds fully static: `nitro: false` in lab/vite.config.ts (no worker; nitro's output relocation also breaks TanStack Start's prerender preview server, which expects dist/server/server.js) plus `prerender: {enabled, crawlLinks}` bake the single route to `lab/dist/client/index.html`; the Cloudflare `_headers` rules moved to `lab/public/_headers` (global max-age=3600, wasm content-type, immutable hashed /assets/, commented COOP/COEP hook). web/build.py slimmed to a cube-only builder (`dist/cube.{js,wasm}`, the stage-cube.mjs contract); web/index.html, web/dashboard.js, and web/vendor/chart.umd.min.js are deleted — the dashboard's four-strategy histograms/tables (timer, spin, uncapped) left the live site with them (accepted at swap time; the committed results docs still carry them). web/data/frametime-hist.json stays (gen-lab-data input, export_hist.py output). GH Pages redirect untouched.
```

- [ ] **Step 5: Commit**

```powershell
git add README.md CLAUDE.md
git commit -m @'
docs: README + CLAUDE.md -- the live page is the lab, dashboard prose retired (Phase 9b)

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>
'@
```

---

### Task 5: Push once, watch the deploy, verify live

- [ ] **Step 1: Full clean re-verify**

```powershell
cd lab; npm run gen-data; npm run build; cd ..
git status --short          # expect: empty (all committed, gen.json unchanged)
```

- [ ] **Step 2: Push (single push = single deploy)**

```powershell
git push
```

- [ ] **Step 3: Watch the pages run via REST (no gh CLI on this machine)**

```powershell
Invoke-RestMethod "https://api.github.com/repos/tiffany-mares/OpenGL-Renderer/actions/workflows/pages.yml/runs?per_page=1" | ForEach-Object { $_.workflow_runs[0] } | Select-Object status, conclusion, html_url
```

Poll until `status=completed`; expect `conclusion=success`. On failure, fetch the run's jobs/logs via the REST API and fix before anything else.

- [ ] **Step 4: Verify the live site**

```powershell
curl.exe -sI https://opengl-renderer.pages.dev/cube.wasm | Select-String "content-type"
curl.exe -s https://opengl-renderer.pages.dev/ | Select-String -Pattern "Windows 11" -Quiet
```

Expected: `content-type: application/wasm`; `True`. Browser pass on https://opengl-renderer.pages.dev/: lab page renders, cube boots, platform/Hz switching works, console clean. Also confirm the old GitHub Pages URL still redirects.

(Cloudflare caches with max-age=3600 — if the old page appears, re-check with a cache-busting query or wait; the deploy URL from the workflow log is uncached.)

- [ ] **Step 5: Update the weekly watch-item memory**

The memory file `watch-first-cron-bench-run.md` describes the Mon 2026-08-03 validation; its "How to apply" now validates the NEW chain: bench.yml bot commit → pages.yml dispatch → lab build (gen-data picks up the fresh CSVs) → Cloudflare deploy shows the new week's provenance dates on the lab page. Update that file's wording accordingly (the check itself is unchanged in spirit).

## Self-Review Notes (completed)

- **Spec coverage (resolved decision #1):** pages.yml gains the lab's Node build with static prerender (T1+T3), wrangler deploys to the same project/URL (T3), gen-data runs in that build for weekly refresh (T3), web/index.html + dashboard.js retire (T2), gated on user approval (given 2026-07-29). Acceptable-loss confirmation recorded in Global Constraints. Docs follow (T4), live verification (T5).
- **Placeholder scan:** all code/config blocks are complete file or step contents; no TBDs.
- **Type consistency:** the only cross-task contracts are paths — `lab/dist/client` (T1→T3,T5), `dist/cube.{js,wasm}` (T2→T3's prebuild via existing stage-cube.mjs) — spelled identically throughout.
- **Failure-mode check:** interim broken-deploy window eliminated by the single-push rule; prerender mechanism pre-verified locally (not speculative); preflight secrets step preserved byte-identical.
