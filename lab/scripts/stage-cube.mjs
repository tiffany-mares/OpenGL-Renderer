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
console.log("staged cube.js + cube.wasm into public/");
