# Encantados — como continuar

Guia prático do projeto, com comandos exatos. (O jogo se chamava *Random Dungeon*:
o repositório, a pasta do mod `random_dungeon` e as pastas de saves continuam com
esse nome de propósito, pra ninguém perder saves.)

## 0. Onde está cada coisa

| O quê | Onde |
|---|---|
| Código + jogo (branch `dev` = `folclore`) | `~/Documentos/claude-projeto/rd_folclore` (e `rd_dev`) |
| GitHub | `github.com/victornaoexiste/random_dungeon` (público: o motor é GPL) |
| Pacotes pra vender | `rd_folclore/dist/Encantados-run-win64.zip` e `...-linux64.tar.gz` |
| Texto da página do itch | `~/Documentos/claude-projeto/marketing/itch_page.md` |
| Prints / GIF | `~/Documentos/claude-projeto/marketing/imagens/` |
| Servidor de salas | `server/` (ver `server/README.md`) |

Antigos (não use mais): `flare-engine/`, `flare-game/`, `random_dungeon/`.

## 1. Compilar e jogar (Linux)

```bash
cd ~/Documentos/claude-projeto/rd_folclore
scripts/build.sh
./build/flare --data-path=.
```
Mapa preto depois de clonar em outro lugar:
`python3 mods/random_dungeon/tools/gen_world.py --tilesets` e
`python3 mods/darkfantasy_sprites/tools/grade.py`.

## 2. Gerar os pacotes (Windows + Linux)

```bash
cd ~/Documentos/claude-projeto/rd_folclore
mingw64-cmake -S . -B build-win -DCMAKE_BUILD_TYPE=Release \
    -DENET_INCLUDE_DIR=$HOME/win-deps/include -DENET_LIBRARY=$HOME/win-deps/lib/libenet.a
make -C build-win -j8
python3 distribution/windows/package_windows.py
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release && make -C build-release -j8
python3 distribution/linux/package_linux.py
```
O empacotador para com erro se faltar imagem, e a mensagem diz o comando pra corrigir.
Testar o .exe sem Windows: `cd dist/Encantados-run-win64 && wine Encantados.exe`

## 3. Editar conteúdo (não edite arquivos marcados GENERATED)

| Quero mudar | Edite | Depois rode |
|---|---|---|
| Monstros | `mods/random_dungeon/tools/ek_data/enemies.csv` | `python3 mods/random_dungeon/tools/gen_content.py --offline` |
| Itens | `.../ek_data/items.csv` | idem |
| Habilidades | `.../ek_data/skills.csv` | `python3 mods/random_dungeon/tools/gen_skills.py` + gen_content |
| Traduções da interface | `EXTRA_PT` em `tools/gen_content.py` | gen_content |
| Ondas e ordem dos chefes | `mods/random_dungeon/engine/horde.txt` (`boss=`) | nada |
| Classes (nome, feminino, descrição) | `mods/random_dungeon/engine/classes.txt` | nada |
| Desenhos (cobras, Mula, Minhocão, Saci, Curupira, fogueira/portal/altar) | `SPECIES` em `tools/gen_serpents.py` | `python3 mods/random_dungeon/tools/gen_serpents.py nome` |
| Acampamento / arenas | `gen_mode_maps()` em `tools/gen_world.py` | `python3 mods/random_dungeon/tools/gen_world.py` |

## 4. Jogar com amigos

