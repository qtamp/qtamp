#!/usr/bin/env bash
#
# Build the QTAMP_WASM_REMOTE head inside the qtamp-wasm-builder
# container (run with the qtamp checkout mounted at /src):
#   docker run --rm -v ~/qtamp-build/qtamp:/src \
#       --entrypoint bash qtamp-wasm-builder /src/scripts/build-wasm-remote.sh
# Emits /src/build-wasm-remote/dist (index.html + qtamp.js/.wasm +
# qtloader.js) and hard-gates the binary under Cloudflare Pages' 25 MiB
# per-file limit.  Separate build dir — the hero build's cache in
# build-wasm/ stays untouched.
set -euo pipefail

SRC="${SRC:-/src}"
BUILD_DIR="${SRC}/build-wasm-remote"
DIST="${BUILD_DIR}/dist"

source /opt/emsdk/emsdk_env.sh
echo "==> Qt wasm: ${QT_WASM}"
echo "==> emcc: $(emcc --version | head -1)"

if [ ! -d "${SRC}/deps/qtWasabi/wasabi-src/Src/Wasabi" ]; then
    echo "==> fetching Wasabi source (archive.org, WCL v1.0, user-supplied)"
    ( cd "${SRC}/deps/qtWasabi" && ./scripts/fetch-wasabi.sh )
fi

mkdir -p "${BUILD_DIR}"
echo "==> configuring (QTAMP_WASM_REMOTE=ON)"
"${QT_WASM}/bin/qt-cmake" -S "${SRC}" -B "${BUILD_DIR}" -G Ninja \
    -DCMAKE_BUILD_TYPE=MinSizeRel \
    -DQT_HOST_PATH="${QT_HOST}" \
    -DQTAMP_USE_QTWASABI=ON \
    -DQTAMP_WASM_REMOTE=ON

echo "==> building"
cmake --build "${BUILD_DIR}" -j"$(nproc)"

echo "==> shrinking with wasm-opt"
/opt/emsdk/upstream/bin/wasm-opt -Oz --all-features \
    "${BUILD_DIR}/qtamp.wasm" -o "${BUILD_DIR}/qtamp.wasm.opt" \
    && mv "${BUILD_DIR}/qtamp.wasm.opt" "${BUILD_DIR}/qtamp.wasm"

# Cloudflare Pages rejects files over 25 MiB — fail loudly, not at deploy.
size=$(stat -c%s "${BUILD_DIR}/qtamp.wasm")
limit=$((25 * 1024 * 1024))
echo "==> qtamp.wasm: ${size} bytes ($((size / 1024 / 1024)) MiB)"
if [ "${size}" -ge "${limit}" ]; then
    echo "FAIL: qtamp.wasm exceeds the 25 MiB Pages per-file limit" >&2
    exit 1
fi

echo "==> collecting dist"
rm -rf "${DIST}"; mkdir -p "${DIST}"
for f in qtamp.js qtamp.wasm qtloader.js; do
    cp "${BUILD_DIR}/${f}" "${DIST}/"
done
# The remote head ships its own host page (query-param sizing).
cp "${SRC}/wasm/remote/index.html" "${DIST}/index.html"

echo "==> smoke: js/wasm link test"
if command -v node >/dev/null 2>&1 && [ -f "${SRC}/wasm/test/link-test.mjs" ]; then
    node "${SRC}/wasm/test/link-test.mjs" "${DIST}" || {
        echo "link-test FAILED: js glue and wasm binary do not match" >&2
        exit 1
    }
fi

echo "==> dist:"; ls -la "${DIST}"
