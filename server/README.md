# Servidor de salas (relay)

`rd_relay.py` deixa os amigos entrarem na partida com um **código de 5 letras**,
sem abrir porta no roteador: host e amigos só fazem conexões de saída para cá.
Funciona com CGNAT, VPN (WARP), firewall do Windows. Não guarda nada.

Precisa de uma máquina com **IP público** e UDP liberado (qualquer VPS barata,
ou a Oracle Cloud "Always Free"). Consome quase nada (só repassa pacotes).

## Instalar (Debian/Ubuntu)

```bash
sudo mkdir -p /opt/rd-relay
sudo cp rd_relay.py /opt/rd-relay/
sudo cp rd-relay.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now rd-relay
sudo journalctl -u rd-relay -f          # ver salas abrindo/fechando
```

Firewall: liberar **UDP 4650** e UDP **32768-60999** (as portas de cada amigo
são efêmeras). Ex.: `sudo ufw allow 4650/udp && sudo ufw allow 32768:60999/udp`.
Na Oracle Cloud, abra as mesmas portas também na "Security List" da VCN.

## Apontar o jogo para ele

`mods/random_dungeon/engine/online.txt`:

```
relay=SEU.IP.OU.DOMINIO:4650
```

Depois gere os pacotes de novo (`distribution/windows/package_windows.py`,
`distribution/linux/package_linux.py`).

## Testar sem o jogo

```bash
python3 -c "import socket;s=socket.socket(2,2);s.settimeout(3);s.sendto(b'RDRL PING',('SEU.IP',4650));print(s.recv(99))"
# b'RDRL PONG'
```
