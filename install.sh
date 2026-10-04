#!/usr/bin/env bash
set -euo pipefail

REPO="DarshanAguru/runDB_native"
LATEST_TAG=$(curl -s "https://api.github.com/repos/${REPO}/releases/latest" | grep '"tag_name":' | sed -E 's/.*"([^"]+)".*/\1/' || echo "v1.0.0")
ARCH=$(uname -m)

if [ "$ARCH" != "x86_64" ]; then
    echo "❌ Currently only x86_64 Linux pre-compiled binaries are supported by this quick installer."
    exit 1
fi

TARBALL="rundb-${LATEST_TAG#v}-linux-${ARCH}.tar.gz"
URL="https://github.com/${REPO}/releases/download/${LATEST_TAG}/${TARBALL}"

echo "⬇️ Downloading RunDB Native ${LATEST_TAG}..."
TMP_DIR=$(mktemp -d)
if ! curl -fsSL "$URL" -o "${TMP_DIR}/${TARBALL}"; then
    echo "❌ Failed to download release asset from ${URL}."
    echo "Please check available releases at: https://github.com/${REPO}/releases"
    rm -rf "$TMP_DIR"
    exit 1
fi

echo "📦 Extracting..."
tar -xzf "${TMP_DIR}/${TARBALL}" -C "${TMP_DIR}"

echo "🚀 Installing to /usr/local/bin..."
sudo install -d /usr/local/bin /usr/local/lib /etc/rundb
sudo install -m 755 "${TMP_DIR}"/*/bin/rundb /usr/local/bin/rundb
sudo install -m 755 "${TMP_DIR}"/*/bin/rundb_benchmark /usr/local/bin/rundb_benchmark
sudo install -m 755 "${TMP_DIR}"/*/lib/libjemalloc.so /usr/local/lib/libjemalloc.so
if [ -f "${TMP_DIR}"/*/config/rundb.conf ]; then
    sudo install -m 644 "${TMP_DIR}"/*/config/rundb.conf /etc/rundb/rundb.conf
fi

rm -rf "$TMP_DIR"
echo "🎉 RunDB Native installed successfully! Run with: rundb --port 7379"
