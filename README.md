# Random Dungeon

Roguelite de masmorras e hordas, **online** (LAN / servidor), isométrico, feito sobre o motor [Flare](https://github.com/flareteam/flare-engine) (C++/SDL2), com camada de rede própria (ENet, host autoritativo).

> Status: protótipo. O que existe hoje: masmorra procedural, 3 classes (Brute, Rogue, Adept), multiplayer com posição/aparência/ataques/inimigos compartilhados, inimigos que perseguem e ferem todos os jogadores, e (em andamento) modo hordas em mapa aberto.

## Como rodar (Linux / WSL)

**1. Dependências**

Fedora:
```bash
sudo dnf install cmake gcc-c++ make SDL2-devel SDL2_image-devel SDL2_mixer-devel SDL2_ttf-devel enet-devel
```
Debian / Ubuntu / WSL:
```bash
sudo apt install cmake build-essential libsdl2-dev libsdl2-image-dev libsdl2-mixer-dev libsdl2-ttf-dev libenet-dev
```

**2. Clonar e compilar**
```bash
git clone <URL-DO-REPOSITORIO> random_dungeon
cd random_dungeon
scripts/build.sh          # gera build/flare
```

**3. Jogar (single-player)**
```bash
scripts/run.sh
```
Na tela inicial: **Play** -> escolha retrato e classe -> **Create**.

## Multiplayer

Um jogador **hospeda**, os outros **conectam** pelo IP dele. A porta padrão é **4650/UDP**.

```bash
# jogador 1 (host): abre o jogo e ja hospeda
scripts/host.sh

# jogador 2 (mesma maquina, para testar)
scripts/join.sh 127.0.0.1

# jogador 2 (outra maquina na LAN)
scripts/join.sh 192.168.0.10
```

Regras importantes:
- **O host precisa estar dentro do jogo** (personagem criado, mapa carregado) *antes* de o cliente conectar; senão o cliente dá timeout em ~5 s.
- Liberar a porta no firewall do host. Fedora: `sudo firewall-cmd --add-port=4650/udp`. Ubuntu: `sudo ufw allow 4650/udp`.
- Testando na mesma máquina: se aparecer o aviso de "instância já rodando", clique em **Continue**.
- Também dá pra hospedar/conectar pelo botão **Multiplayer** da tela inicial (sem scripts).

Logs de rede: `~/.config/flare/flare_log_host.txt` e `flare_log_client.txt`.

## Windows

Dois caminhos (o segundo **ainda não foi testado**, não trate como garantido):

1. **WSL2 (funciona hoje)**: instale o WSL com Ubuntu e siga a seção Linux acima. Requer WSLg (Windows 11 / Windows 10 atualizado) pra abrir a janela.
2. **`.exe` nativo (em preparação)**: compilar com [MSYS2](https://www.msys2.org) (terminal *MSYS2 MinGW x64*):
   ```bash
   pacman -S --needed mingw-w64-x86_64-{gcc,cmake,make,SDL2,SDL2_image,SDL2_mixer,SDL2_ttf,enet}
   cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
   cmake --build build -j
   ```
   Depois use `scripts\run.bat`, `scripts\host.bat` e `scripts\join.bat` (os `.bat` esperam `build\flare.exe`). Para distribuir, copie as DLLs do MinGW/SDL2 para a mesma pasta do `.exe`.

## Problemas comuns

- **Mods "not found" no log / jogo com visual diferente**: o Flare lê a lista de mods de `~/.config/flare/mods.txt` (tem prioridade sobre `mods/mods.txt` do repositório). Apague esse arquivo ou deixe nele: `fantasycore`, `empyrean_campaign`, `random_dungeon`, `darkfantasy_gui` (nessa ordem; mods mais abaixo sobrescrevem os de cima).
- **`ERROR: ... game_over.png`** no log: asset da tela de game over ainda não criado (cosmético).
- **Sem som/música**: confira `music_volume` em `~/.config/flare/settings.txt`.

## Estrutura

```
src/                motor (C++), incluindo NetManager (rede) e HordeManager (hordas)
mods/default        base do motor
mods/fantasycore    mecânica e assets base (do projeto Flare)
mods/empyrean_campaign  dados/inimigos/tilesets reaproveitados (do projeto Flare)
mods/random_dungeon o jogo: classes, poderes, mapas, música, menus
mods/darkfantasy_gui    interface (menus e logo)
scripts/            build/run/host/join (.sh e .bat)
docs/UPSTREAM.md    origem do código e licenças
```

## Licenças e créditos

Este projeto é derivado do Flare, então **continua aberto**: código do motor sob **GPL v3** (`COPYING`), conteúdo do Flare sob **CC-BY-SA 3.0** (`LICENSE-CONTENT-flare-game.txt`). Créditos completos em `CREDITS.md`, `CREDITS.engine.txt` e `CREDITS.content-flare-game.txt`.

Música dos menus: *Anguish* de Kevin MacLeod ([incompetech.com](https://incompetech.com)), licença [CC BY 3.0](https://creativecommons.org/licenses/by/3.0/).
