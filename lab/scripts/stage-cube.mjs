// Copies the Emscripten cube artifacts from the repo's dist/ into public/.
// dist/ is built by `python web/build.py --out dist` at the repo root (needs an
// activated emsdk). FATAL when absent: a lab build without the cube is incomplete.
import { copyFileSync, existsSync, mkdirSync, statSync } from "node:fs";
import { join, dirname } from "node:path";
import { fileURLToPath } from "node:url";

const HERE = dirname(fileURLToPath(import.meta.url));
const DIST = join(HERE, "..", "..", "dist");
const PUBLIC = join(HERE, "..", "public");

for (const name of ["cube.js", "cube.wasm"]) {
  const src = join(DIST, name);
  if (!existsSync(src) || statSync(src).size === 0) {
    console.error(
      `FATAL: ${src} is missing -- run "python web/build.py --out dist" at the repo root first (requires an activated emsdk; local install at C:\\Users\\tiffm\\emsdk).`,
    );
    process.exit(1);
  }
  mkdirSync(PUBLIC, { recursive: true });
  copyFileSync(src, join(PUBLIC, name));
}

// The architecture diagram is committed once at docs/architecture.png and
// staged here rather than duplicated in git -- same rule as the bench CSVs.
const DIAGRAM = join(HERE, "..", "..", "docs", "architecture.png");
if (!existsSync(DIAGRAM) || statSync(DIAGRAM).size === 0) {
  console.error(
    `FATAL: ${DIAGRAM} is missing -- it is committed at docs/architecture.png (also embedded by the README).`,
  );
  process.exit(1);
}
copyFileSync(DIAGRAM, join(PUBLIC, "architecture.png"));
console.log("staged cube.js + cube.wasm + architecture.png into public/");
