#!/bin/bash

echo "This script builds the earlier draft library, not LDP-1."
echo "The supported protocol build is in README.md."
echo "Building the earlier draft for macOS..."

# Check for dependencies
if ! command -v cmake &> /dev/null; then
    echo "Error: cmake not found. Please install cmake."
    echo "  brew install cmake"
    exit 1
fi

# Check for libsodium
if ! pkg-config --exists libsodium 2>/dev/null; then
    echo "libsodium not found. Installing via Homebrew..."
    if command -v brew &> /dev/null; then
        brew install libsodium
    else
        echo "Error: Homebrew not found. Please install libsodium manually."
        echo "  brew install libsodium"
        exit 1
    fi
fi

# Create build directory
mkdir -p build
cd build

echo "Configuring CMake..."
cmake .. -DCMAKE_BUILD_TYPE=Release \
         -DCMAKE_OSX_ARCHITECTURES="x86_64;arm64" \
         -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0

if [ $? -ne 0 ]; then
    echo "CMake configuration failed!"
    exit 1
fi

echo "Building..."
cmake --build . -j$(sysctl -n hw.ncpu)

if [ $? -ne 0 ]; then
    echo "Build failed!"
    exit 1
fi

echo "Build successful!"
echo "Output: build/libldp.a (or build/libldp.dylib)"
echo ""
echo "To install: sudo cmake --install ."
echo "To run tests: ctest"

