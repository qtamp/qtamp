# qtamp tests

Framework-free unit tests, registered with CTest.

```sh
cmake -S . -B build -DQTAMP_TESTS=ON
cmake --build build --target wavreader_test
ctest --test-dir build --output-on-failure
```

## What is covered

- **`wavreader_test`** — `src/wavreader.h`, the dependency-free RIFF/WAVE
  PCM16 parser that feeds the WebAssembly player's demo track into the
  audio pipeline (Qt's wasm backend has no `QAudioDecoder`). Covers valid
  mono/stereo, chunk skipping, truncated data, and every rejection path.

## WebAssembly link test

`wasm/test/link-test.mjs` guards against the js/wasm import-table mismatch
that surfaces in a browser as `LinkError: ... function import requires a
callable` (js glue and wasm binary from different builds). It runs after
every wasm build inside the [qtamp-wasm-builder](https://github.com/qtamp/qtamp-wasm-builder)
image; to run it by hand against a `dist` directory:

```sh
node wasm/test/link-test.mjs path/to/build-wasm/dist
```

`wasm/test/cdp-check.mjs` is an optional real-Chromium smoke check over
the DevTools Protocol (real wall-clock time, so timer-driven paths run and
console errors like BigInt/abort surface as in a user's browser). It needs
a Chromium listening on `--remote-debugging-port=9222`.
