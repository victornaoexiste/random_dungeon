#!/usr/bin/env python3
"""Offline preview of a Flare isometric map: python3 tools/render_map.py maps/world/aurora_a.txt out.png [scale]"""
import os, re, sys
from PIL import Image
TOOLS = os.path.dirname(os.path.abspath(__file__)); MOD = os.path.dirname(TOOLS); MODS = os.path.dirname(MOD)
MODLIST = [os.path.basename(MOD), 'empyrean_campaign', 'fantasycore']

def locate(p):
    for m in MODLIST:
        f = os.path.join(MODS, m, p)
        if os.path.exists(f): return f

def load_tileset(path):
    tiles, cur, cache = {}, None, {}
    for line in open(locate(path), encoding='utf8'):
        line = line.strip()
        if line.startswith('img='):
            p = line[4:]; cur = cache.setdefault(p, Image.open(locate(p)).convert('RGBA'))
        elif line.startswith('tile='):
            v = [int(x) for x in line[5:].split(',')]
            tiles[v[0]] = (cur, v[1:])
    return tiles

def main(mapfile, out, scale=0.25):
    t = open(locate(mapfile) or mapfile, encoding='utf8').read()
    W = int(re.search(r'^width=(\d+)', t, re.M).group(1)); H = int(re.search(r'^height=(\d+)', t, re.M).group(1))
    ts = load_tileset(re.search(r'^tileset=(.*)$', t, re.M).group(1))
    layers = {}
    for m in re.finditer(r'\[layer\]\s*type=(\w+)\s*data=\s*\n(.*?)(?=\n\s*\n|\n\[|\Z)', t, re.S):
        v = [int(x) for x in re.findall(r'\d+', m.group(2))]
        layers[m.group(1)] = [v[i * W:(i + 1) * W] for i in range(H)]
    TW, TH = 192, 96
    img = Image.new('RGBA', ((W + H) * TW // 2, (W + H) * TH // 2 + 400), (0, 0, 0, 255))
    ox = H * TW // 2
    for name in ('background', 'object'):
        if name not in layers: continue
        for s in range(W + H):          # draw back-to-front
            for y in range(H):
                x = s - y
                if not (0 <= x < W): continue
                tid = layers[name][y][x]
                if tid == 0 or tid not in ts: continue
                sheet, (sx, sy, w, h, offx, offy) = ts[tid]
                px = ox + (x - y) * TW // 2 - offx + TW // 2
                py = (x + y) * TH // 2 - offy + TH // 2 + 300
                img.alpha_composite(sheet.crop((sx, sy, sx + w, sy + h)), (px, py))
    img = img.resize((int(img.width * scale), int(img.height * scale)), Image.LANCZOS)
    img.convert('RGB').save(out)

if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2], float(sys.argv[3]) if len(sys.argv) > 3 else 0.25)
