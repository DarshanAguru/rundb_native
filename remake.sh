#!/bin/bash
set -eu pipefail

echo "==> RunDB rebuild started"

rm -rf build
cmake --preset default
cmake --build --preset build-debug

echo "==> Build completed successfully"