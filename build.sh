#!/bin/bash

set -e

if [ "$(id -u)" -ne 0 ]; then
    echo "Error: this script must be run as root."
    echo "Usage: sudo ./build.sh"
    exit 1
fi

# Install live-build if not present
if ! command -v lb > /dev/null 2>&1; then
    echo "Installing live-build..."
    apt-get update
    apt-get install -y live-build
fi

cd "$(dirname "$0")"

echo "=== Cleaning previous build ==="
lb clean

echo "=== Configuring live-build ==="
lb config

echo "=== Building ISO (this will take a while) ==="
lb build

ISO=$(ls -1 koder-linux-amd64.hybrid.iso 2>/dev/null || true)
if [ -n "$ISO" ]; then
    echo ""
    echo "=== Build complete ==="
    echo "ISO: $(pwd)/$ISO"
    echo "Size: $(du -h "$ISO" | cut -f1)"
else
    echo "Build finished but ISO not found. Check build.log for errors."
    exit 1
fi
