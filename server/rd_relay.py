#!/usr/bin/env python3
"""
Random Dungeon - servidor de salas (relay UDP).

Por que existe: pela internet quase ninguém consegue hospedar direto (roteador
fechado, CGNAT das operadoras, firewall do Windows). Aqui todo mundo só faz
conexões DE SAÍDA para este servidor, que repassa os pacotes:

    host  --(ENet)-->  relay:porta_do_cliente  <--(ENet)--  relay:4650  <--  cliente

1. O host manda "RDRL HOST <versão> [código]" pelo próprio socket do ENet e
   recebe "RDRL CODE <código>" (5 letras). Repete a cada ~10 s (mantém a sala).
2. O cliente manda "RDRL JOIN <versão> <código>" pelo socket do ENet dele e
   recebe "RDRL OK" (ou "RDRL ERR <motivo>"). Depois conecta o ENet normalmente
   em relay:4650.
3. Para cada cliente o relay abre uma porta UDP própria e avisa o host
   ("RDRL PEER <porta>"); o host responde "RDRL PUNCH" para essa porta (abre o
   NAT dele e diz ao relay o endereço certo). Daí em diante o host vê cada
   amigo como um endereço diferente (relay:porta), e o ENet funciona sem mudar
   nada no protocolo do jogo.

Não guarda nada em disco e não entende o conteúdo do jogo: só repassa bytes.

Uso:   python3 rd_relay.py [--port 4650] [--bind 0.0.0.0]
Precisa: Python 3.8+, UDP liberado na porta 4650 e nas portas altas de saída
(as portas por cliente são efêmeras, escolhidas pelo sistema).
"""
import argparse
import asyncio
import logging
import random
import time

MAGIC = b'RDRL'
CODE_CHARS = 'ABCDEFGHJKLMNPQRSTUVWXYZ'   # sem I/O (confundem com 1/0)
CODE_LEN = 5
ROOM_TIMEOUT = 45.0      # sala sem HOST por esse tempo -> apagada
CLIENT_TIMEOUT = 60.0    # cliente sem tráfego -> desligado
MAX_ROOMS = 2000
MAX_CLIENTS_PER_ROOM = 31
PENDING_MAX = 32         # pacotes do cliente guardados até o host furar o NAT

log = logging.getLogger('rd_relay')


class Room:
    def __init__(self, code, host_addr, version):
        self.code = code
        self.host_addr = host_addr      # endereço do host visto pela porta principal
        self.version = version
        self.last_seen = time.monotonic()
        self.clients = {}               # client_addr -> Client


class Client:
    def __init__(self, relay, room, addr):
        self.relay = relay
        self.room = room
        self.addr = addr
        self.transport = None           # socket exclusivo deste cliente (lado do host)
        self.port = 0
        self.host_ep = None             # endereço do host visto por essa porta (após PUNCH)
        self.pending = []
        self.last_seen = time.monotonic()
        self.peer_notices = 0

    # --- lado do host (porta exclusiva)
    def connection_made(self, transport):
        self.transport = transport

    def datagram_received(self, data, addr):
        if data.startswith(MAGIC + b' PUNCH'):
            if self.host_ep != addr:
                log.info('room %s: host punched port %d from %s', self.room.code, self.port, addr)
            self.host_ep = addr
            for pkt in self.pending:
                self.transport.sendto(pkt, addr)
            self.pending.clear()
            return
        if data.startswith(MAGIC):
            return
        if self.host_ep is None:
            self.host_ep = addr
        # ENet do host -> cliente, pela porta principal
        self.relay.main.sendto(data, self.addr)

    def error_received(self, exc):
        pass

    def connection_lost(self, exc):
        pass

    def to_host(self, data):
        self.last_seen = time.monotonic()
        if self.host_ep is None:
            if len(self.pending) < PENDING_MAX:
                self.pending.append(data)
            return
        self.transport.sendto(data, self.host_ep)

    def close(self):
        if self.transport:
            self.transport.close()
            self.transport = None


