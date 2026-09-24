# Origem do código

Fork do Flare, consolidado em um único repositório em 2026-09-24:

- Motor: `github.com/flareteam/flare-engine`, base na versão 1.15 + commits locais (camada de rede `NetManager`, menu Multiplayer, sincronização de inimigos/combate, `HordeManager`).
- Conteúdo: `github.com/flareteam/flare-game` (`fantasycore`, `empyrean_campaign`), sem modificações neles; toda alteração do jogo fica em `mods/random_dungeon` (o Flare sobrepõe arquivos pela ordem em `mods/mods.txt`).

Ao redistribuir: manter GPL v3 no código, CC-BY-SA 3.0 no conteúdo derivado do Flare, e as atribuições em `CREDITS.md`.
