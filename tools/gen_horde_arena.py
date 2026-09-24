#!/usr/bin/env python3
"""Gera mods/random_dungeon/maps/horde_arena.txt: arena de grama aberta, cercada de arvores.
Uso: python3 tools/gen_horde_arena.py   (semente fixa: mesmo mapa toda vez)"""
import os, random
random.seed(7)
W = H = 100
BORDER = 3
CX, CY = W // 2, H // 2
CLEAR_R = 9            # area limpa em volta do ponto de inicio
TREES = list(range(48, 72))   # tiles de arvore (colisao 1) do tileset_grassland
DECOR = (124, 125, 126)       # tufos/pedras sem colisao

bg = [[16 + (y % 4) * 4 + (x % 4) for x in range(W)] for y in range(H)]
obj = [[0] * W for _ in range(H)]
col = [[0] * W for _ in range(H)]

def block(x, y, tree=True):
    if 0 <= x < W and 0 <= y < H:
        col[y][x] = 1
        if tree:
            obj[y][x] = random.choice(TREES)

for y in range(H):
    for x in range(W):
        d = min(x, y, W - 1 - x, H - 1 - y)
        if d < BORDER:
            col[y][x] = 1
            if d == BORDER - 1 or random.random() < 0.5:
                obj[y][x] = random.choice(TREES)

def far_from_center(x, y):
    return (x - CX) ** 2 + (y - CY) ** 2 > CLEAR_R ** 2

for _ in range(70):   # bosques espalhados
    cx = random.randint(BORDER + 3, W - BORDER - 4)
    cy = random.randint(BORDER + 3, H - BORDER - 4)
    if not far_from_center(cx, cy):
        continue
    for _ in range(random.randint(3, 9)):
        x = cx + random.randint(-2, 2)
        y = cy + random.randint(-2, 2)
        if far_from_center(x, y):
            block(x, y)

for y in range(BORDER, H - BORDER):
    for x in range(BORDER, W - BORDER):
        if col[y][x] == 0 and obj[y][x] == 0 and random.random() < 0.05:
            obj[y][x] = random.choice(DECOR)

def layer(name, g):
    return "[layer]\ntype=%s\ndata=\n%s\n" % (name, ",\n".join(",".join(map(str, r)) for r in g) + "\n")

out = f"""[header]
width={W}
height={H}
tilewidth=192
tileheight=96
orientation=isometric
background_color=0,0,0,255
hero_pos={CX},{CY}
music=music/title_theme.ogg
tileset=tilesetdefs/tileset_grassland.txt
title=Arena

{layer('background', bg)}
{layer('object', obj)}
{layer('collision', col)}"""
path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "mods", "random_dungeon", "maps", "horde_arena.txt")
open(path, "w").write(out)
print("ok", os.path.normpath(path))
