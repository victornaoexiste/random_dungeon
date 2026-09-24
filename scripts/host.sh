#!/usr/bin/env bash
# Abre o jogo e hospeda uma partida online na porta 4650 (ou $1).
# O host precisa estar dentro do jogo (mapa carregado) antes dos clientes conectarem.
cd "$(dirname "$0")/.."
exec ./build/flare --data-path="$PWD" --net-host="${1:-4650}"
