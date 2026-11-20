// SPDX-License-Identifier: GPL-3.0-or-later
// Copy the ES modules reachable from entry points, for QuickJS:
//   node tools/esm_vendor.mjs <compiled dir> <dest dir> <entry.js>...
// - relative imports get their file name ("./a" -> "./a.js", "./d" -> "./d/index.js");
// - Node-only bare imports ("fs", "uf2", ...) point to shims/<name>.js, which the
//   caller provides (functions that throw: never reached by the co-simulation).
import { existsSync, mkdirSync, readFileSync, statSync, writeFileSync } from 'fs';
import { dirname, join, relative, resolve } from 'path';

const [src, dest, ...entries] = process.argv.slice(2);
if (!src || !dest || entries.length === 0) {
  console.error('usage: node esm_vendor.mjs <compiled dir> <dest dir> <entry.js>...');
  process.exit(2);
}

const SPEC = /(\bfrom\s*|\bimport\s*\(?\s*)(['"])([^'"]+)\2/g;
const done = new Set();
const bare = new Set();
const queue = entries.map((e) => resolve(src, e));

function resolveRelative(fromFile, spec) {
  const base = resolve(dirname(fromFile), spec);
  for (const candidate of [base, `${base}.js`, join(base, 'index.js')]) {
    if (existsSync(candidate) && statSync(candidate).isFile()) {
      return candidate;
    }
  }
  throw new Error(`${relative(src, fromFile)}: cannot resolve '${spec}'`);
}

while (queue.length > 0) {
  const file = queue.shift();
  if (done.has(file)) {
    continue;
  }
  done.add(file);
  const rel = relative(src, file);
  const code = readFileSync(file, 'utf8').replace(SPEC, (all, head, quote, spec) => {
    let target;
    if (spec.startsWith('.')) {
      target = resolveRelative(file, spec);
      queue.push(target);
      target = relative(dirname(file), target);
    } else {
      bare.add(spec);
      target = relative(dirname(file), join(src, 'shims', `${spec.replace(/[^A-Za-z0-9_-]/g, '_')}.js`));
    }
    if (!target.startsWith('.')) {
      target = `./${target}`;
    }
    return `${head}${quote}${target}${quote}`;
  });
  mkdirSync(dirname(join(dest, rel)), { recursive: true });
  writeFileSync(join(dest, rel), code);
}
console.log(`${done.size} modules; shims needed: ${[...bare].sort().join(' ') || 'none'}`);