- Mesma rede: host em Multiplayer > Criar partida (ou Esc > Jogar com amigos); amigo em Multiplayer > Buscar na rede > Entrar, ou digita o IP que aparece embaixo do minimapa do host.
- Wi-Fi de escola/visitantes ou casas diferentes: Tailscale nos dois PCs; o amigo digita o IP "Tailscale: 100.x.x.x" do host.
- Windows hospedando: rodar `Liberar-firewall.bat` uma vez.
- Logs: Linux `~/.config/flare/flare_log*.txt`; Windows `%APPDATA%\RandomDungeon\config\`.

## 5. Servidor de salas (Oracle Cloud grátis, precisa de cartão)

1. Conta em oracle.com/cloud/free, região Brazil East (São Paulo); depois, upgrade pra Pay As You Go (segue grátis e a VM não é desligada).
2. VM Ubuntu `VM.Standard.E2.1.Micro` (Always Free); baixar a chave SSH.
3. VCN > Security List: liberar UDP 4650 e UDP 32768-60999.
4. Na VM (`ssh -i chave.key ubuntu@IP`):
   `sudo iptables -I INPUT -p udp --dport 4650 -j ACCEPT && sudo iptables -I INPUT -p udp --dport 32768:60999 -j ACCEPT && sudo apt install -y iptables-persistent && sudo netfilter-persistent save`
5. `scp -i chave.key server/rd_relay.py server/rd-relay.service ubuntu@IP:` e seguir `server/README.md`.
6. `mods/random_dungeon/engine/online.txt` → `relay=IP:4650`; gerar os pacotes de novo.

## 6. Vender no itch.io

1. itch.io > Upload new project; Kind: Downloadable.
2. Preço: mínimo US$ 3 / R$ 15 (ou grátis com doação pra começar).
3. Uploads: `.zip` marcado Windows, `.tar.gz` marcado Linux.
4. Texto de `marketing/itch_page.md` (o rodapé de licenças é obrigatório).
5. Imagens de `marketing/imagens/`. Pagamentos: PayPal ou Payoneer em "Payouts".

## 7. O que falta (ordem de impacto)

1. Servidor de salas (seção 5).
2. Teste real com amigo no Windows 11 (só testado no Wine): criar partida, entrar, onda 5, cair e reviver.
3. Visual dos monstros comuns (Sacis são goblins, Lobisomens/Mapinguari são minotauros, Cuca é dragão): usar `biped_pose`/`quad_pose` do gen_serpents.py.
4. Equilíbrio dos chefes novos.
5. Tamanho do pacote (684 MB; tilesets tingidos enormes).
6. Posts (`marketing/posts.md`): gancho co-op + chefes; diferença pro *Vigília do Lampião* (solo, navegador).

## 8. Ganchos de teste

| Variável | Faz |
|---|---|
| `--trailer` | HUD limpo; F1 HUD, F2 print, F3 GIF, F4 trava câmera |
| `RD_AUTO_NEW="1,classe,retrato,0,0,0,run"` | cria personagem sozinho (classe 0-3; retrato 0-5 homem, 20+ mulher) |
| `RD_START_MAP=maps/run/start.txt` | pula o acampamento |
| `RD_SHOWCASE="rd_mula,rd_minhocao"` + `RD_SHOWCASE_SHOT=pasta` (`_AT`, `_SHOTS`, `_EVERY`) | monstros em volta do herói + prints |
| `RD_OPEN_ROOM=3` | abre pros amigos após 3 s |
| `--net-join=IP` / `lan` / `CODIGO` | entra numa partida |
| `RD_BANNER_SHOT=pasta` | print de cada aviso de onda |
| `RD_CAMP_SHOT=pasta` | prints do acampamento e Santuário |

## 9. Plano pra fazer dinheiro (escrito em 2026-09-29)

O jogo **já está vendável**: pacotes Windows/Linux prontos em `dist/`, página e imagens em
`../marketing/`. O que falta é colocar à venda e trazer gente. Nessa ordem:

1. **Hoje: publicar no itch.io** (seção 6). Preço US$ 2,99 com "pagar mais se quiser" ligado.
   Não espere o servidor de salas: dá pra vender como "co-op na mesma rede / Tailscale".
2. **Todo dia, 1 vídeo curto** (TikTok, Reels, Shorts) de 15-30 s: um chefe do folclore
   aparecendo (Mula sem Cabeça, Minhocão, Saci). Legenda com o link do itch.
   Gravar: `./build/flare --data-path=. --trailer` (F3 grava GIF) ou OBS.
   Gancho: "fiz um jogo de sobreviver hordas com o folclore brasileiro, dá pra jogar com amigo".
3. **Postar uma vez em cada**: r/brasil, r/gamesEcultura, r/IndieGaming, r/roguelites,
   TabNews, grupos de Discord de gamedev BR. Texto pronto em `../marketing/posts.md`.
4. **Apoio recorrente**: página no apoia.se ou Catarse ("ajude o jogo do folclore a crescer").
   Link na página do itch e no menu do jogo.
5. **Steam só depois** que o itch mostrar interesse (taxa US$ 100 + precisa de wishlists).
6. **Mandar pra criadores de conteúdo** que jogam indie BR: chave grátis do itch
   (Dashboard > Distribute > Download keys).

Sendo realista: jogo indie leva meses pra render algo que pague contas. Enquanto isso, o
**Vereda** (sistema escolar, já no ar) pode ser vendido pra escolas pequenas por mensalidade,
e freela de Django/Python paga mais rápido. Use o jogo como portfólio também.

Pendente de UI: Victor mandou uma imagem de referência (skills, itens, opções, retrato abre
inventário) que não chegou. Retrato → inventário já funciona (`src/MenuHUD.*`).
