#!/usr/bin/env bash
# Abre o jogo (single-player).
cd "$(dirname "$0")/.."
exec ./build/flare --data-path="$PWD" "$@"
