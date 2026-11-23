#!/bin/bash

echo "This script builds the earlier draft library, not LDP-1."
echo "The supported protocol build is in README.md."
echo "Building the earlier draft for Linux..."

# Check for dependencies
if ! command -v cmake &> /dev/null; then
    echo "Error: cmake not found. Please install cmake."
    exit 1
fi

if ! pkg-config --exists libsodium; then
    echo "Warning: libsodium not found via pkg-config. Trying to find manually..."
    if [ ! -f /usr/lib/libsodium.so ] && [ ! -f /usr/local/lib/libsodium.so ]; then
        echo "Error: libsodium not found. Please install libsodium-dev"
        echo "  Ubuntu/Debian: sudo apt-get install libsodium-dev"
        echo "  Fedora/RHEL: sudo dnf install libsodium-devel"
        echo "  Arch: sudo pacman -S libsodium"
        exit 1
    fi
fi

# Create build directory
mkdir -p build
cd build

echo "Configuring CMake..."
cmake .. -DCMAKE_BUILD_TYPE=Release

if [ $? -ne 0 ]; then
    echo "CMake configuration failed!"
    exit 1
fi

echo "Building..."
cmake --build . -j$(nproc)

if [ $? -ne 0 ]; then
    echo "Build failed!"
    exit 1
fi

echo "Build successful!"
echo "Output: build/libldp.a (or build/libldp.so)"
echo ""
echo "To install: sudo cmake --install ."
echo "To run tests: ctest"

