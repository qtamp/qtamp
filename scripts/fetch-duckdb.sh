#!/usr/bin/env bash
# fetch-duckdb.sh — vendor the prebuilt DuckDB C library into deps/duckdb.
#
# The Media Library index (src/medialibraryindex.cpp) stores the scanned
# collection in DuckDB and persists it as Parquet.  DuckDB is not packaged
# on most distros, so we pull the official prebuilt C-API release
# (duckdb.h + libduckdb.so) matching the host platform.  deps/duckdb is
# gitignored; run this once before configuring with CMake.
set -euo pipefail

DUCKDB_VERSION="${DUCKDB_VERSION:-v1.5.4}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEST="$ROOT/deps/duckdb"

case "$(uname -s)" in
    Linux)  os=linux ;;
    Darwin) os=osx ;;
    *) echo "unsupported OS: $(uname -s)" >&2; exit 1 ;;
esac
case "$(uname -m)" in
    x86_64|amd64) arch=amd64 ;;
    aarch64|arm64) arch=arm64 ;;
    *) echo "unsupported arch: $(uname -m)" >&2; exit 1 ;;
esac

if [ "$os" = "osx" ]; then
    asset="libduckdb-osx-universal.zip"       # universal binary covers arm64+amd64
else
    asset="libduckdb-linux-${arch}.zip"
fi
url="https://github.com/duckdb/duckdb/releases/download/${DUCKDB_VERSION}/${asset}"

if [ -f "$DEST/duckdb.h" ] && { [ -f "$DEST/libduckdb.so" ] || [ -f "$DEST/libduckdb.dylib" ]; }; then
    echo "DuckDB already present in $DEST — nothing to do."
    exit 0
fi

mkdir -p "$DEST"
echo "Fetching DuckDB ${DUCKDB_VERSION} (${asset})..."
curl -fsSL -o "$DEST/duckdb.zip" "$url"
( cd "$DEST" && unzip -oq duckdb.zip && rm -f duckdb.zip )
echo "DuckDB vendored into $DEST:"
ls -1 "$DEST"
