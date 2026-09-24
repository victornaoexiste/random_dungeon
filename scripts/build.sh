#!/usr/bin/env bash
# Compila o Random Dungeon (Linux / WSL / macOS com deps instaladas).
set -euo pipefail
cd "$(dirname "$0")/.."
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc 2>/dev/null || echo 4)"
echo "OK: build/flare"
