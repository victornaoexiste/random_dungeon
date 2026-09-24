# Random Dungeon — status do projeto

Mod para o [Flare Engine](https://github.com/flareteam/flare-engine) (ARPG isométrico open source). Objetivo: um "horda-survival" roguelite (estilo Vampire Survivors / Hades) com masmorra procedural, inimigos e loot aleatórios escalando por nível. Ambição de longo prazo: multiplayer LAN, depois servidor online.

Este documento existe pra qualquer sessão nova (humana ou IA) retomar o trabalho sem precisar redescobrir tudo que já foi resolvido.

## Estrutura no disco

```
C:\Users\poiso\Projects\
├── flare-engine\        clone git oficial (github.com/flareteam/flare-engine) — o MOTOR (C++)
│   ├── build_wsl\       build compilado dentro do WSL (Release) — NAO portar, recompilar no destino
│   └── mods\            mods\default e mods\gcw0_defaults sao do proprio motor;
│                        fantasycore/empyrean_campaign/random_dungeon sao SYMLINKS pra flare-game\mods\
├── flare-game\          clone git oficial (github.com/flareteam/flare-game) — o CONTEUDO (mapas, sprites, itens)
│   └── mods\
│       ├── fantasycore\       mecanica/assets base (nao mexemos)
│       ├── empyrean_campaign\ campanha padrao, fonte dos inimigos/itens/tilesets que reaproveitamos (nao mexemos)
│       └── random_dungeon\         <<< NOSSO MOD. Tudo que criamos esta aqui, isolado.
└── assets\              scripts/arquivos de trabalho descartaveis (nao e parte do jogo)
```

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
- **`spawn.txt`**: ponto de entrada de todo novo jogo, redireciona pra `dungeon.txt`.
- **`dungeon.txt` + `procgen_rules/random_dungeon.txt`**: masmorra procedural (geração aleatória nativa do Flare, `procgen_filename`). Reaproveita os "chunks" de sala prontos da campanha (`empyrean_campaign/maps/iron_labyrinth/room*.txt`, `links*.txt`, `door_*.txt`) e o tileset `tileset_ruins`. Configurado pra mapa **pequeno** (`main_path_length_min=3, max=6`, `doors_max=1`) — ajustar em `random_dungeon.txt` se quiser maior.
  - O sistema de inimigo aleatório nativo (`[enemy]` com `category=`, `number=min,max`, `spawn_level=hero_level,N`) já é usado pelos próprios chunks da campanha (ex: `category=il_common` sorteia entre 16 tipos de inimigo). **Ainda não trocamos `il_common` pela nossa própria categoria** — isso é um próximo passo óbvio se quiser inimigos temáticos custom em vez dos da campanha padrão.
- **`room1.txt` / `room2.txt` / `room3.txt`**: sistema ANTERIOR de salas fixas com dificuldade crescente (fraco→médio→forte). Não está mais no fluxo principal (spawn.txt aponta pra dungeon.txt agora), mas os arquivos continuam aqui, funcionais, caso queira comparar ou reaproveitar.
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
  - **Limitação que continua existindo**: a aparência só é mandada **uma vez, ao conectar** (o cache resolve "chegou atrasado", não "mudou depois"). Trocar de equipamento no meio da sessão não atualiza o que os outros veem — reenviar em `checkEquipmentChange()` é o próximo passo óbvio aqui.
- **Ataques/skills sincronizam a animação** (não o dano): `NetManager::sendAction()` manda um evento avulso (canal 3, confiável) toda vez que o herói local entra em `StatBlock::ENTITY_POWER`, carregando `Avatar::attack_anim` (ex: "swing", "cast"). `GameStatePlay::syncOwnAction()` detecta a transição (compara `cur_state` do tick anterior) e manda só uma vez por uso, não por tick. Do lado de quem recebe, `syncRemoteEntities()` aplica a animação na `GameSlotPreview` daquele `player_id` e **segura** ela (não deixa a lógica de posição sobrescrever pra `stance`/`run`) até `Animation::isCompleted()`. Mesmo relay em estrela dos outros eventos.
  - **Limitação**: só a animação sincroniza. Sem projétil, sem hazard, sem dano — um ataque de um jogador remoto não machuca ninguém aqui ainda. É o próximo passo real pra um sistema de combate de verdade.
- **A horda em si é compartilhada, não por-cliente** (host autoritativo): só o host roda o spawn/AI/combate normal de inimigos do mapa (`EntityManager::handleNewMap`, travado em `settings->net_join_target` vazio); um cliente pula esse spawn inteiro e em vez disso espelha o que o host reporta — cria uma `Entity` de verdade por `net_id` dentro do próprio `entitym->entities` (`GameStatePlay::syncRemoteEnemies`), pra que o `HazardManager` já existente acerte ela sem nenhuma mudança. Posição/animação/vida são forçadas pela rede a cada tick (`StatBlock::net_proxy`), então o proxy pula a IA normal (`EntityManager::logic()`) e só avança o frame de animação.
  - Como o proxy carrega o **mesmo arquivo de definição do inimigo** que a cópia real do host (`EnemySpawnPacket::type_filename` → `EntityManager::getEntityPrototype`), a fórmula de dano/resistência/loot é idêntica — só a vida atual difere, o que não muda quanto dano um golpe local calcula. Isso significa que o golpe do PRÓPRIO cliente já roda o `takeHit()`/`takeDamage()` real do motor, sem modificação — ou seja, **XP e loot já resolvem 100% local, por jogador**, igual single-player, sem precisar de mensagem de rede pra recompensa. Cada jogador ganha seu próprio drop, de graça, sem código dedicado de sync de loot.
  - O que PRECISA de ida-e-volta na rede é manter a barra de vida compartilhada consistente: quando o hazard de um cliente tira vida de um proxy, `sendEnemyHit()` reporta `(net_id, dano)` pro host (cliente→host, nunca retransmitido), e o host subtrai isso da sua `Entity` autoritativa **sem chamar `takeDamage()` de novo** (senão XP/loot seriam concedidos duas vezes).
  - **Limitação conhecida**: só inimigo *colocado no mapa* sincroniza. Criatura invocada em tempo real (`EntityManager::spawn()`, ex: poder de invocação do jogador) não é sincronizada ainda. Proxy também não bloqueia colisão no cliente — dá pra atravessar andando por cima.
  - **Pegadinha séria, já caímos nela**: um inimigo colocado via `[event] ... spawn=weak_skeleton,x,y` **NÃO** passa pelo loop sincronizado — isso vai por `EntityManager::spawn()` (o mesmo caminho de invocação), que não recebe `net_id`. Pra um inimigo de teste/mapa ser sincronizado de verdade, **tem que ser uma seção nativa `[enemy]`** (`category=`, `location=x,y,1,1`, `number=1`), que é o que `net_test_room.txt` usa agora. Com `[event] spawn=`, cada lado simula sua própria cópia independente — parece funcionar (o bicho aparece dos dois lados) mas é só coincidência visual; matar num lado não afeta o outro, e parece exatamente um "bug de sincronização de morte" sem ser um.
- **Duas correções reais encontradas testando isso ao vivo** (host+cliente na mesma máquina, `--net-host`/`--net-join`):
  1. `NetManager::connectToServer()` ainda pedia só **3 canais ENet** na conexão, mas o transporte já usa **6** (posição/aparência/ação/spawn-inimigo/estado-inimigo/hit-inimigo) — os canais 3-5 (toda sincronia de inimigo) ficavam silenciosamente inutilizáveis em qualquer conexão de cliente. Corrigido pra pedir 6.
  2. `GameStatePlay::~GameStatePlay()` não desconectava da rede. Como o `NetManager` só conecta/hospeda uma vez, no boot, sair do jogo (Save & Exit, trocar de personagem) deixava a sessão antiga conectada e congelada — o outro lado continuava vendo um personagem parado, indefinidamente. Agora desconecta ao sair do `GameStatePlay`.
- **Menu de Multiplayer no jogo** (`GameStateMultiplayer`, botão "Multiplayer" na tela de título): "Hospedar" sobe o servidor na porta 4650; "Conectar" usa um campo de texto com IP[:porta]. Não precisa mais saber os flags `--net-host`/`--net-join` de linha de comando pra testar. `connectToServer()` continua bloqueante (motor não tem rede assíncrona) — "Conectar" trava a tela brevemente numa tentativa falha, é limitação conhecida, documentada no header do arquivo.
- **Log persistente, agora separado por papel**: `~/.config/flare/flare_log_host.txt` / `flare_log_client.txt` quando rodando via `--net-host`/`--net-join` (antes os dois processos escreviam no MESMO `flare_log.txt`, um truncando o outro — dificultava debugar exatamente quando mais precisava). Eventos de rede (conectou/desconectou/aparência recebida/ação/spawn/despawn/hit de inimigo) logados via `Utils::logInfo`/`logError`, todos de baixo volume (um por evento, não por tick) — pensados pra ficar sempre ligados e serem a PRIMEIRA coisa a checar ao investigar um bug de sync, antes de especular.
- **Auto-detecção de idioma** (não é rede, mas foi na mesma leva de mudanças em `Settings.cpp`): no primeiro launch sem `settings.txt`, detecta o idioma do SO (`$LANG` etc. no Linux/macOS, `GetUserDefaultLocaleName` no Windows) em vez de sempre cair em inglês.
- **Classe Scout trocada por Rogue** (não é rede, é conteúdo — `mods/random_dungeon/engine/classes.txt` + `mods/random_dungeon/powers/`): o Scout original **não conseguia atacar de jeito nenhum** — suas duas powers da actionbar (ids 42 e 50, de `empyrean_campaign/powers/categories/ranger.txt`) são `meta_power=true`, convenção do Flare pra "essa power só funciona se um item equipado a substituir via `replace_power`" — e nada no kit inicial do mod fazia essa substituição, então ficavam permanentemente inertes. Em vez de consertar essa corrente frágil de item→replace_power (e ainda ter que sincronizar projétil/hazard em rede, que não existe), o Scout virou **Rogue**: mesma posição na lista de classes, mas com uma power real e diretamente utilizável (`powers/rogue.txt`, id 5000, "Dagger Strike"), usando a Adaga inicial (item id 8, já usado pelo Brute) em vez do Estilingue. `classes.txt` e `powers.txt` são overrides de arquivo inteiro (o motor não faz merge desses, só substitui) — cópias completas do `empyrean_campaign` com só essa troca. Validado via log de boot (`PowerManager: Power IDs = 5000 reserved`, sem erro de parse), mas **não testado ao vivo em combate** (sessão sem controle de input).

### Limitações conhecidas (não é produção-MMO ainda)
- Aparência só sincroniza **uma vez ao conectar** (ver acima) — trocar de arma/armadura no meio do jogo não propaga pros outros ainda.
- **Sem interpolação/suavização** entre updates de posição — com latência real (internet, não LAN) vai parecer "pulando".
- **Ataques sincronizam só a animação, não o dano** (ver acima) — pré-requisito pro sistema de combate de verdade.
- **Sem área de interesse**: hoje o servidor retransmite a posição de todo mundo pra todo mundo, sempre. Pra escala de MMO de verdade isso precisa virar "só sincroniza quem tá perto".
- **Sem persistência**: não existe banco de dados nem estado de mundo salvo no servidor — é tudo em memória, morre quando o processo do host fecha.
- **Sem autenticação/anti-cheat**: qualquer um que saiba o IP:porta conecta. Ok pra LAN entre amigos, não pra internet aberta.

### Pegadinhas de teste que valem a pena lembrar
- O `NetManager` só processa a rede (`pollGame()`) dentro de `GameStatePlay::logic()` — ou seja, **o host precisa estar realmente jogando** (não na tela de título/cutscene) antes de um cliente tentar conectar, senão o handshake trava e o cliente dá timeout em 5s.
- Firewall (Fedora/firewalld): `sudo firewall-cmd --add-port=4650/udp --zone=public` (sem `--permanent` não sobrevive reboot).
- Pra testar sem precisar de uma segunda máquina: dá pra rodar **duas instâncias do jogo na mesma máquina**, uma `--net-host` e outra `--net-join=127.0.0.1:PORT`. O Flare tem uma trava de "instância já rodando" que abre um diálogo zenity (Continue/Reset) — só clicar em Continue.
- Se for testar assim, vale desligar `pause_on_focus_loss` e `mute_on_focus_loss` em `~/.config/flare/settings.txt` (senão a janela que perde foco pausa/muta sozinha, atrapalha alternar entre as duas). **Cuidado**: só edite esse arquivo com o jogo fechado — se algum processo do Flare ainda estiver rodando, ele sobrescreve o arquivo com os valores antigos (em memória) na saída.
- Mapa de teste dedicado: `maps/net_test_room.txt` (sala fixa 16x16, 2 esqueletos via `[enemy]` nativo, sem geração procedural) — `maps/spawn.txt` aponta pra ela em vez de `maps/dungeon.txt` enquanto os testes de rede estiverem em andamento. Pra voltar ao fluxo normal (masmorra procedural), troca o `intermap=` de volta em `spawn.txt`. **Se for adicionar mais inimigo de teste num mapa, usa `[enemy]`, nunca `[event] spawn=`** (ver pegadinha da horda compartilhada acima).

### Em aberto no momento da entrega (reportado pelo usuário, não investigado a fundo ainda)
- **"Ataques não são vistos entre os jogadores"**: os logs confirmam que o evento de ação chega (`NetManager: player X action 'swing'`), e a animação é sincronizada (ver acima) — mas isso é só a animação do CORPO/arma balançando. Se o que falta é o **efeito visual do golpe em si** (hazard/projétil, faísca de impacto etc.), isso é esperado: **não há sync de hazard/projétil ainda**, só de animação — é o item #2 abaixo. Precisa confirmar com o usuário exatamente o que ele esperava ver e não viu, antes de assumir que é a mesma coisa.
- **"Inventário ainda bugado"**: reportado antes nesta sessão como possível choque de estilo (personagem pixelado no mapa vs. ícones de item em alta definição no inventário — ver commit de pixel art), mas o usuário não confirmou se é isso ou outra coisa (posição de item, texto cortado, etc.). Sem repro exato ainda.

### Próximos passos sugeridos (não implementado, é só orientação)
1. **Testar o combate de verdade em jogo** (host + cliente, os dois com Brute/Rogue/Adept) — a troca do Scout pelo Rogue só foi validada por log de boot, não jogada. Se ainda tiver erro de ataque, é aqui que precisa olhar primeiro.
2. Sincronizar dano/vida de combate de verdade — decidir primeiro o que é autoritativo no servidor vs. previsto localmente no cliente. Hoje só a animação do ataque sincroniza (ver acima); um golpe de um jogador remoto não tira vida de ninguém.
3. Reenviar a aparência quando o equipamento muda (hoje só manda uma vez, ao conectar) — hook óbvio em `checkEquipmentChange()`.
4. Interpolação de posição entre pacotes (suaviza movimento com latência).
5. Se for mesmo pra MMO: área de interesse (só sincronizar entidades próximas), taxa de tick do servidor separada da taxa de render, e um plano de persistência (banco de dados) — isso é uma conversa de arquitetura em si, não um ajuste incremental no que já existe.

### Erros pré-existentes ainda não investigados (não é rede, aparecem em todo boot)
- `ERROR: ItemStack: Item id is 753, but quantity is zero.` / `Item id is 2002, but quantity is zero.` — vem de algum save/stash com item id + quantidade inconsistente. Presente desde antes desta sessão de rede; não sei se é save de teste corrompido (slots 1/2, mexidos bastante durante os testes) ou algo estrutural no mod.
- `ERROR: SDLHardwareRenderDevice: Couldn't load image: 'images/menus/game_over.png'.` — asset faltando (não existe em `fantasycore`/`empyrean_campaign`/`random_dungeon`). Cosmético (só afeta a tela de game over), mas ainda não corrigido.

## Créditos de música
- `music/anguish.ogg` — "Anguish" de Kevin MacLeod (incompetech.com), licença Creative Commons Attribution 3.0 (https://creativecommons.org/licenses/by/3.0/). Usada nos menus (`engine/default_music.txt`). A licença exige manter essa atribuição.
- A trilha antiga dos menus (`fantasycore/music/title_theme.ogg`) passou a tocar na masmorra (`maps/dungeon.txt`, `music=`).