class Relay(asyncio.DatagramProtocol):
    def __init__(self, loop):
        self.loop = loop
        self.main = None
        self.rooms = {}          # code -> Room
        self.host_rooms = {}     # host_addr -> Room
        self.clients = {}        # client_addr -> Client

    def connection_made(self, transport):
        self.main = transport

    def reply(self, addr, text):
        self.main.sendto(MAGIC + b' ' + text.encode(), addr)

    def new_code(self):
        while True:
            code = ''.join(random.choice(CODE_CHARS) for _ in range(CODE_LEN))
            if code not in self.rooms:
                return code

    def datagram_received(self, data, addr):
        if data.startswith(MAGIC):
            try:
                self.command(data.decode('ascii', 'replace').split(), addr)
            except Exception as exc:   # nunca derruba o servidor por um pacote ruim
                log.warning('bad command from %s: %r (%s)', addr, data[:60], exc)
            return
        c = self.clients.get(addr)
        if c:
            c.to_host(data)

    def command(self, parts, addr):
        cmd = parts[1] if len(parts) > 1 else ''
        if cmd == 'PING':
            self.reply(addr, 'PONG')
        elif cmd == 'HOST':
            version = parts[2] if len(parts) > 2 else '?'
            want = parts[3].upper() if len(parts) > 3 else ''
            room = self.host_rooms.get(addr)
            if room is None and want and want in self.rooms and self.rooms[want].host_addr == addr:
                room = self.rooms[want]
            if room is None and want and want not in self.rooms and len(want) == CODE_LEN:
                room = self.open_room(want, addr, version)       # o host voltou (relay reiniciou)
            if room is None:
                if len(self.rooms) >= MAX_ROOMS:
                    self.reply(addr, 'ERR full')
                    return
                room = self.open_room(self.new_code(), addr, version)
            room.last_seen = time.monotonic()
            self.reply(addr, 'CODE %s' % room.code)
            # reenvia avisos de clientes cujo host ainda não furou o NAT
            for c in room.clients.values():
                if c.host_ep is None:
                    self.reply(room.host_addr, 'PEER %d' % c.port)
        elif cmd == 'JOIN':
            version = parts[2] if len(parts) > 2 else '?'
            code = parts[3].upper() if len(parts) > 3 else ''
            room = self.rooms.get(code)
            if room is None:
                self.reply(addr, 'ERR nocode')
                return
            if version != room.version:
                self.reply(addr, 'ERR version')
                return
            if addr in self.clients and self.clients[addr].room is room:
                self.reply(addr, 'OK')
                return
            if len(room.clients) >= MAX_CLIENTS_PER_ROOM:
                self.reply(addr, 'ERR full')
                return
            self.loop.create_task(self.add_client(room, addr))
        elif cmd == 'CLOSE':
            room = self.host_rooms.get(addr)
            if room:
                self.close_room(room, 'host closed')

    def open_room(self, code, addr, version):
        old = self.host_rooms.get(addr)
        if old:
            self.close_room(old, 'host reopened')
        room = Room(code, addr, version)
        self.rooms[code] = room
        self.host_rooms[addr] = room
        log.info('room %s opened by %s (version %s)', code, addr, version)
        return room

    def close_room(self, room, why):
        for c in list(room.clients.values()):
            self.drop_client(c)
        self.rooms.pop(room.code, None)
        if self.host_rooms.get(room.host_addr) is room:
            del self.host_rooms[room.host_addr]
        log.info('room %s closed (%s)', room.code, why)

    async def add_client(self, room, addr):
        if addr in self.clients:
            self.drop_client(self.clients[addr])
        c = Client(self, room, addr)
        local = self.main.get_extra_info('sockname')
        await self.loop.create_datagram_endpoint(lambda: c, local_addr=(local[0], 0))
        c.port = c.transport.get_extra_info('sockname')[1]
        room.clients[addr] = c
        self.clients[addr] = c
        log.info('room %s: client %s via port %d (%d in room)', room.code, addr, c.port, len(room.clients))
        self.reply(room.host_addr, 'PEER %d' % c.port)
        self.reply(addr, 'OK')

    def drop_client(self, c):
        c.close()
        c.room.clients.pop(c.addr, None)
        if self.clients.get(c.addr) is c:
            del self.clients[c.addr]

    async def housekeeping(self):
        while True:
            await asyncio.sleep(1.0)
            now = time.monotonic()
            for room in list(self.rooms.values()):
                if now - room.last_seen > ROOM_TIMEOUT:
                    self.close_room(room, 'timeout')
                    continue
                for c in list(room.clients.values()):
                    if now - c.last_seen > CLIENT_TIMEOUT:
                        log.info('room %s: client %s timed out', room.code, c.addr)
                        self.drop_client(c)
                    elif c.host_ep is None and c.peer_notices < 20:
                        # UDP perde pacote: insiste até o host furar o NAT
                        c.peer_notices += 1
                        self.reply(room.host_addr, 'PEER %d' % c.port)


async def main():
    ap = argparse.ArgumentParser(description='Random Dungeon relay (salas por código)')
    ap.add_argument('--port', type=int, default=4650)
    ap.add_argument('--bind', default='0.0.0.0')
    ap.add_argument('-v', '--verbose', action='store_true')
    args = ap.parse_args()
    logging.basicConfig(level=logging.DEBUG if args.verbose else logging.INFO,
                        format='%(asctime)s %(levelname)s %(message)s')
    loop = asyncio.get_running_loop()
    relay = Relay(loop)
    await loop.create_datagram_endpoint(lambda: relay, local_addr=(args.bind, args.port))
    log.info('relay listening on %s:%d/udp', args.bind, args.port)
    await relay.housekeeping()


if __name__ == '__main__':
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        pass
