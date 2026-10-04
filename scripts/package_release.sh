#!/usr/bin/env bash
set -euo pipefail

# Configure and compile in Release preset
cmake --preset release
cmake --build --preset build-release

# Extract version from compiled binary if not explicitly specified via environment
VERSION="${VERSION:-$(./build/release/rundb --version | awk '{print $3}')}"
ARCH="$(uname -m)"
PACKAGE_NAME="rundb-${VERSION}-linux-${ARCH}"
DIST_DIR="dist/${PACKAGE_NAME}"

echo "🔨 Packaging RunDB Native Release ${VERSION} (${ARCH})..."

# Assemble distribution directory
rm -rf "dist"
mkdir -p "${DIST_DIR}/bin" "${DIST_DIR}/lib" "${DIST_DIR}/config"

cp build/release/rundb "${DIST_DIR}/bin/rundb"
cp build/release/rundb_benchmark "${DIST_DIR}/bin/rundb_benchmark"
cp build/release/rundb_tests "${DIST_DIR}/bin/rundb_tests"
if [ -f build/release/libjemalloc.so ]; then
    cp build/release/libjemalloc.so "${DIST_DIR}/lib/libjemalloc.so"
elif [ -f lib/libjemalloc.so ]; then
    cp lib/libjemalloc.so "${DIST_DIR}/lib/libjemalloc.so"
fi
(cd "${DIST_DIR}/lib" && ln -sf libjemalloc.so libjemalloc.so.2)

cp config/rundb.conf "${DIST_DIR}/config/rundb.conf"
cp LICENSE "${DIST_DIR}/"
cp README.md "${DIST_DIR}/"

# Add standalone install script inside tarball
cat << 'EOF' > "${DIST_DIR}/install.sh"
#!/usr/bin/env bash
set -e
PREFIX="${PREFIX:-/usr/local}"
echo "Installing RunDB Native to ${PREFIX}..."
sudo install -d "${PREFIX}/bin" "${PREFIX}/lib" "${PREFIX}/etc/rundb"
sudo install -m 755 bin/rundb "${PREFIX}/bin/rundb"
sudo install -m 755 bin/rundb_benchmark "${PREFIX}/bin/rundb_benchmark"
if [ -f lib/libjemalloc.so ]; then
    sudo install -m 755 lib/libjemalloc.so "${PREFIX}/lib/libjemalloc.so"
    sudo ln -sf libjemalloc.so "${PREFIX}/lib/libjemalloc.so.2"
    sudo ldconfig 2>/dev/null || true
fi
sudo install -m 644 config/rundb.conf "${PREFIX}/etc/rundb/rundb.conf"
echo "RunDB Native successfully installed to ${PREFIX}/bin/rundb!"
echo "Run with: rundb --config ${PREFIX}/etc/rundb/rundb.conf"
EOF
chmod +x "${DIST_DIR}/install.sh"

# Create compressed tarball and checksum
tar -czvf "dist/${PACKAGE_NAME}.tar.gz" -C dist "${PACKAGE_NAME}"
(cd dist && sha256sum "${PACKAGE_NAME}.tar.gz" > "${PACKAGE_NAME}.tar.gz.sha256")

echo "✅ Generated: dist/${PACKAGE_NAME}.tar.gz"
echo "✅ Checksum:  dist/${PACKAGE_NAME}.tar.gz.sha256"
