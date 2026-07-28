# Phase 8d: Hosting — Cloudflare Pages Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Move the live site to Cloudflare Pages at the free `opengl-renderer.pages.dev` subdomain (HTTPS included, no domain/cert to manage), with a `web/_headers` file carrying the wasm content-type + cache rules and the commented COOP/COEP threaded-WASM hook, while GitHub Pages retires behind a permanent redirect.

**Architecture:** Direct Upload, not a git-connected Cloudflare build: the servable site is git-ignored `dist/`, built in CI by the pinned emsdk — so the existing `pages.yml` keeps building exactly as today and its deploy step becomes `wrangler pages deploy dist` (cloudflare/wrangler-action@v3, wrangler pinned 4.114.0). This supersedes the spec's "connect the repo in the dashboard / output dir `web/`" letter via its own escape hatch ("if the WASM build happens in CI… treat it as a static deploy"), and it dissolves the spec's ordering worry outright: bench.yml already pushes CSVs first and then explicitly dispatches pages.yml, whose fresh checkout sees the new data — that contract survives byte-for-byte. A transition run deploys the real site to Cloudflare and ships a redirect page as the final GitHub Pages deployment; the next commit removes the GH Pages job.

**Tech Stack:** Cloudflare Pages (Direct Upload, `_headers`), cloudflare/wrangler-action@v3 + wrangler 4.114.0 (pinned), existing GH Actions build (emsdk 6.0.4), GitHub REST API for local verification (no gh CLI on this machine; the `gh` calls inside workflows run on GitHub's runners).

## Context

Phases 0–8c shipped the renderer, the writeup, the live dashboard, and the weekly CI benchmark pipeline. 8d is the hosting move the spec motivates: `*.pages.dev` with `_headers` control — the hook that GitHub Pages can never offer (custom headers, and someday COOP/COEP for threaded WASM). User decisions (AskUserQuestion timed out; recommended defaults locked): **redirect-then-retire** for GitHub Pages; project name **`opengl-renderer`** (fallback `opengl-renderer-cube` if globally taken — the name/URL live in two workflow `env:` values, a two-line switch).

**Verified live during design (2026-07-28):** wrangler-action current major v3 (inputs apiToken/accountId/command/wranglerVersion; `deployment-url` output); `wrangler pages deploy DIR --project-name --branch --commit-dirty` and `pages project create NAME --production-branch`; wrangler 4.114.0 latest on npm (the pin); `_headers` is parsed at the deploy root and never served; Direct Upload limits 20,000 files / 25 MiB per file (dist is ~15 files, wasm ≪1 MiB). Cutoff-knowledge, designed fail-closed: non-interactive `pages deploy` can't answer wrangler's create-project prompt (hence the explicit idempotent create step); the already-exists grep FATALs on unrecognized errors rather than swallowing them; GH Pages serves `max-age=600` (redirect propagation up to ~10 min).

## Global Constraints

- **The bench.yml dispatch contract is untouchable:** workflow file name `pages.yml`, `name: pages`, `workflow_dispatch` trigger, and the `pages` concurrency group all survive both workflow versions — `gh workflow run pages.yml --ref main` in bench.yml needs zero edits (verify by running that path once in T2).
- **Secrets gate (user action; the agent never handles the token value):** repo secrets `CLOUDFLARE_API_TOKEN` (Custom API token, permission "Cloudflare Pages — Edit", account-scoped, from dash.cloudflare.com/profile/api-tokens) and `CLOUDFLARE_ACCOUNT_ID` (dashboard → Workers & Pages → Overview, right sidebar), added by the user in GitHub → Settings → Secrets and variables → Actions. The agent confirms existence BY NAME ONLY via REST (`GET /repos/tiffany-mares/OpenGL-Renderer/actions/secrets` with the git-credential PAT). The workflow's preflight step FATALs with full setup instructions when either is absent — that failure is the designed pause point.
- **Pins:** `cloudflare/wrangler-action@v3`, `wranglerVersion: "4.114.0"`; deploy flags exactly `pages deploy dist --project-name=$CF_PAGES_PROJECT --branch=main --commit-dirty=true` (`--branch` because checkout's detached HEAD hides the branch; `--commit-dirty` because the emsdk cache folder dirties the tree).
- **`web/_headers` content is the spec's** (Cache-Control public max-age=3600 on `/*`; Content-Type application/wasm on `/*.wasm`) **plus the COOP/COEP block COMMENTED OUT** (single-threaded build today; enabling COEP without need risks future embeds — the hook ships, disabled). Staged into dist by build.py (the FATAL-guarded list becomes 7 items; `staged=7`).
- **Required post-deploy check (spec):** `curl -sI https://opengl-renderer.pages.dev/cube.wasm` → `content-type: application/wasm` (+ our `cache-control: public, max-age=3600`); also confirm `/_headers` itself 404s (parsed, not published).
- **Redirect page** ships as BOTH `index.html` and `404.html` (deep links redirect too), containing meta refresh 0 + `rel=canonical` + visible link + `location.replace`. The redirect deploy's artifact is a fresh `redirect/` dir containing ONLY those two files — never dist (so `_headers` can't leak onto GH Pages as a text file).
- **GitHub Pages must stay ENABLED in repo settings forever** — that is what keeps the redirect serving. The retirement is the workflow-job removal only.
- Docs: every live-doc `tiffany-mares.github.io/OpenGL-Renderer` mention swaps to the new URL (README:8 + README browser-build paragraph, web-dashboard.md:4, ci-pipeline.md Pipeline bullet, CLAUDE.md Phase 8 record) — historical plan copies under docs/superpowers/plans/ and .superpowers/ stay untouched. CLAUDE.md gains the Phase 8d record.
- Commit style: `feat:`/`docs:`/`fix:` with ` -- ` sub-clauses. Schedule the transition outside a bench window (cron is Mondays 09:00 UTC).

## File map

- Create: `web/_headers`, `docs/superpowers/plans/2026-07-28-phase-8d-hosting.md` (plan copy, T1).
- Modify: `web/build.py` (one stage-list line + docstring touch), `.github/workflows/pages.yml` (transition version in T2, steady-state version in T3), `README.md` + `bench/results/2026-07-28-web-dashboard.md` + `bench/results/2026-07-28-ci-pipeline.md` + `CLAUDE.md` (T4).

---

### Task 1: `_headers` + staging + plan copy

**Files:**
- Create: `web/_headers`, `docs/superpowers/plans/2026-07-28-phase-8d-hosting.md` (copy this plan verbatim)
- Modify: `web/build.py`

**Interfaces:**
- Produces: `dist/_headers` staged by build.py (`staged=7`); the file T2's deploy publishes to Cloudflare.

- [ ] **Step 1: Create `web/_headers`:**

```
# Cloudflare Pages header rules -- parsed by Cloudflare at the deploy root,
# never served as an asset (https://developers.cloudflare.com/pages/configuration/headers/).
# Staged into dist/ by web/build.py (FATAL-guarded, item 7 of the stage list).
# Cloudflare-only semantics: GitHub Pages would have served this as plain text,
# which is one more reason the final GH Pages redirect deploy contains ONLY the
# redirect page, never a dist build.

/*
  Cache-Control: public, max-age=3600

/*.wasm
  Content-Type: application/wasm

# Threaded-WASM stretch goal: SharedArrayBuffer needs cross-origin isolation
# (COOP + COEP). The build is single-threaded today (no -pthread, no SAB), and
# enabling COEP without need would break future third-party embeds -- so the
# hook ships commented out. Uncomment when a threaded build actually lands.
# /*
#   Cross-Origin-Opener-Policy: same-origin
#   Cross-Origin-Embedder-Policy: require-corp
```

- [ ] **Step 2: `web/build.py`** — append one entry to the FATAL-guarded `stage` list (after the handoff CSV entry):

```python
        # Phase 8d: Cloudflare Pages header rules (cache + wasm content-type;
        # commented COOP/COEP hook). Parsed by Cloudflare, never served.
        (root / "web" / "_headers", out / "_headers"),
```

and extend the module docstring's staging sentence with `, plus the Cloudflare _headers rules`.

- [ ] **Step 3: Local verify:** emsdk-activated PowerShell (`C:\Users\tiffm\emsdk\emsdk_env.ps1` then) `python web/build.py --out dist` → the `wasm:` line shows `staged=7`; `dist/_headers` byte-identical to `web/_headers`; a quick `python -m http.server 8000 -d dist` load still renders (the file is inert off-Cloudflare).

- [ ] **Step 4: Copy this plan** to `docs/superpowers/plans/2026-07-28-phase-8d-hosting.md`, then **commit (do not push yet — T1+T2 push together so `_headers` never sits on GH Pages as text):**

```bash
git add web/_headers web/build.py docs/superpowers/plans/2026-07-28-phase-8d-hosting.md
git commit -m "feat: stage web/_headers into dist -- cache + wasm content-type rules, commented COOP/COEP threaded-wasm hook (Phase 8d)"
```

---

### Task 2: Secrets gate + transition deploy + full verification

**Files:**
- Modify: `.github/workflows/pages.yml` (full replacement — TRANSITION version)

**Interfaces:**
- Consumes: repo secrets `CLOUDFLARE_API_TOKEN` + `CLOUDFLARE_ACCOUNT_ID` (user-created); `dist/` incl. `_headers` from T1.
- Produces: the live Cloudflare site + the GH Pages redirect; the actual project URL for T4's docs.

- [ ] **Step 1 — USER GATE (execution pauses here until satisfied).** Surface these instructions to the user and wait:
  1. Create a free Cloudflare account at https://dash.cloudflare.com/sign-up if you don't have one.
  2. Copy your **Account ID**: dashboard → Workers & Pages → Overview → right sidebar.
  3. Create an API token at https://dash.cloudflare.com/profile/api-tokens → Create Token → Custom token → Permissions: **Account → Cloudflare Pages → Edit** → Account Resources: your account → Create, and copy the token once.
  4. Add BOTH as repo secrets at https://github.com/tiffany-mares/OpenGL-Renderer/settings/secrets/actions → New repository secret: name `CLOUDFLARE_API_TOKEN` (the token) and name `CLOUDFLARE_ACCOUNT_ID` (the account id).

  The agent then confirms existence BY NAME ONLY: `GET https://api.github.com/repos/tiffany-mares/OpenGL-Renderer/actions/secrets` (Bearer = git-credential PAT) lists both names. Never ask for or handle the token value.

- [ ] **Step 2: Write the TRANSITION `.github/workflows/pages.yml`** (full replacement):

```yaml
name: pages
on:
  push:
    branches: [main]
  workflow_dispatch:

permissions:
  contents: read

concurrency:
  group: pages
  cancel-in-progress: false

env:
  # Single edit point if the pages.dev name is taken: fallback is
  # opengl-renderer-cube (both values must change together).
  CF_PAGES_PROJECT: opengl-renderer
  CF_PAGES_URL: https://opengl-renderer.pages.dev

jobs:
  deploy-cloudflare:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - uses: mymindstorm/setup-emsdk@v14
        with:
          version: 6.0.4
          actions-cache-folder: emsdk-cache
      - name: Build wasm site
        run: python web/build.py --out dist
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
      - name: Ensure the Pages project exists (idempotent)
        # Non-interactive `pages deploy` cannot answer wrangler's create-project
        # prompt, so create explicitly and tolerate only already-exists.
        env:
          CLOUDFLARE_API_TOKEN: ${{ secrets.CLOUDFLARE_API_TOKEN }}
          CLOUDFLARE_ACCOUNT_ID: ${{ secrets.CLOUDFLARE_ACCOUNT_ID }}
        run: |
          out=$(npx wrangler@4.114.0 pages project create "$CF_PAGES_PROJECT" \
                --production-branch=main 2>&1) && { echo "$out"; exit 0; }
          echo "$out"
          if echo "$out" | grep -qi 'already exists'; then
            echo "project $CF_PAGES_PROJECT exists; continuing"
            exit 0
          fi
          echo "FATAL: could not create Pages project $CF_PAGES_PROJECT" >&2
          echo "  (if the name is taken globally, switch CF_PAGES_PROJECT/CF_PAGES_URL" >&2
          echo "  at the top of this workflow to the opengl-renderer-cube fallback)" >&2
          exit 1
      - name: Deploy dist to Cloudflare Pages (Direct Upload)
        id: cf
        uses: cloudflare/wrangler-action@v3
        with:
          apiToken: ${{ secrets.CLOUDFLARE_API_TOKEN }}
          accountId: ${{ secrets.CLOUDFLARE_ACCOUNT_ID }}
          wranglerVersion: "4.114.0"
          # --branch=main: actions/checkout leaves a detached HEAD, so wrangler
          # cannot infer the branch; main = production branch = production deploy.
          # --commit-dirty=true: the emsdk actions-cache folder dirties the tree.
          command: pages deploy dist --project-name=${{ env.CF_PAGES_PROJECT }} --branch=main --commit-dirty=true
      - name: Report deployment URL
        run: echo "deployed ${{ steps.cf.outputs.deployment-url }} (production alias ${{ env.CF_PAGES_URL }})"

  # TRANSITION ONLY -- removed by the Phase 8d steady-state commit. Ships the
  # final GitHub Pages deployment: a redirect page (index.html + 404.html, so
  # deep links redirect too) and nothing else. Runs only after Cloudflare is
  # live, so the old URL never redirects into a void. Re-running this job on
  # later triggers before the steady-state commit lands is idempotent (it
  # re-deploys the same redirect).
  redirect-github-pages:
    needs: deploy-cloudflare
    runs-on: ubuntu-latest
    permissions:
      pages: write
      id-token: write
    environment:
      name: github-pages
      url: ${{ steps.deployment.outputs.page_url }}
    steps:
      - name: Write the redirect page
        run: |
          mkdir redirect
          cat > redirect/index.html <<'EOF'
          <!doctype html>
          <html lang="en">
          <head>
            <meta charset="utf-8">
            <title>OpenGL-Renderer has moved</title>
            <link rel="canonical" href="https://opengl-renderer.pages.dev/">
            <meta http-equiv="refresh" content="0; url=https://opengl-renderer.pages.dev/">
            <script>location.replace("https://opengl-renderer.pages.dev/");</script>
          </head>
          <body>
            <p>This site has moved to
               <a href="https://opengl-renderer.pages.dev/">opengl-renderer.pages.dev</a>.</p>
          </body>
          </html>
          EOF
          cp redirect/index.html redirect/404.html
      - uses: actions/configure-pages@v5
        with:
          enablement: true
      - uses: actions/upload-pages-artifact@v3
        with:
          path: redirect
      - id: deployment
        uses: actions/deploy-pages@v4
```

- [ ] **Step 3: YAML-check, commit, push** (T1's commit rides along):

```bash
python -c "import yaml; yaml.safe_load(open('.github/workflows/pages.yml'))"
git add .github/workflows/pages.yml
git commit -m "feat: deploy to Cloudflare Pages by wrangler Direct Upload -- transition run ships the GitHub Pages redirect (Phase 8d)"
git push
```

The push triggers the transition run.

- [ ] **Step 4: Watch the run** (REST poll): preflight passes, project-create prints created (or exists), deploy prints the `deployment-url`, the redirect job deploys after. If project-create FATALs name-taken: edit the two `env:` values + the three heredoc URLs to `opengl-renderer-cube`, commit `fix: pages.dev project name fallback -- opengl-renderer is taken`, push, re-watch; record the actual URL for T4.

- [ ] **Step 5: Full verification** (controller: curl + Chrome):
  1. New URL `https://opengl-renderer.pages.dev/`: the 8b/8c browser checklist — cube renders/spins, keys work, console clean, dashboard renders, 3 platforms in the select, `data/*` fetches 200.
  2. `curl -sI https://opengl-renderer.pages.dev/cube.wasm` → `content-type: application/wasm` AND `cache-control: public, max-age=3600` (the spec's required header check).
  3. `curl -s -o /dev/null -w "%{http_code}" https://opengl-renderer.pages.dev/_headers` → 404 (parsed, not published).
  4. Old URL redirect: `curl -s https://tiffany-mares.github.io/OpenGL-Renderer/?cachebust=1` shows the redirect HTML; a deep path (`/OpenGL-Renderer/cube.wasm`) serves the 404-page redirect; a browser load of the old URL lands on the new one. Allow ~10 min for GH's max-age=600 caches.
  5. Dispatch contract: fire bench.yml's exact path once — REST `POST /repos/tiffany-mares/OpenGL-Renderer/actions/workflows/pages.yml/dispatches {"ref":"main"}` — and confirm a run starts and the Cloudflare deployment timestamp advances (the 8c contract intact).

---

### Task 3: Steady-state cleanup

**Files:**
- Modify: `.github/workflows/pages.yml` (full replacement — STEADY-STATE version)

- [ ] **Step 1: Replace pages.yml** with the steady-state version — the transition file minus the entire `redirect-github-pages` job and minus the `Ensure the Pages project exists` step (the project now exists; a future deletion fails loudly at deploy):

```yaml
name: pages
on:
  push:
    branches: [main]
  workflow_dispatch:

permissions:
  contents: read

concurrency:
  group: pages
  cancel-in-progress: false

env:
  CF_PAGES_PROJECT: opengl-renderer
  CF_PAGES_URL: https://opengl-renderer.pages.dev

jobs:
  deploy-cloudflare:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - uses: mymindstorm/setup-emsdk@v14
        with:
          version: 6.0.4
          actions-cache-folder: emsdk-cache
      - name: Build wasm site
        run: python web/build.py --out dist
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
      - name: Deploy dist to Cloudflare Pages (Direct Upload)
        id: cf
        uses: cloudflare/wrangler-action@v3
        with:
          apiToken: ${{ secrets.CLOUDFLARE_API_TOKEN }}
          accountId: ${{ secrets.CLOUDFLARE_ACCOUNT_ID }}
          wranglerVersion: "4.114.0"
          command: pages deploy dist --project-name=${{ env.CF_PAGES_PROJECT }} --branch=main --commit-dirty=true
      - name: Report deployment URL
        run: echo "deployed ${{ steps.cf.outputs.deployment-url }} (production alias ${{ env.CF_PAGES_URL }})"
```

(If T2 fell back to `opengl-renderer-cube`, carry the same two `env:` values here.)

- [ ] **Step 2: YAML-check, commit, push, verify:**

```bash
python -c "import yaml; yaml.safe_load(open('.github/workflows/pages.yml'))"
git add .github/workflows/pages.yml
git commit -m "feat: retire the GitHub Pages deploy -- Cloudflare is the single live host, the redirect stays served (Phase 8d)"
git push
```

Watch the run: a single `deploy-cloudflare` job; Cloudflare deployment advances; the old URL STILL serves the redirect (the final GH Pages deployment persists as long as Pages stays enabled — do NOT disable Pages in repo settings, ever).

---

### Task 4: The record

**Files:**
- Modify: `README.md`, `bench/results/2026-07-28-web-dashboard.md`, `bench/results/2026-07-28-ci-pipeline.md`, `CLAUDE.md`

- [ ] **Step 1: URL swaps** (live docs only — historical plan copies under docs/superpowers/plans/ and .superpowers/ untouched; use the ACTUAL URL from T2 if the fallback fired):
  1. `README.md` live-demo block: link → `https://opengl-renderer.pages.dev/`.
  2. `README.md` "The browser build" deploy sentence → `Deployed automatically to Cloudflare Pages (wrangler Direct Upload from .github/workflows/pages.yml) on every push to main, with emsdk pinned to the version recorded there. The original GitHub Pages URL serves a permanent redirect here.`
  3. `bench/results/2026-07-28-web-dashboard.md` Page bullet → `- **Page:** https://opengl-renderer.pages.dev/ (moved from GitHub Pages in Phase 8d; the old URL redirects) — Phase 8b: wasm cube as hook, static dashboard below`
  4. `bench/results/2026-07-28-ci-pipeline.md` Pipeline bullet — append: `Since Phase 8d the dispatched pages.yml deploys dist/ to Cloudflare Pages by wrangler Direct Upload instead of a GitHub Pages artifact; the dispatch contract is unchanged.` (The GITHUB_TOKEN-gotcha paragraph stays — still true and load-bearing.)
  5. `CLAUDE.md` Phase 8 record: `https://tiffany-mares.github.io/OpenGL-Renderer/ serves` → `https://opengl-renderer.pages.dev/ (GitHub Pages originally; hosting migrated in Phase 8d, old URL redirects) serves`.

- [ ] **Step 2: CLAUDE.md Phase 8d record** — append to the line-7 phase paragraph:

```
Phase 8d (complete 2026-07-28): hosting migration — the site moved to Cloudflare Pages at https://opengl-renderer.pages.dev/ via Direct Upload from the existing pages.yml (`cloudflare/wrangler-action@v3`, wrangler pinned 4.114.0, `pages deploy dist --project-name=opengl-renderer --branch=main --commit-dirty=true`; `--branch` because checkout's detached HEAD hides the branch, `--commit-dirty` because the emsdk cache folder dirties the tree). A git-connected Cloudflare build was rejected: the servable site is git-ignored `dist/` built in CI with the pinned emsdk, and Direct Upload preserves the 8c ordering guarantee — bench.yml pushes CSVs, then dispatches pages.yml, whose checkout sees the new data; the `gh workflow run pages.yml --ref main` contract is untouched. Secrets: `CLOUDFLARE_API_TOKEN` (Custom token, "Cloudflare Pages — Edit", account-scoped) + `CLOUDFLARE_ACCOUNT_ID` as GitHub repo secrets, guarded by a preflight step that FATALs with setup instructions when absent. `web/_headers` (staged by build.py — the FATAL list is 7 items, `staged=7`) sets `Cache-Control: public, max-age=3600` on `/*`, pins `Content-Type: application/wasm` on `/*.wasm` (verified live via `curl -sI .../cube.wasm` — required so streaming instantiation never silently falls back), and carries a commented-out COOP/COEP block as the threaded-WASM stretch-goal hook — cross-origin isolation deliberately off today. GitHub Pages retired by redirect: the transition run deployed the real site to Cloudflare and then a minimal redirect page (meta refresh 0 + canonical + location.replace; shipped as both index.html and 404.html so deep links redirect too) as the final GH Pages deployment; the next commit removed the GH Pages job. GH Pages must stay ENABLED in repo settings — that is what keeps the redirect serving. Direct Upload limits (20,000 files / 25 MiB per file) are far above dist's ~15 files.
```

(Adjust the URL/project name if the fallback fired.)

- [ ] **Step 3: Commit, final review, push, verify:**

```bash
git add README.md bench/results/2026-07-28-web-dashboard.md bench/results/2026-07-28-ci-pipeline.md CLAUDE.md
git commit -m "docs: hosting migration record -- live URL swap in README/results docs + CLAUDE.md Phase 8d"
```

Final whole-branch review per the SDD skill before the push; then push, confirm `build` + `pages` green, and confirm the live Cloudflare site updated once more.

---

## Verification (end-to-end)

1. T1: `staged=7`; `dist/_headers` present; local preview unaffected.
2. T2: transition run green (preflight → create → deploy → redirect); new URL passes the full browser checklist; `cube.wasm` serves `application/wasm` + `max-age=3600`; `/_headers` 404s; old URL (and a deep path) redirects; a manual pages.yml dispatch proves the bench contract.
3. T3: single-job run; Cloudflare advances; old URL still redirects.
4. T4: every live-doc URL swapped; CLAUDE.md records 8d; final review clean; CI green at the last head.

## Risks (watch during execution)

- Name collision → two-value env fallback (`opengl-renderer-cube`) + heredoc URLs; T2 records reality.
- Already-exists grep is fail-closed: unknown create errors FATAL loudly, never silently pass.
- GH Pages redirect propagation: max-age=600 → up to ~10 min of stale full-site at the old URL.
- bench.yml racing the transition: `pages` concurrency serializes; cron is Mondays — clear today.
- Secrets absent: the preflight FATAL is the designed pause; instructions are in the log and in this plan's Task 2 Step 1.
- Disabling GH Pages in settings would kill the redirect — never do it.

## Self-review notes

- Spec coverage: *.pages.dev free hosting (project create + deploy), "build in CI → static deploy" (Direct Upload of dist; the git-connect/`web/` letter superseded, divergence stated in Context), wasm Content-Type checked once live (T2 Step 5.2) and pinned in `_headers` forever, `_headers` added now with the exact spec rules + the commented COOP/COEP hook, the ordering note (already solved by 8c's commit-then-dispatch; preserved byte-for-byte and re-proven in T2 Step 5.5).
- Type consistency: `CF_PAGES_PROJECT`/`CF_PAGES_URL` identical across both workflow versions; secret names identical across preflight, wrangler-action inputs, plan instructions, and CLAUDE.md; the redirect URL appears in exactly three heredoc places + the env, all listed in the fallback edit note.
- Deliberate choices: redirect-then-retire (user default), COOP/COEP commented (spec's "hook you will need later", not needed now), Pages stays enabled forever, no gh CLI locally (REST + Chrome for all local verification).
