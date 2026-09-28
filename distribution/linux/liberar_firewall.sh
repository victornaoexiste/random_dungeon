#!/usr/bin/env bash
# Random Dungeon - libera as portas da rede local (4650-4651/udp) no firewall.
# Só é preciso para HOSPEDAR partida; para entrar na de um amigo não precisa.
set -e
if command -v firewall-cmd >/dev/null && sudo firewall-cmd --state >/dev/null 2>&1; then
    sudo firewall-cmd --permanent --add-port=4650-4651/udp
    sudo firewall-cmd --reload
    echo "Pronto (firewalld): portas 4650-4651/udp liberadas."
elif command -v ufw >/dev/null && sudo ufw status | grep -q "Status: active"; then
    sudo ufw allow 4650:4651/udp
    echo "Pronto (ufw): portas 4650-4651/udp liberadas."
else
    echo "Nenhum firewall ativo encontrado (firewalld/ufw): nada a fazer."
fi
