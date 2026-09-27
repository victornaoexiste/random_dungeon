# Random Dungeon — status do projeto

Mod para o [Flare Engine](https://github.com/flareteam/flare-engine) (ARPG isométrico open source). Objetivo: um "horda-survival" roguelite (estilo Vampire Survivors / Hades) com masmorra procedural, inimigos e loot aleatórios escalando por nível. Ambição de longo prazo: multiplayer LAN, depois servidor online.

Este documento existe pra qualquer sessão nova (humana ou IA) retomar o trabalho sem precisar redescobrir tudo que já foi resolvido.

## Estrutura no disco

```
~/Documentos/claude-projeto/
├── flare-engine/        clone git oficial (github.com/flareteam/flare-engine) — o MOTOR (C++), com nossas mudanças
│   ├── build/           build (cmake/make); o binário é build/flare. Pasta ignorada pelo git.
│   └── mods/            mods/default e mods/gcw0_defaults são do próprio motor; darkfantasy_gui é local;
│                        fantasycore, empyrean_campaign, random_dungeon, darkfantasy_sprites e ek_icons
│                        são SYMLINKS pra flare-game/mods/
└── flare-game/          clone git oficial (github.com/flareteam/flare-game) — o CONTEÚDO
    └── mods/
        ├── fantasycore/        mecânica/assets base (não mexemos)
        ├── empyrean_campaign/  campanha padrão, fonte de inimigos/itens/tilesets (não mexemos)
        ├── random_dungeon/     <<< NOSSO MOD. Tudo que criamos está aqui.
        └── ek_icons/           ícones originais do Exiled Kingdoms (uso pessoal, fora do git)
```
Configuração e saves do jogo: `~/.config/flare/` (settings, mods.txt ativo, logs) e `~/.local/share/flare/saves/random_dungeon/`.

**Regra de ouro:** só editamos dentro de `flare-game/mods/random_dungeon/`. `fantasycore` e `empyrean_campaign` são referência/dependência, nunca modificados diretamente — se algo precisa mudar neles, criamos um arquivo com o mesmo caminho relativo dentro de `random_dungeon/` (o Flare sobrepõe por ordem de mod, ver `settings.txt`).

## Lição mais importante da sessão: NÃO use pacotes de distro

O pacote `flare-engine`/`flare-game` do `apt` (testado no Kali 1.15 e no Ubuntu/WSL 1.14) está **desatualizado e incompleto** em relação ao repositório oficial:
- Faltam features do motor (ex: `procgen_filename` em eventos de mapa, `equipment_set` em classes).
- Faltam/estão quebrados assets do conteúdo (ex: `tileset_ruins.txt` ausente, `icons.png`/`icons_crafting.png` truncados no 1.14).
- IDs de power/item mudam entre versões do conteúdo.

**Solução adotada:** compilar o `flare-engine` a partir do clone git (não do apt) e apontar `--data-path` para uma pasta cujo `mods/` contenha os mods do clone git do `flare-game` (via symlink) + `mods/default` do próprio `flare-engine`. Ver seção "Como rodar" abaixo. Isso eliminou uma sequência inteira de bugs (ver "Histórico de bugs resolvidos").

## Como compilar e rodar (Linux nativo ou WSL)

```bash
# 1. Clonar (se ainda não tiver)
git clone https://github.com/flareteam/flare-engine.git
git clone https://github.com/flareteam/flare-game.git

# 2. Dependências de build
sudo apt-get install -y cmake build-essential libsdl2-dev libsdl2-image-dev libsdl2-mixer-dev libsdl2-ttf-dev

# 3. Compilar o motor
cd flare-engine
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)

# 4. Linkar os mods de conteúdo pra dentro do flare-engine/mods (que já tem mods/default)
cd ../mods
ln -sfn ../../flare-game/mods/fantasycore fantasycore
ln -sfn ../../flare-game/mods/empyrean_campaign empyrean_campaign
ln -sfn ../../flare-game/mods/random_dungeon random_dungeon
printf 'fantasycore\nempyrean_campaign\nrandom_dungeon\n' > mods.txt

# 5. Rodar apontando pro flare-engine (repo raiz) como data-path
cd ../build
./flare --data-path=/caminho/absoluto/pra/flare-engine
```

Se `flare_log.txt` (em `~/.config/flare/`) mostrar `Active mods: fantasycore (1.15.01), empyrean_campaign (1.15.01), random_dungeon (0.10)` sem erros, está correto.

## O que já está construído

### Mapas (`maps/`)
- **`spawn.txt`**: ponto de entrada de todo novo jogo, redireciona pra `maps/world/aurora_a.txt` (mundo aberto).
- **`dungeon.txt` + `procgen_rules/random_dungeon.txt`**: masmorra procedural (geração aleatória nativa do Flare, `procgen_filename`). Reaproveita os "chunks" de sala prontos da campanha (`empyrean_campaign/maps/iron_labyrinth/room*.txt`, `links*.txt`, `door_*.txt`) e o tileset `tileset_ruins`. Configurado pra mapa **pequeno** (`main_path_length_min=3, max=6`, `doors_max=1`) — ajustar em `random_dungeon.txt` se quiser maior.
  - O sistema de inimigo aleatório nativo (`[enemy]` com `category=`, `number=min,max`, `spawn_level=hero_level,N`) é usado pelos chunks de sala (ex: `category=il_common`). As masmorras do mundo aberto usam cópias dos chunks com categorias da própria região (ver "Mundo aberto").
- **`room1.txt` / `room2.txt` / `room3.txt`**: sistema ANTERIOR de salas fixas com dificuldade crescente (fraco→médio→forte). Não está mais no fluxo principal, mas os arquivos continuam aqui, funcionais, caso queira comparar ou reaproveitar.
- **`test_room.txt` → `cave_room.txt` → `grassland_room.txt` → `snow_room.txt` → `infernal_room.txt` → (volta pro test_room)**: sala de exibição de assets — um exemplar parado (`speed=0`) de cada família de inimigo, útil pra visualizar o que existe. `infernal_room.txt` usa um tileset "gelo tingido de vermelho" gerado via script (ver abaixo) como prova de conceito de reskin por cor.

### Inimigos custom (`enemies/`)
Todos são `INCLUDE` de um inimigo-base do `fantasycore` + stats próprios + `categories=` auto-referenciada (necessário: o Flare resolve `spawn=` por **categoria**, não por nome de arquivo — um inimigo sem uma categoria que bata exatamente com o nome usado no `spawn=` não spawna, silenciosamente):
- `weak_skeleton.txt`, `mid_skeleton.txt`, `strong_skeleton.txt` — tiers de dificuldade pro sistema de salas antigo, com `loot=loot/level_N.txt` escalando.
- `show_*.txt` (goblin, goblin_elite, skeleton_archer, skeleton_mage, zombie, antlion, antlion_fire, minotaur, wyvern) — versões estáticas (`speed=0, chance_pursue=0`) pra sala de exibição.

### Assets gerados
- `images/tilesets/tileset_infernal.png` + `tilesetdefs/tileset_infernal.txt`: tileset de neve com um tingimento vermelho (multiply blend) aplicado via Pillow, reaproveitando a geometria/coordenadas do `tileset_snowplains.txt` original. Prova que dá pra reskinar tilesets existentes por cor sem arte nova — útil pra gerar variedade rápida (ex: "área congelada" vira "área infernal").

### Engine override (`engine/resolutions.txt`)
Só sobrescreve `virtual_height` (trava o zoom da câmera, já que por padrão ele varia com o tamanho da janela). **Está em 960** (o menor valor da lista nativa `960,1080,1440` do fantasycore) — testamos 720 e 480 pra um zoom mais "retro"/próximo, mas ambos quebram proporções da UI (elementos ficam desalinhados/fora de escala), porque a interface do fantasycore/empyrean_campaign não foi desenhada pra virtual_height abaixo de 960. **Se quiser mesmo um zoom mais fechado**, o caminho certo é redesenhar os arquivos de `menus/` (posições em pixels absolutos) pra uma resolução menor — tentamos isso uma vez em `menus/inventory.txt`, deu errado (painel ficou maior que a tela) e foi revertido. Ficou pendente.

## O que foi tentado e abandonado (não repetir)
- **Sprite do soldado C&C via OpenRA extraction**: convertemos sprites do Red Alert (via OpenRA.Utility) pra usar como personagem. Deu muito trabalho (formato de animação errado, escala, âncora dos pés) e o resultado ficou visualmente destoante do resto do jogo (HD fantasia vs pixel art retro). **Decisão do usuário: abandonar, usar as classes/sprites nativos do Flare** (Brute/Scout/Adept). Os arquivos ainda existem no histórico do git da conversa mas não estão no mod atual.
- **`icon_size=32`** (achando que resolvia ícones grandes): quebra a matemática de extração de todos os ícones (cada `icon_size` define como o motor recorta sub-imagens das planilhas — não é só "tamanho de exibição"). Reverter sempre para `icon_size=64` se aparecer.
- Testar via Kali Linux por SSH: funciona mas é **muito lento** (netbook Atom N455, 2GB RAM) — inviável pra iteração rápida. WSL local acabou sendo a solução (depois de resolver o problema de pacote desatualizado acima).

## Arquitetura de rede (LAN → servidor) — status atual

**Contexto:** o `flare-engine` upstream **não tem nenhum código de rede nativo**. Tudo abaixo foi adicionado por cima, em `flare-engine/src/NetManager.{h,cpp}` + hooks em `main.cpp`/`GameStatePlay.cpp`/`SharedResources.{h,cpp}`/`Settings.{h,cpp}` — não é um mod, é código do motor mesmo (exige recompilar, ver "Como compilar e rodar" acima). Ambição de longo prazo do projeto: LAN hoje, servidor dedicado depois — o modelo escolhido (servidor autoritativo) já é compatível com os dois.

Biblioteca escolhida: **ENet** (não `SDL_net` — decisão tomada por já dar reliability/sequencing sobre UDP de graça). Dependência do sistema: `libenet-dev`.

### O que já funciona
- **Transporte validado cross-machine**: dois processos `flare` em máquinas físicas diferentes na mesma LAN trocam pacotes ENet corretamente (testado fedora ↔ Kali, incluindo firewall).
- **Multiplayer real (não só transporte) rodando dentro do jogo gráfico de verdade**, não mais um teste headless isolado:
  - `--net-host[=PORT]` (padrão 4650): joga normalmente e também hospeda o servidor.
  - `--net-join=HOST[:PORT]`: joga normalmente, conectando no host.
  - (Flags antigas `--net-server`/`--net-client` continuam existindo, mas são só o smoke test headless original — fora de `init()`/`mainLoop()`, não tem jogo gráfico. Úteis pra testar só o transporte sem precisar de display.)
- **Suporta múltiplos jogadores, não só 1-pra-1**: cada cliente recebe um `player_id` (o `connectID` do ENet ao conectar; `player_id=0` é reservado pro herói do host). O servidor faz *relay em estrela*: recebe a posição de cada cliente e retransmite pra todos os OUTROS clientes — ninguém fala direto com ninguém, só com o servidor.
- **Cada jogador remoto aparece com a skin real dele** (corpo + cabeça + item equipado em cada slot — arma, armadura, tudo), não um monstro placeholder. Mecanismo: `NetManager::sendAppearance()` manda (uma vez, por canal confiável) `gfx_base`/`gfx_head`/lista de gráficos por camada — os mesmos dados que `GameSlotPreview` usa pra desenhar o preview de personagem na tela de "Load Game". `GameStatePlay::sendOwnAppearance()` calcula essa lista com `GameSlotPreview::getPreviewGfx(menu->inv)` (refatorado pra fora de `loadGraphicsFromInventory`, que agora só chama isso + `loadGraphics`). Do lado de quem recebe, `GameStatePlay::syncRemoteEntities()` cria uma `StatBlock` mínima (só `gfx_base`/`gfx_head`/`direction`/`pos`) + um `GameSlotPreview` de verdade por `player_id`, atualiza posição/direção/animação (`stance`/`run`, direção via `Utils::calcDirection`) a cada tick, e destrói os dois quando o `player_id` some do relay. `GameSlotPreview::addRenders()` não seta `map_pos` sozinho (foi feito pra desenhar em posição fixa de tela, não no mapa) — isso é preenchido manualmente em `GameStatePlay::render()` antes de empurrar pro `mapr->render()`.
  - Igual à posição, o servidor faz relay em estrela: aparência de cada cliente é retransmitida pros outros, tagueada com o `player_id` de quem mandou. O servidor também **cacheia a última aparência de cada `player_id` (incluindo a própria dele, sob id 0)** e reenvia tudo pra quem acabou de conectar — sem isso, um jogador que já tinha mandado a aparência antes de outro conectar nunca seria visto por esse recém-chegado (bug real, encontrado e corrigido nesta sessão: o host manda a aparência dele assim que entra no jogo, quase sempre **antes** de qualquer cliente conectar, e o broadcast ia pro vazio).
  - A aparência é **reenviada sempre que o equipamento muda** (`checkEquipmentChange()`), e quem recebe recarrega os gráficos daquele jogador. Ela também leva o **nome do herói**, desenhado acima da barra de vida.
- **Ataques/skills sincronizam a animação**: `NetManager::sendAction()` manda um evento avulso (canal 3, confiável) toda vez que o herói local entra em `StatBlock::ENTITY_POWER`, carregando `Avatar::attack_anim` (ex: "swing", "cast"). `GameStatePlay::syncOwnAction()` detecta a transição (compara `cur_state` do tick anterior) e manda só uma vez por uso, não por tick. Do lado de quem recebe, `syncRemoteEntities()` aplica a animação na `GameSlotPreview` daquele `player_id` e **segura** ela (não deixa a lógica de posição sobrescrever pra `stance`/`run`) até `Animation::isCompleted()`. Mesmo relay em estrela dos outros eventos.
  - O projétil/hazard também aparece pra todos (`PowerVisualPacket`, só visual: `Hazard::cosmetic_only`, pra não dar dano duas vezes). O dano de verdade é tratado à parte, ver "Dano em combate" abaixo.
- **Dano em combate (co-op + PvP opcional)**:
  - Seu golpe num inimigo: o cliente calcula o dano no próprio proxy (XP/loot locais, cada um ganha o seu) e manda `EnemyHitPacket` pro host, que desconta da vida autoritativa — todo mundo vê a barra de vida do grupo inteiro.
  - Inimigo acertando jogador: o host testa os hazards dos inimigos contra a posição dos jogadores remotos (`EntityManager::net_targets`) e manda `PlayerHitPacket` pro alvo, que roda o `takeHit()` do próprio herói (defesa/esquiva/resistências valem normalmente).
  - **PvP (opcional, decidido pelo host)**: botão "PvP: Ligado/Desligado" na tela Multiplayer, antes de Hospedar, ou `--net-host --net-pvp`. A regra vai pros clientes junto com o `MapPacket`. Com PvP ligado, cada máquina testa os hazards do PRÓPRIO herói contra os outros jogadores (`HazardManager::pvp_targets`) e manda o golpe como `PlayerHitPacket` endereçado à vítima (cliente → host → vítima); a vítima aplica no próprio herói, igual golpe de inimigo. O host recusa golpes PvP se a regra estiver desligada.
  - Teste automático: `RD_NET_PVP_SELFTEST=1` no cliente faz o herói dele andar até o primeiro outro jogador e atacar uma vez por segundo. Rodar o host com `--net-host=PORTA --net-pvp` e conferir `NetManager: our hero was hit ... hp X -> Y` no log do host (validado: 13 golpes, 250 → 0; com PvP desligado, nenhum).
  - **Vida dos jogadores sincronizada**: o `TickPacket` (posição, todo tick) também leva vida atual/máxima e se está vivo. Cada jogador vê uma barra de vida sobre os outros (`GameStatePlay::renderRemotePlayerBars`), a animação de golpe ("hit") quando a vida cai e a pose de morte ("die") enquanto estiver morto. Jogador morto não é mais alvo de PvP nem da IA dos inimigos (antes eles continuavam atacando o corpo de um cliente morto).
  - Com `RD_NET_PVP_SELFTEST=<pasta>` (em vez de `=1`) o teste também salva `pvp_fighting.png` e `pvp_dead.png` na pasta.
- **A horda em si é compartilhada, não por-cliente** (host autoritativo): só o host roda o spawn/AI/combate normal de inimigos do mapa (`EntityManager::handleNewMap`, travado em `settings->net_join_target` vazio); um cliente pula esse spawn inteiro e em vez disso espelha o que o host reporta — cria uma `Entity` de verdade por `net_id` dentro do próprio `entitym->entities` (`GameStatePlay::syncRemoteEnemies`), pra que o `HazardManager` já existente acerte ela sem nenhuma mudança. Posição/animação/vida são forçadas pela rede a cada tick (`StatBlock::net_proxy`), então o proxy pula a IA normal (`EntityManager::logic()`) e só avança o frame de animação.
  - Como o proxy carrega o **mesmo arquivo de definição do inimigo** que a cópia real do host (`EnemySpawnPacket::type_filename` → `EntityManager::getEntityPrototype`), a fórmula de dano/resistência/loot é idêntica — só a vida atual difere, o que não muda quanto dano um golpe local calcula. Isso significa que o golpe do PRÓPRIO cliente já roda o `takeHit()`/`takeDamage()` real do motor, sem modificação — ou seja, **XP e loot já resolvem 100% local, por jogador**, igual single-player, sem precisar de mensagem de rede pra recompensa. Cada jogador ganha seu próprio drop, de graça, sem código dedicado de sync de loot.
  - O que PRECISA de ida-e-volta na rede é manter a barra de vida compartilhada consistente: quando o hazard de um cliente tira vida de um proxy, `sendEnemyHit()` reporta `(net_id, dano)` pro host (cliente→host, nunca retransmitido), e o host subtrai isso da sua `Entity` autoritativa **sem chamar `takeDamage()` de novo** (senão XP/loot seriam concedidos duas vezes).
  - **Limitação conhecida**: só inimigo *colocado no mapa* sincroniza. Criatura invocada em tempo real (`EntityManager::spawn()`, ex: poder de invocação do jogador) não é sincronizada ainda. Proxy também não bloqueia colisão no cliente — dá pra atravessar andando por cima.
  - **Pegadinha séria, já caímos nela**: um inimigo colocado via `[event] ... spawn=weak_skeleton,x,y` **NÃO** passa pelo loop sincronizado — isso vai por `EntityManager::spawn()` (o mesmo caminho de invocação), que não recebe `net_id`. Pra um inimigo de teste/mapa ser sincronizado de verdade, **tem que ser uma seção nativa `[enemy]`** (`category=`, `location=x,y,1,1`, `number=1`), que é o que `net_test_room.txt` usa agora. Com `[event] spawn=`, cada lado simula sua própria cópia independente — parece funcionar (o bicho aparece dos dois lados) mas é só coincidência visual; matar num lado não afeta o outro, e parece exatamente um "bug de sincronização de morte" sem ser um.
- **Duas correções reais encontradas testando isso ao vivo** (host+cliente na mesma máquina, `--net-host`/`--net-join`):
  1. `NetManager::connectToServer()` ainda pedia só **3 canais ENet** na conexão, mas o transporte já usa **6** (posição/aparência/ação/spawn-inimigo/estado-inimigo/hit-inimigo) — os canais 3-5 (toda sincronia de inimigo) ficavam silenciosamente inutilizáveis em qualquer conexão de cliente. Corrigido pra pedir 6.
  2. `GameStatePlay::~GameStatePlay()` não desconectava da rede. Como o `NetManager` só conecta/hospeda uma vez, no boot, sair do jogo (Save & Exit, trocar de personagem) deixava a sessão antiga conectada e congelada — o outro lado continuava vendo um personagem parado, indefinidamente. Agora desconecta ao sair do `GameStatePlay`.
- **Menu de Multiplayer no jogo** (`GameStateMultiplayer`, botão "Multiplayer" na tela de título): "Hospedar" sobe o servidor na porta 4650; "Conectar" usa um campo de texto com IP[:porta]. Não precisa mais saber os flags `--net-host`/`--net-join` de linha de comando pra testar. `connectToServer()` continua bloqueante (motor não tem rede assíncrona) — "Conectar" trava a tela brevemente numa tentativa falha, é limitação conhecida, documentada no header do arquivo. Quem entra por aqui é tratado como cliente pelo estado real da conexão (`NetManager::isClient()`), não pelo flag `--net-join` — antes, entrar pelo menu fazia o cliente gerar a própria horda e os próprios inimigos do mapa, além dos espelhados do host.
- **Log persistente, agora separado por papel**: `~/.config/flare/flare_log_host.txt` / `flare_log_client.txt` quando rodando via `--net-host`/`--net-join` (antes os dois processos escreviam no MESMO `flare_log.txt`, um truncando o outro — dificultava debugar exatamente quando mais precisava). Eventos de rede (conectou/desconectou/aparência recebida/ação/spawn/despawn/hit de inimigo) logados via `Utils::logInfo`/`logError`, todos de baixo volume (um por evento, não por tick) — pensados pra ficar sempre ligados e serem a PRIMEIRA coisa a checar ao investigar um bug de sync, antes de especular.
- **Auto-detecção de idioma** (não é rede, mas foi na mesma leva de mudanças em `Settings.cpp`): no primeiro launch sem `settings.txt`, detecta o idioma do SO (`$LANG` etc. no Linux/macOS, `GetUserDefaultLocaleName` no Windows) em vez de sempre cair em inglês.
- **Classe Scout trocada por Rogue** (não é rede, é conteúdo — `mods/random_dungeon/engine/classes.txt` + `mods/random_dungeon/powers/`): o Scout original **não conseguia atacar de jeito nenhum** — suas duas powers da actionbar (ids 42 e 50, de `empyrean_campaign/powers/categories/ranger.txt`) são `meta_power=true`, convenção do Flare pra "essa power só funciona se um item equipado a substituir via `replace_power`" — e nada no kit inicial do mod fazia essa substituição, então ficavam permanentemente inertes. Em vez de consertar essa corrente frágil de item→replace_power (e ainda ter que sincronizar projétil/hazard em rede, que não existe), o Scout virou **Rogue**: mesma posição na lista de classes, mas com uma power real e diretamente utilizável (`powers/rogue.txt`, id 5000, "Dagger Strike"), usando a Adaga inicial (item id 8, já usado pelo Brute) em vez do Estilingue. `classes.txt` e `powers.txt` são overrides de arquivo inteiro (o motor não faz merge desses, só substitui) — cópias completas do `empyrean_campaign` com só essa troca. Validado via log de boot (`PowerManager: Power IDs = 5000 reserved`, sem erro de parse), mas **não testado ao vivo em combate** (sessão sem controle de input).

### Protocolo e robustez (Step 7 em `NetManager.h`)
- **Mensagens com tipo**: todo pacote começa com um byte `NetMsgType`. Antes as mensagens eram reconhecidas pelo **tamanho**, e qualquer campo novo podia fazer duas colidirem.
- **Versão do protocolo**: o cliente manda `PROTOCOL_VERSION` ao conectar; um host de outra versão recusa ("O host usa outra versão do jogo" na tela Multiplayer) em vez de dessincronizar. **Sempre que mudar o formato de alguma mensagem, suba `PROTOCOL_VERSION`.**
- **Menos tráfego**: posição/vida e estado dos inimigos saem a `NET_SEND_HZ` (20/s), não a cada quadro; os estados de todos os inimigos de um tick vão juntos em poucos pacotes (`MSG_ENEMY_STATES`).
- **Movimento suave (interpolação)**: quem recebe guarda as últimas posições com o horário de chegada e desenha jogadores/inimigos remotos `INTERP_DELAY_MS` (100 ms) no passado, interpolando entre duas amostras reais. Saltos grandes (teleporte) não são "deslizados".
- **Sem pausa no multiplayer**: abrir o menu (ESC etc.) não congela mais o mundo — se o host pausasse, todo mundo travava e os clientes caíam por timeout.
- **Queda de conexão**: timeouts do ENet curtos (~4–10 s). Se o host fecha o jogo, avisa os clientes na hora; o cliente mostra "O host fechou o jogo / A conexão caiu", descarta os inimigos espelhados e recarrega o mapa pra continuar sozinho.
- **Busca na LAN**: o host responde a broadcasts UDP na porta 4651. Botão **"Buscar na rede"** na tela Multiplayer preenche o IP; na linha de comando, `./flare --net-discover` lista as partidas.
- **Minimapa**: os outros jogadores aparecem como pontos dourados.
- Referência usada: o mod "Exiled Kingdoms Multiplayer" (github.com/winlatorbrasil/Exiled-Kingdoms-Multiplayer) é só APK, sem código-fonte — aproveitamos as ideias da lista de recursos (busca na LAN, jogadores no mapa, interpolação, limpeza de desconexão), não código.
- Testes automáticos (host + cliente sem janela na mesma máquina): ver "Teste automático" em "Dano em combate"; o mesmo teste agora também tira um equipamento do cliente aos 5 s (o host deve logar `player X changed equipment`).

### Limitações conhecidas (não é produção-MMO ainda)
- **Sem área de interesse**: hoje o servidor retransmite a posição de todo mundo pra todo mundo, sempre. Pra escala de MMO de verdade isso precisa virar "só sincroniza quem tá perto".
- **Sem persistência**: não existe banco de dados nem estado de mundo salvo no servidor — é tudo em memória, morre quando o processo do host fecha.
- **Sem autenticação/anti-cheat**: qualquer um que saiba o IP:porta conecta. Ok pra LAN entre amigos, não pra internet aberta.

### Pegadinhas de teste que valem a pena lembrar
- O `NetManager` só processa a rede (`pollGame()`) dentro de `GameStatePlay::logic()` — ou seja, **o host precisa estar realmente jogando** (não na tela de título/cutscene) antes de um cliente tentar conectar, senão o handshake trava e o cliente dá timeout em 5s.
- Firewall (Fedora/firewalld): `sudo firewall-cmd --add-port=4650/udp --zone=public` (sem `--permanent` não sobrevive reboot).
- Pra testar sem precisar de uma segunda máquina: dá pra rodar **duas instâncias do jogo na mesma máquina**, uma `--net-host` e outra `--net-join=127.0.0.1:PORT`. O Flare tem uma trava de "instância já rodando" que abre um diálogo zenity (Continue/Reset) — só clicar em Continue.
- Se for testar assim, vale desligar `pause_on_focus_loss` e `mute_on_focus_loss` em `~/.config/flare/settings.txt` (senão a janela que perde foco pausa/muta sozinha, atrapalha alternar entre as duas). **Cuidado**: só edite esse arquivo com o jogo fechado — se algum processo do Flare ainda estiver rodando, ele sobrescreve o arquivo com os valores antigos (em memória) na saída.
- Mapa de teste dedicado: `maps/net_test_room.txt` (sala fixa 16x16, 2 esqueletos via `[enemy]` nativo, sem geração procedural) — `maps/spawn.txt` apontava pra ela durante os testes de rede; agora aponta pro mundo aberto (`maps/world/aurora_a.txt`). Pra voltar ao fluxo normal (masmorra procedural), troca o `intermap=` de volta em `spawn.txt`. **Se for adicionar mais inimigo de teste num mapa, usa `[enemy]`, nunca `[event] spawn=`** (ver pegadinha da horda compartilhada acima).

### Em aberto no momento da entrega (reportado pelo usuário, não investigado a fundo ainda)
- (resolvido) "Ataques não são vistos entre os jogadores": projéteis/hazards agora aparecem pra todos (`PowerVisualPacket`) e o dano conta (ver "Dano em combate").
- **"Inventário ainda bugado"**: reportado antes nesta sessão como possível choque de estilo (personagem pixelado no mapa vs. ícones de item em alta definição no inventário — ver commit de pixel art), mas o usuário não confirmou se é isso ou outra coisa (posição de item, texto cortado, etc.). Sem repro exato ainda.

### Próximos passos sugeridos (não implementado, é só orientação)
1. **Testar o combate de verdade em jogo** (host + cliente, os dois com Brute/Rogue/Adept) — a troca do Scout pelo Rogue só foi validada por log de boot, não jogada. Se ainda tiver erro de ataque, é aqui que precisa olhar primeiro.
2. (feito) Vida dos jogadores sincronizada, com barra de vida e pose de morte.
3. (feito) Reenviar a aparência quando o equipamento muda.
4. (feito) Interpolação de posição entre pacotes.
5. Se for mesmo pra MMO: área de interesse (só sincronizar entidades próximas), taxa de tick do servidor separada da taxa de render, e um plano de persistência (banco de dados) — isso é uma conversa de arquitetura em si, não um ajuste incremental no que já existe.

### Erros pré-existentes ainda não investigados (não é rede, aparecem em todo boot)
- `ERROR: ItemStack: Item id is 753, but quantity is zero.` / `Item id is 2002, but quantity is zero.` — vem de algum save/stash com item id + quantidade inconsistente. Presente desde antes desta sessão de rede; não sei se é save de teste corrompido (slots 1/2, mexidos bastante durante os testes) ou algo estrutural no mod.
- `ERROR: SDLHardwareRenderDevice: Couldn't load image: 'images/menus/game_over.png'.` — asset faltando (não existe em `fantasycore`/`empyrean_campaign`/`random_dungeon`). Cosmético (só afeta a tela de game over), mas ainda não corrigido.

## Créditos de música
- `music/anguish.ogg` — "Anguish" de Kevin MacLeod (incompetech.com), licença Creative Commons Attribution 3.0 (https://creativecommons.org/licenses/by/3.0/). Usada nos menus (`engine/default_music.txt`). A licença exige manter essa atribuição.
- A trilha antiga dos menus (`fantasycore/music/title_theme.ogg`) passou a tocar na masmorra (`maps/dungeon.txt`, `music=`).

## Conteúdo baseado no Exiled Kingdoms (itens, loot, inimigos, classes, mundo aberto)

Tudo abaixo é **gerado por script a partir de planilhas**. Pra mudar qualquer coisa: edite o CSV e rode o script de novo. Os arquivos gerados começam com `# GENERATED` — não edite eles na mão.

| O que mexer | Onde | Depois rode |
|---|---|---|
| Itens (nome, nome PT, nível, dano, armadura, bônus, filtro de cor) | `tools/ek_data/items.csv` | `python3 tools/gen_content.py` |
| **Chance de loot** por tipo de inimigo | `tools/ek_data/loot_bands.csv` | `python3 tools/gen_content.py` |
| Inimigos (força, sprite, filtro de cor, resistências) | `tools/ek_data/enemies.csv` | `python3 tools/gen_content.py` |
| Filtros de cor ("skins") | `PALETTE` em `tools/gen_content.py` | `python3 tools/gen_content.py` |
| Regiões e áreas do mundo aberto | `tools/world/regions.csv`, `tools/world/zones.csv` | `python3 tools/gen_world.py` |
| **Conferir equilíbrio/inconsistências** (só lê, não altera nada) | — | `python3 tools/balance_check.py` |
| Biomas / NPCs das cidades | `BIOMES` / `TOWNS` em `tools/gen_world.py` | `python3 tools/gen_world.py` |

`tools/ek_data/bootstrap_from_wiki.py` recria `items.csv`/`enemies.csv` do zero a partir das tabelas do wiki do EK guardadas em `tools/ek_data/wiki_cache/` (pede `--force`, porque apaga suas edições). `tools/render_map.py maps/world/x.txt saida.png` desenha um mapa isométrico offline, pra conferir sem abrir o jogo.

**Depois de clonar o repo**: rode `python3 tools/gen_world.py` uma vez — os tilesets de bioma (`images/tilesets/rd_*.png`, ~5–9 MB cada) ficam fora do git e são recriados por ele.

### Itens (215, ids 5000+)
- Armas por material, como no EK: **Ferro** (nv 1) → **Aço** (4) → **Prata** (6) → **Aço Azul** (9) → **Adamantita** (13, rara). 9 tipos: adaga, espada curta, espada longa, espadão, machado, machado de guerra, maça, martelo, marreta. Espadão/machado de guerra/marreta são de duas mãos (desativam o escudo).
- ~47 armas mágicas/arcos/varinhas/cajados e 22 únicas do EK, com dano elemental (Fogo, Gelo, Raio, Trevas).
- 15 conjuntos de armadura (Couro, Cota de Malha, Placas, Legião, Assassino, Cinzas...) + escudos, anéis, colares e cintos. 8 conjuntos dão bônus por peças (`items/sets.txt`).
- Cada material/elemento tem um filtro de cor aplicado **no ícone e no sprite equipado** no herói (espada de prata aparece prateada no boneco).
- Conversão EK → Flare: dano médio = (7 + 3×nível) × tipo × material, com a proporção mín/máx do item original; velocidade do EK vira bônus de crítico; vida/mana do EK ×4; resistências 1:1. Detalhes no topo do bootstrap.
- Ícones: o jogo usa os ícones do Flare tingidos (`images/icons/icons_rd.png`). O mod **`ek_icons`** (fora do git, em `flare-game/mods/ek_icons`) troca pelos ícones originais do wiki do EK — **uso pessoal apenas**, a arte é do Exiled Kingdoms; não distribua. Liga/desliga no menu de Mods.
- Nomes em português via `languages/data.pt_BR.po` (coluna `name_pt`). O jogo mostra em PT quando o idioma estiver em pt_BR.

### Loot
Como o Flare sorteia: a cada tentativa de drop sai **um** número de 0 a 100; ganha o item de **menor** chance que ainda seja ≥ ao número. Então as colunas de `loot_bands.csv` funcionam como faixas: com `gold=55, potion=30, common=18, uncommon=7, rare=2.5, unique=0.8` o inimigo normal dá 0,8% única, 1,7% rara, 4,5% incomum, 11% comum, 12% poção, 25% ouro, 45% nada. `drops_min/max` = tentativas por morte. Itens só caem se o nível do jogador estiver entre `nível do item - level_below` e `+ level_above`. O stat `item_find` do jogador multiplica tudo.

### Inimigos (63, `enemies/rd_*.txt`)
Famílias do EK mapeadas nos sprites do Flare com filtro de cor: goblins, orcs e hobgoblins, esqueletos, zumbis/múmias/fantasmas (translúcidos), mirmeks e aranhas, minotauros, dracos/dragões, demônios. Modelo "il": stats de nível 1 + crescimento por nível, então escalam com o nível do herói (masmorra) ou da onda (horda). Categorias pra spawn: `rd_t1`..`rd_t6` (faixa de nível do EK), família (`rd_goblins`, `rd_skeletons`...), `rd_normal`/`rd_elite`/`rd_boss`, `rd_all`. Todos os não-chefes também entram em `il_common`, então aparecem na masmorra procedural. Pra horda (`HordeManager`, ainda não commitado), um `engine/horde.txt` usaria algo como `tier=rd_t1,0,10`, `tier=rd_t2,3,8`, ..., `tier=rd_boss,10,1`.

### Classes (como no EK)
Guerreiro (espada longa de ferro + broquel), Ladino (adaga + arco curto na mochila; "Atirar" na barra funciona com arco equipado), **Clérigo** (novo: maça + broquel, Golpe Sagrado e Cura — `powers/cleric.txt`), Mago (varinha de carvalho). Saves antigos de "Brute"/"Adept" carregam, só não acham a classe pros stats base.

### Mundo aberto (`maps/world/`)
Inspirado no EK (grade de áreas ligadas pelas bordas, cada uma com faixa de nível, cidades e masmorras), mas com mundo próprio: cada povo tem sua região e bioma.

```
        col 0              col 1              col 2               col 3              col 4               col 5
row 0   Fenda do Uivo      Campos de Gelo     Portões do Planalto Trono de Cinzas    Encosta Fumegante   Coração do Vulcão
        Geleira 13-16 ☠    Geleira 10-13 ⛏    Minotauros 11-14 ⛏  Minotauros 13-16 ☠ Vulcão 14-17 ⛏      Vulcão 16-19 ☠
row 1   Jardins Proibidos  TORRE DE VIDRO     Brejo dos Ossos     Lodo Eterno        FORTE DA FRONTEIRA  Acamp. de Guerra
        Magos 9-12 ⛏       Magos 8-11 🏠      Mortos 5-8 ⛏        Mortos 7-10 ☠      Orcs 7-10 🏠        Orcs 9-12 ☠⛏
row 2   Coração do Bosque  Trilha das Sombras AURORA (início)     Campos da Aurora   Mar de Areia        Colmeia de Âmbar
        Goblins 4-7 ☠⛏     Goblins 2-5        Humanos 1-2 🏠      Humanos 1-3 ⛏      Mirmeks 4-7         Mirmeks 6-9 ☠⛏
```
🏠 cidade com vendedores · ⛏ entrada de masmorra (procedural; a escada de saída volta pra caverna) · ☠ chefe da região.

- Biomas = tileset do Flare + filtro de cor: pradaria, floresta escura, pântano, bosque arcano (violeta), planalto cinzento, geleira, deserto, terras rubras, vulcânico.
- Cidades: Aurora (ferreiro, alquimista, ancião que explica o mundo), Torre de Vidro (arcanista vende varinhas/cajados/anéis), Forte da Fronteira. Estoque dos vendedores acompanha o nível do jogador (`loot/rd_vendor_*.txt`).
- Terreno é aleatório mas com semente fixa por área (rodar de novo dá o mesmo mapa; mude `SEED_SALT` pra sortear outro).
- **Como entrar**: todo jogo novo começa em Aurora (`maps/spawn.txt`). A sala de teste de rede (`maps/net_test_room.txt`) continua existindo, com um portal pro mundo; pra testar rede nela de novo, troque o `intermap=` do `spawn.txt` de volta.
- Inimigos respawnam ao reentrar na área (padrão do Flare), parecido com o respawn por tempo do EK.
- Ainda não tem: o mundo nas outras faixas do EK, missões, waypoints/viagem rápida, seleção de modo (Run infinita x Mundo aberto) no menu.

## Modos de jogo, Kit Dev e Códice (wiki)

### Modos (tela de título)
Botões **Mundo Aberto**, **Run Infinita** e **Sala de Teste**. Os três passam pela tela normal de personagens (criar ou carregar); o modo só decide onde você entra (`GameStatePlay::modeStartMap` no motor):
- Mundo Aberto: novo jogo em Aurora (`maps/spawn.txt`); save carregado continua de onde parou (se estava numa arena ou na sala de teste, volta pra Aurora).
- Run Infinita: `maps/run/start.txt` sorteia uma das 8 arenas (`maps/run/arena_<bioma>.txt`, uma por bioma) e a horda (`HordeManager` + `engine/horde.txt`) começa. Portal no meio da arena volta pra Aurora.
- Sala de Teste: `maps/dev_room.txt`, com portais pro mundo e pra run.
- Linha de comando: `./flare --mode=run|test|world` (com `--load-slot=N` entra direto).
- Posições dos botões: `run_pos` / `test_pos` em `menus/gametitle.txt` (o tema dark fantasy já tem).

### Kit Dev (motor: `src/MenuDevKit.*`)
Na sala de teste (e na sala de teste de rede) aparece um botão **Kit Dev** no topo da tela. Abas:
- **Monstros**: lista todos os `rd_*`, escolhe o nível, spawna 1 ou 5, **Boneco de Treino** (`enemies/rd_dummy.txt`, 1.000.000 de vida, parado) com medidor de dano total, DPS dos últimos 5 s e maior golpe; **Matar Todos** (poupa os bonecos).
- **Itens**: busca por nome ou id, pega 1 ou 10, +1000 de ouro, +10 poções.
- **Classe**: troca de classe na hora (reaplica atributos, kit, poderes e barra; o equipamento atual vai pra mochila).
- **Herói**: pontos de atributo/poder infinitos, modo deus, +1/+5 níveis, curar.
Autoteste sem precisar clicar: `RD_DEVKIT_SELFTEST=/pasta SDL_VIDEODRIVER=offscreen ./flare --load-slot=1 --mode=test` (resultado no log, prints de cada aba na pasta). O motor também ganhou `RenderDevice::screenshot_request` (salva o próximo quadro em PNG).

### Códice (wiki editável)
Página publicada com os 215 itens, 64 monstros e as tabelas de loot, com banco compartilhado: você edita lá, salva, e a entrada fica **pendente**. Pra aplicar, peça ao Claude: ele lê as pendentes do banco, roda `tools/wiki_apply.py <pasta> --write` (atualiza os CSVs; as "anotações" ele interpreta à parte), regenera com `gen_content.py`/`gen_world.py` e marca como aplicadas.
- Exportar/atualizar a página: `python3 tools/wiki_export.py tools/wiki/build` (gera `catalog.json`, `icons.png`, `enemies.png` e um JSON por registro) e republicar `tools/wiki/index.html`.
- Os ícones da página são a arte do Flare tingida (a do EK não sai do seu computador).

## Interface (tema único), controles e Android

### Tema visual (mod `darkfantasy_gui`, no repo do flare-engine)
Toda a interface — botões, abas, listas, HUD (placa de vida/mana/XP, barra de ações, minimapa, barra do inimigo), painéis (inventário, personagem, poderes, diário, loja/baú, pausa/config), fichas de save, logo e fundo dos menus — é **gerada por um script**: `mods/darkfantasy_gui/tools/gen_theme.py`. Estilo: dark fantasy em **pixel art**, vermelho/preto/dourado. Tudo é desenhado em meia resolução com uma paleta fixa e ampliado sem suavização, então todas as peças usam a mesma grade de pixel.
- Mudou cor, moldura, ícone? Edite o script e rode `python3 tools/gen_theme.py` (a opção `--preview` gera uma folha com tudo).
- Os layouts do HUD e do inventário (`menus/*.txt` do tema) são escritos pelo mesmo script, pra arte e posição nunca se desalinharem.
- Fontes em pixel: Jersey 10 (texto) e Jacquard 12 (títulos), licença SIL OFL (`fonts/OFL.txt`). Elas só ficam nítidas em múltiplos da grade: Jersey em 20/30/40 e Jacquard em 24/36/48.
- Assinatura **"Random Dungeon vX - RedByte"** no canto dos menus e da pausa (`GameState::renderSignature`).
- Revisar sem clicar: `RD_UI_SHOTS=<pasta>` (painéis no jogo) e `RD_STATE_SHOTS=<pasta>` (título/carregar/novo/multiplayer/config), com `SDL_VIDEODRIVER=offscreen`.

### Celular (toque)
`MenuTouchControls` (motor), multitoque:
- **Joystick flutuante** na esquerda: onde o dedão encostar vira o centro (8 direções).
- **Botões na direita**: ataque grande (slot M1), 3 habilidades (M2, 3, 4) e 2 poções (1, 2), com o ícone do que estiver no slot. As habilidades **miram sozinhas no inimigo mais próximo** (`MenuActionBar::touchAimTarget`).
- **Pausa** ao lado do minimapa.
- Barra de ações do celular: `menus/actionbar_touch.txt` (slots 5–8 + menus). Os slots dos botões de toque ficam **embaixo** deles: com o menu de Poderes aberto os botões somem e dá pra arrastar habilidades pra eles.
- Teste sem celular: `touch_controls=1` no settings.txt + `RD_TOUCH_TEST=<pasta>` (dedos simulados; `RD_TOUCH_DELAY=<quadros>` pra esperar a horda).

### Controle (gamepad)
Suporte nativo do Flare via SDL GameController (analógico anda, gatilho direito ataca, START pausa, direcional navega nos menus). Conectar um controle com o jogo aberto já passa a usá-lo, e funciona também no Android. Teste: `RD_PAD_TEST=<pasta>` (controle virtual do SDL).

### Android (APK)
Projeto em `flare-engine/flare-android-project`:
```bash
cd flare-engine/flare-android-project
./setup_android_deps.sh        # SDK/NDK em ~/Android/Sdk, fontes SDL2/ENet em ~/Android/src (só na 1ª vez)
python3 pack_data.py           # empacota os mods (fantasycore, empyrean_campaign, random_dungeon, darkfantasy_gui) + ícone
./gradlew assembleDebug        # -> app/build/outputs/apk/debug/app-debug.apk
```
- Só **arm64** (todo celular atual). ID do app: `com.redbyte.randomdungeon`.
- Os dados vão dentro do APK e são copiados pro armazenamento do app na **primeira abertura**, ou quando uma atualização traz dados novos (`src/AndroidData.cpp`). A primeira abertura demora e fica com a tela preta enquanto copia.
- **Crossplay**: o mesmo protocolo de rede (ENet/UDP, `PROTOCOL_VERSION`) em Android, Windows e Linux; celular e PC entram na mesma partida pela LAN ou por IP.
- `ek_icons` **não** entra no APK (é arte do Exiled Kingdoms, uso pessoal).
- Tamanho: ~700 MB de dados (a maior parte PNG de personagens/inimigos do fantasycore). Reduzir é o próximo passo: tirar inimigos/equipamentos que o mod não usa e comprimir os PNG.

## Equilíbrio de itens e monstros

`tools/balance_check.py` lê as planilhas e o mundo gerado e lista inconsistências: item de nível maior mais fraco que um de nível menor do mesmo tipo, item raro mais fraco que um comum, dano longe da curva do nível, arma de duas mãos sem bloquear a outra mão, tipo de dano errado, monstro fora da faixa, elite/chefe fraco demais, monstro nascendo longe do próprio nível, tabela de loot invertida. Rode depois de mexer nas planilhas; o objetivo é 0 pendências.

**Regra importante dos monstros:** `hp_mult`/`dmg_mult` em `enemies.csv` são **relativos ao próprio nível EK** do monstro (vêm do EK normalizado pelo nível). Por isso o mundo gerado só coloca numa área os monstros da região cujo nível EK está na faixa da área (`EK_BAND` em `gen_world.py`: de 3 abaixo a 2 acima) e cada um nasce no próprio nível EK (limitado à faixa). As masmorras usam uma categoria por área (`rd_dg_<área>`) com a mesma regra. Antes disso, um Orc Recruta (EK 2) nascia no nível 10 das Terras Rubras mais forte que um Orc Soldado (EK 10).

Polimento de 2026-09-27: cajados grandes viraram armas de duas mãos; armaduras raras que protegiam menos que comuns de nível menor foram ajustadas; dano/vida de 6 monstros muito acima dos vizinhos de nível foram limitados (Orc Atirador, Esqueleto Arqueiro, Aranha Monstruosa, Dracos de Fogo/Gelo, Minotauro); as classes começam com roupa do Random Dungeon (couro; aprendiz para o mago) em vez da roupa de pano do Flare; Ermos Vulcânicos ganharam o Herói Caído. No motor: `requires_item=<id>,0` ("precisa ter, não consome") era descartado — a habilidade de poções funcionava sem o Pilão e Almofariz e o erro `ItemStack ... quantity is zero` aparecia em todo boot.
