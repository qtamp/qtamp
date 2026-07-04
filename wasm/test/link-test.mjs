// link-test.mjs — deterministic guard against the js/wasm import mismatch
// that shows up in a browser as
//   LinkError: WebAssembly.instantiate(): Import #N "a" "xy":
//              function import requires a callable
//
// It happens when the Emscripten js glue and the wasm binary come from
// different builds (their import tables disagree). We reproduce the exact
// check the browser's WebAssembly loader does, with no browser: compile
// the wasm, list every function import it requires, load the js glue, and
// assert the glue's import object provides a callable for each one.
//
// Usage: node link-test.mjs <dir with qtamp.js + qtamp.wasm>
// Exit 0 = js and wasm link; non-zero = mismatch (the user-visible bug).

import { readFileSync } from 'node:fs';
import { pathToFileURL } from 'node:url';
import path from 'node:path';

const dir = process.argv[2] || '.';
const wasmPath = path.join(dir, 'qtamp.wasm');
const jsPath = path.join(dir, 'qtamp.js');

const wasmBytes = readFileSync(wasmPath);
const mod = await WebAssembly.compile(wasmBytes);
const needed = WebAssembly.Module.imports(mod)
  .filter((i) => i.kind === 'function');
console.log(`wasm requires ${needed.length} function imports`);

// Drive the Emscripten module factory far enough to build its import
// object, then intercept instantiation to compare against `needed`.
// The factory aborts later (no DOM/WebGL in node); we only care that
// instantiation itself does not throw a LinkError.
let linkError = null;
let instantiated = false;

const origInstantiate = WebAssembly.instantiate;
WebAssembly.instantiate = async function (bytesOrModule, importObject) {
  try {
    const env = importObject?.a || importObject?.env || {};
    const missing = [];
    for (const imp of needed) {
      const table = importObject?.[imp.module] || env;
      if (typeof table?.[imp.name] !== 'function') missing.push(`${imp.module}.${imp.name}`);
    }
    if (missing.length) {
      linkError = `js glue does not provide a callable for ${missing.length} import(s): ` +
                  missing.slice(0, 8).join(', ');
    } else {
      instantiated = true;
    }
  } catch (e) {
    linkError = 'import-object inspection threw: ' + e;
  }
  // Hand back to the real loader so the factory can proceed/abort normally.
  return origInstantiate.call(this, bytesOrModule, importObject);
};

const factory = (await import(pathToFileURL(jsPath).href)).default;

try {
  await factory({
    // Silence the runtime; we only need it to reach instantiation.
    print: () => {}, printErr: () => {},
    wasmBinary: wasmBytes,
    locateFile: (f) => path.join(dir, f),
  });
} catch (e) {
  const msg = String(e && e.message ? e.message : e);
  // A LinkError (or "requires a callable") is the real failure. Anything
  // else (no document, no canvas, WebGL, abort after start) means the
  // link succeeded, which is all this test asserts.
  if (/LinkError|requires a callable|WebAssembly\.instantiate/i.test(msg)) {
    linkError = linkError || msg;
  }
}

if (linkError) {
  console.log('FAIL: ' + linkError);
  process.exit(1);
}
if (!instantiated) {
  console.log('FAIL: wasm never reached instantiation (js glue mismatch?)');
  process.exit(1);
}
console.log('PASS: js glue and wasm link cleanly (all function imports satisfied)');
process.exit(0);
