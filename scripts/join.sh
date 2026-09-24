#!/usr/bin/env bash
# Abre o jogo e conecta em um host. Uso: scripts/join.sh IP[:PORTA]   (padrao 127.0.0.1:4650)
cd "$(dirname "$0")/.."
exec ./build/flare --data-path="$PWD" --net-join="${1:-127.0.0.1:4650}"
