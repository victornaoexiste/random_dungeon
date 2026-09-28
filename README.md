# Encantados

Roguelite co-op de hordas do **folclore brasileiro**, isométrico, **online** (código de sala, rede local ou IP), feito sobre o motor [Flare](https://github.com/flareteam/flare-engine) (C++/SDL2), com camada de rede própria (ENet, host autoritativo). Antes se chamava *Random Dungeon* (o repositório, a pasta do mod `random_dungeon` e a pasta de saves continuam com esse nome).

> Hoje: acampamento (lobby) → Horda Infinita com ondas temáticas e um chefe do folclore a cada 5 ondas (Saci-Pererê, Curupira, Minhocão, Mula sem Cabeça, Boitatá, Cuca, Mapinguari, Cobra Grande...); 4 classes (Cangaceiro, Mateiro, Benzedeiro, Feiticeiro), 75 habilidades, itens e conjuntos; Santuário com bênçãos permanentes; co-op com revive.

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

## Modo hordas

Novo jogo cai na **Arena** (mapa aberto, 100x100). A cada 8 s uma horda aparece fora da tela ao redor de um jogador vivo; a cada 45 s a onda sobe (hordas maiores, inimigos mais fortes e novos tipos). Tudo é configurável em `mods/random_dungeon/engine/horde.txt`; o mapa é gerado por `python3 tools/gen_horde_arena.py`. Em multiplayer, só o host simula a horda e todos veem os mesmos inimigos. No log (`~/.config/flare/flare_log.txt`) procure `HordeManager: wave` pra ver os spawns. Para voltar à masmorra procedural, troque o `intermap=` em `mods/random_dungeon/maps/spawn.txt`.

## Multiplayer

Três jeitos de jogar junto, todos no mesmo jogo:

1. **Pelo código da sala (internet)**: *Multiplayer > Criar partida*, ou no meio
   de uma partida solo: *Esc > Jogar com amigos*. Aparece um código de 5 letras
   (canto da tela e menu de pausa). Os amigos vão em *Multiplayer*, digitam o
   código e *Entrar*. Ninguém precisa abrir porta: tudo passa pelo servidor de
   salas (`server/`, endereço em `mods/random_dungeon/engine/online.txt`).
2. **Rede local**: o host faz o mesmo; o amigo usa *Buscar na rede*.
3. **IP direto** (avançado): digite `IP` ou `IP:porta` no lugar do código
   (precisa da porta **4650/UDP** aberta no host).

O host pode estar no menu, criando personagem ou já jogando: o amigo conecta
e segue o host para o mapa dele. *Fechar para amigos* volta a partida a solo.

Linha de comando / testes:

```bash
scripts/host.sh                       # hospeda na porta 4650
scripts/join.sh 192.168.0.10          # entra por IP
./build/flare --data-path=. --net-join=ABCDE   # entra por código
python3 server/rd_relay.py --port 4650         # relay local para testar
```

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

## Branch `dev`

Versão de desenvolvimento completa (Mundo Aberto, dungeons, Sala de Teste,
Run Infinita, multiplayer, Android). O jogo comercial é a edição reduzida
(`mods/random_dungeon/engine/edition.txt` com `edition=run`).

- As imagens de `mods/darkfantasy_sprites` são geradas: `python3 mods/darkfantasy_sprites/tools/grade.py`.
- `mods/ek_icons` é de uso pessoal e não faz parte do repositório.
- Conteúdo gerado: `mods/random_dungeon/tools/gen_*.py` a partir dos CSVs.

## Como testar em outra máquina

**Windows / Android (mais fácil):** baixe na página *Releases* do GitHub o
`RandomDungeon-run-win64.zip` (descompacte e abra `RandomDungeon.exe`) ou o
`RandomDungeon.apk` (instale no celular; permita "fontes desconhecidas").

**Linux (a partir do código):**

```sh
# Fedora
sudo dnf install gcc-c++ cmake SDL2-devel SDL2_image-devel SDL2_mixer-devel SDL2_ttf-devel enet-devel python3-pillow
# Debian/Ubuntu
sudo apt install g++ cmake libsdl2-dev libsdl2-image-dev libsdl2-mixer-dev libsdl2-ttf-dev libenet-dev python3-pil

git clone -b dev https://github.com/victornaoexiste/random_dungeon.git && cd random_dungeon
python3 mods/darkfantasy_sprites/tools/grade.py     # gera as sprites do tema (uma vez)
cmake -B build -DCMAKE_BUILD_TYPE=Release && make -C build -j8
./build/flare --data-path=.
```

**Multiplayer:** todos precisam da mesma versão. Um jogador escolhe
Multiplayer > Hospedar; os outros usam "Buscar na rede" (mesma rede local) ou
o IP do anfitrião (porta 4650 UDP liberada no roteador/firewall para jogar
pela internet).

**Gerar os instaláveis:** `python3 distribution/windows/package_windows.py`
(cross-compile MinGW, ver o topo do arquivo) e
`python3 flare-android-project/pack_data.py --edition run && ./gradlew assembleDebug`
dentro de `flare-android-project` (SDK/NDK: `setup_android_deps.sh`).

## Prints e GIFs para divulgação (modo trailer)

```sh
./build/flare --data-path=. --trailer          # Linux (código-fonte)
./jogar.sh --trailer                           # Linux (pacote)
RandomDungeon-trailer.bat                      # Windows (pacote)
```

Entre numa Run: HUD e cursor somem, o herói não morre, a tela não treme.

| Tecla | Ação |
|---|---|
| F1 | mostra / esconde o HUD |
| F2 | screenshot PNG (tamanho real) |
| F3 | começa / para de gravar quadros para GIF (15 fps, até 20 s) |
| F4 | trava / solta a câmera |

Arquivos em `~/.local/share/flare/trailer/` (Linux) ou
`%APPDATA%\RandomDungeon\userdata\trailer\` (Windows).
GIF/MP4: `python3 distribution/make_gif.py <pasta gif_NNN>`.

Para prints limpos, desligue o texto de combate em Configurações (ou
`combat_text=0` no `settings.txt`). Variáveis opcionais:
`RD_TRAILER_CROWD=1.3` (tamanho da horda; padrão 4) e, para capturas
automáticas, `RD_TRAILER_TEST=shots RD_TRAILER_EVERY=2` (um print a cada
2 × 0,5 s a partir dos 10 s).
