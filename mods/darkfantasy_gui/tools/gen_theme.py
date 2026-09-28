#!/usr/bin/env python3
"""
Random Dungeon UI theme: dark fantasy pixel art in red / black / gold.

Every image is drawn at ART resolution (1 art pixel = SCALE screen pixels)
with a small fixed palette and hard edges, then upscaled with nearest
neighbour, so the whole interface shares one pixel grid and one look.

Run:  python3 tools/gen_theme.py            (writes into this mod)
      python3 tools/gen_theme.py --preview  (also writes preview.png)

Output sizes match what the engine / fantasycore layouts expect, so the
existing menus/*.txt keep working; panels are drawn from those layouts
(slot positions etc.) so frames line up with what's on top of them.
"""
import os, sys, random, re
from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MODS = os.path.dirname(ROOT)
SCALE = 2
random.seed(7)

# ---------------------------------------------------------------- palette
K0 = (8, 4, 5)        # outline
K1 = (18, 9, 10)      # deepest fill
K2 = (28, 13, 14)     # panel fill
K3 = (42, 19, 19)     # raised fill
K4 = (60, 30, 28)     # light edge on dark
R0 = (58, 9, 11)      # dark red
R1 = (104, 16, 18)    # red
R2 = (156, 28, 26)    # bright red
R3 = (214, 58, 36)    # hot red
G0 = (84, 56, 14)     # dark gold
G1 = (150, 104, 22)   # gold
G2 = (222, 170, 44)   # bright gold
G3 = (255, 228, 128)  # gold highlight
GR0 = (40, 36, 36)    # disabled
GR1 = (78, 70, 66)
CLEAR = (0, 0, 0, 0)

def rgba(c, a=255):
    return c + (a,) if len(c) == 3 else c


# ---------------------------------------------------------------- helpers
def new(w, h, fill=CLEAR):
    return Image.new('RGBA', (w, h), rgba(fill) if fill != CLEAR else CLEAR)

def up(im, scale=SCALE):
    return im.resize((im.width * scale, im.height * scale), Image.NEAREST)

def out(rel):
    p = os.path.join(ROOT, rel)
    os.makedirs(os.path.dirname(p), exist_ok=True)
    return p

def save(im, rel, scale=SCALE):
    """im is art-space; saved upscaled."""
    up(im, scale).save(out(rel))

def px(d, x, y, c):
    d.point((x, y), fill=rgba(c))

def rect(d, x0, y0, x1, y1, c):
    d.rectangle([x0, y0, x1, y1], fill=rgba(c))

def hline(d, x0, x1, y, c):
    d.line([x0, y, x1, y], fill=rgba(c))

def vline(d, x, y0, y1, c):
    d.line([x, y0, x, y1], fill=rgba(c))

def dither_fill(d, x0, y0, x1, y1, a, b, density=0.5, pattern='bayer'):
    """Ordered-dither between colours a and b (density = share of b)."""
    bayer = [[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]]
    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1):
            t = (bayer[y % 4][x % 4] + 0.5) / 16.0
            px(d, x, y, b if t < density else a)

def vgrad_bands(d, x0, y0, x1, y1, colors):
    """Hard-banded vertical gradient (pixel art style), with dithered seams."""
    h = y1 - y0 + 1
    n = len(colors)
    for y in range(h):
        f = y / max(1, h - 1) * (n - 1)
        i = min(n - 2, int(f))
        t = f - i
        bayer = [[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]]
        for x in range(x0, x1 + 1):
            th = (bayer[y % 4][x % 4] + 0.5) / 16.0
            px(d, x, y0 + y, colors[i + 1] if t > th else colors[i])

def stone_texture(d, x0, y0, x1, y1, base, dark, light, seed=0):
    """Subtle hand-placed-looking pixel noise for panel fills."""
    rnd = random.Random(seed)
    rect(d, x0, y0, x1, y1, base)
    area = (x1 - x0 + 1) * (y1 - y0 + 1)
    for _ in range(area // 18):
        x = rnd.randint(x0, x1); y = rnd.randint(y0, y1)
        px(d, x, y, dark)
    for _ in range(area // 60):
        x = rnd.randint(x0, x1); y = rnd.randint(y0, y1)
        px(d, x, y, light)


# ---------------------------------------------------------------- frames
def rivet(d, cx, cy):
    """3x3 gold stud with highlight, K0 rim."""
    for (x, y) in [(-1, -2), (0, -2), (1, -2), (-2, -1), (2, -1), (-2, 0), (2, 0), (-2, 1), (2, 1), (-1, 2), (0, 2), (1, 2)]:
        px(d, cx + x, cy + y, K0)
    rect(d, cx - 1, cy - 1, cx + 1, cy + 1, G1)
    px(d, cx - 1, cy - 1, G3); px(d, cx, cy - 1, G2); px(d, cx - 1, cy, G2)
    px(d, cx + 1, cy + 1, G0)

def corner_ornament(d, x, y, fx, fy):
    """Small gold bracket + red gem in a panel corner. fx/fy = +1/-1 direction into the panel."""
    for i in range(7):
        px(d, x + fx * i, y, G2 if i < 6 else G1)
        px(d, x, y + fy * i, G2 if i < 6 else G1)
    for i in range(5):
        px(d, x + fx * i, y + fy, G0)
        px(d, x + fx, y + fy * i, G0)
    gx, gy = x + fx * 3, y + fy * 3
    px(d, gx, gy, R3); px(d, gx + fx, gy, R2); px(d, gx, gy + fy, R2); px(d, gx + fx, gy + fy, R0)

def panel(w, h, seed=1, title=False, ornaments=True, fill=K2):
    """Main window frame. Art-space w,h."""
    im = new(w, h)
    d = ImageDraw.Draw(im)
    rect(d, 0, 0, w - 1, h - 1, K0)
    # gold bevel
    rect(d, 1, 1, w - 2, h - 2, G1)
    hline(d, 1, w - 2, 1, G2); vline(d, 1, 1, h - 2, G2)
    hline(d, 1, w - 2, h - 2, G0); vline(d, w - 2, 1, h - 2, G0)
    rect(d, 2, 2, w - 3, h - 3, K0)
    # red inner line
    rect(d, 3, 3, w - 4, h - 4, R0)
    stone_texture(d, 4, 4, w - 5, h - 5, fill, K1, K3, seed)
    # inner shadow under the top edge
    hline(d, 4, w - 5, 4, K1); vline(d, 4, 4, h - 5, K1)
    if title:
        th = 14
        rect(d, 4, 4, w - 5, 4 + th, R0)
        vgrad_bands(d, 4, 4, w - 5, 4 + th - 1, [R1, R0, K1])
        hline(d, 4, w - 5, 4 + th, G0)
        hline(d, 4, w - 5, 5 + th, K0)
    if ornaments and w >= 24 and h >= 24:
        corner_ornament(d, 5, 5, 1, 1)
        corner_ornament(d, w - 6, 5, -1, 1)
        corner_ornament(d, 5, h - 6, 1, -1)
        corner_ornament(d, w - 6, h - 6, -1, -1)
    return im

def inset(d, x0, y0, x1, y1, fill=K1, rim=K0, light=K4, shadow=K0):
    """Recessed box (slots, text areas, inputs)."""
    rect(d, x0, y0, x1, y1, rim)
    rect(d, x0 + 1, y0 + 1, x1 - 1, y1 - 1, fill)
    hline(d, x0 + 1, x1 - 1, y0 + 1, shadow); vline(d, x0 + 1, y0 + 1, y1 - 1, shadow)
    hline(d, x0 + 1, x1 - 1, y1 - 1, light); vline(d, x1 - 1, y0 + 1, y1 - 1, light)

def gold_border(d, x0, y0, x1, y1, bright=False):
    c1, c2, c0 = (G3, G2, G1) if bright else (G2, G1, G0)
    rect(d, x0, y0, x1, y1, K0)
    hline(d, x0 + 1, x1 - 1, y0 + 1, c1); vline(d, x0 + 1, y0 + 1, y1 - 1, c1)
    hline(d, x0 + 1, x1 - 1, y1 - 1, c0); vline(d, x1 - 1, y0 + 1, y1 - 1, c0)


# ---------------------------------------------------------------- buttons
BTN_STATES = ('normal', 'pressed', 'hover', 'disabled')

def button_face(w, h, state):
    im = new(w, h)
    d = ImageDraw.Draw(im)
    rect(d, 0, 0, w - 1, h - 1, K0)
    if state == 'disabled':
        rect(d, 1, 1, w - 2, h - 2, GR1)
        rect(d, 2, 2, w - 3, h - 3, GR0)
        return im
    edge = {'normal': (G2, G0), 'hover': (G3, G1), 'pressed': (G0, G2)}[state]
    body = {'normal': [R2, R1, R0], 'hover': [R3, R2, R1], 'pressed': [R0, R1, R1]}[state]
    # gold rim
    rect(d, 1, 1, w - 2, h - 2, G1)
    hline(d, 1, w - 2, 1, edge[0]); vline(d, 1, 1, h - 2, edge[0])
    hline(d, 1, w - 2, h - 2, edge[1]); vline(d, w - 2, 1, h - 2, edge[1])
    rect(d, 2, 2, w - 3, h - 3, K0)
    vgrad_bands(d, 3, 3, w - 4, h - 4, body)
    if state != 'pressed':
        hline(d, 3, w - 4, 3, R3 if state == 'normal' else G3)
    else:
        hline(d, 3, w - 4, 3, K0)
    # tiny end caps (gems) on wide buttons
    if w >= 40:
        cy = h // 2
        for cx in (5, w - 6):
            px(d, cx, cy - 1, G2); px(d, cx, cy, G1); px(d, cx - 1, cy, G1); px(d, cx + 1, cy, G0); px(d, cx, cy + 1, G0)
    return im

def button_sheet(w, h, draw_icon=None):
    """Engine layout: 4 states stacked vertically (normal, pressed, hover, disabled)."""
    sheet = new(w, h * 4)
    for i, st in enumerate(BTN_STATES):
        face = button_face(w, h, st)
        if draw_icon:
            draw_icon(ImageDraw.Draw(face), w, h, st)
        sheet.paste(face, (0, i * h))
    return sheet

def icon_color(st):
    return {'disabled': GR1, 'pressed': G2, 'hover': G3, 'normal': G3}[st]

def icon_x(d, w, h, st):
    c = icon_color(st); cx, cy = w // 2, h // 2; r = min(w, h) // 4
    for i in range(-r, r + 1):
        px(d, cx + i, cy + i, c); px(d, cx + i, cy - i, c)
        px(d, cx + i + 1, cy + i, K0 if st != 'disabled' else GR0)

def icon_plus(d, w, h, st):
    c = icon_color(st); cx, cy = w // 2, h // 2; r = min(w, h) // 4
    hline(d, cx - r, cx + r, cy, c); vline(d, cx, cy - r, cy + r, c)
    hline(d, cx - r, cx + r, cy + 1, K0)

def icon_up_arrow(d, w, h, st):
    c = icon_color(st); cx, cy = w // 2, h // 2
    for i in range(5):
        hline(d, cx - i, cx + i, cy - 2 + i, c)

def icon_sort(d, w, h, st):
    c = icon_color(st); cx, cy = w // 2, h // 2
    for i, ln in enumerate((7, 5, 3)):
        hline(d, cx - 4, cx - 4 + ln, cy - 3 + i * 3, c)

def icon_gear(d, w, h, st):
    c = icon_color(st); cx, cy = w // 2, h // 2
    d.ellipse([cx - 4, cy - 4, cx + 4, cy + 4], outline=rgba(c))
    for (x, y) in [(0, -5), (0, 5), (-5, 0), (5, 0), (-4, -4), (4, 4), (-4, 4), (4, -4)]:
        px(d, cx + x, cy + y, c)
    px(d, cx, cy, c)

def arrow_sheet(w, h, direction):
    """up/down/left/right.png: 4 states of an arrow button."""
    def draw(d, w, h, st):
        c = icon_color(st); cx, cy = w // 2, h // 2
        for i in range(5):
            if direction == 'up':    hline(d, cx - i, cx + i, cy - 2 + i, c)
            if direction == 'down':  hline(d, cx - i, cx + i, cy + 2 - i, c)
            if direction == 'left':  vline(d, cx - 2 + i, cy - i, cy + i, c)
            if direction == 'right': vline(d, cx + 2 - i, cy - i, cy + i, c)
    return button_sheet(w, h, draw)


# ---------------------------------------------------------------- small widgets
def checkbox_sheet(s):
    sheet = new(s, s * 2)
    for i in range(2):
        f = new(s, s); d = ImageDraw.Draw(f)
        inset(d, 0, 0, s - 1, s - 1, K1)
        gold_border(d, 0, 0, s - 1, s - 1)
        rect(d, 2, 2, s - 3, s - 3, K1)
        if i == 1:
            m = max(3, s // 4)
            rect(d, m, m, s - 1 - m, s - 1 - m, R2)
            hline(d, m, s - 1 - m, m, R3)
            rect(d, m + 1, m + 1, m + 2, m + 2, G3)
        sheet.paste(f, (0, i * s))
    return sheet

def input_sheet(w, h):
    sheet = new(w, h * 2)
    for i in range(2):
        f = new(w, h); d = ImageDraw.Draw(f)
        inset(d, 0, 0, w - 1, h - 1, K1)
        if i == 1:  # editing: gold rim
            gold_border(d, 0, 0, w - 1, h - 1, True)
            rect(d, 2, 2, w - 3, h - 3, K1)
        sheet.paste(f, (0, i * h))
    return sheet

def listbox_sheet(w, h):
    """3 rows: first / middle / last item backgrounds."""
    sheet = new(w, h * 3)
    for i in range(3):
        f = new(w, h); d = ImageDraw.Draw(f)
        rect(d, 0, 0, w - 1, h - 1, K0)
        rect(d, 1, 0 if i else 1, w - 2, h - 1 if i < 2 else h - 2, K2)
        hline(d, 2, w - 3, h - 1, K3 if i < 2 else K0)
        vline(d, 1, 0, h - 1, G0); vline(d, w - 2, 0, h - 1, G0)
        if i == 0: hline(d, 1, w - 2, 1, G1)
        if i == 2: hline(d, 1, w - 2, h - 2, G0)
        sheet.paste(f, (0, i * h))
    return sheet

def scrollbar_sheet(s):
    """5 cells: up, up pressed, down, down pressed, knob."""
    sheet = new(s, s * 5)
    for i, (kind, st) in enumerate([('up', 'normal'), ('up', 'pressed'), ('down', 'normal'), ('down', 'pressed'), ('knob', 'normal')]):
        f = button_face(s, s, st if kind != 'knob' else 'hover')
        d = ImageDraw.Draw(f)
        cx, cy = s // 2, s // 2
        c = G3
        if kind == 'up':
            for k in range(4): hline(d, cx - k, cx + k, cy - 2 + k, c)
        elif kind == 'down':
            for k in range(4): hline(d, cx - k, cx + k, cy + 1 - k, c)
        else:
            for k in (-2, 0, 2): hline(d, cx - 3, cx + 3, cy + k, G2)
        sheet.paste(f, (0, i * s))
    return sheet

def slider_sheet(w, h):
    """Top row: groove w x h. Bottom row: knob (w/8 wide) at x=0."""
    sheet = new(w, h * 2)
    f = new(w, h); d = ImageDraw.Draw(f)
    cy = h // 2
    inset(d, 1, cy - 3, w - 2, cy + 3, K1)
    hline(d, 3, w - 4, cy, R0)
    sheet.paste(f, (0, 0))
    kw = w // 8
    knob = button_face(kw, h, 'hover')
    sheet.paste(knob, (0, h))
    return sheet

def tab_image(w, h, active):
    im = new(w, h); d = ImageDraw.Draw(im)
    rect(d, 0, 0, w - 1, h - 1, K0)
    body = [R2, R1, R0] if active else [K3, K2, K1]
    rim = G2 if active else G0
    rect(d, 1, 1, w - 2, h - 1, rim)
    vgrad_bands(d, 2, 2, w - 3, h - 1, body)
    hline(d, 2, w - 3, 2, R3 if active else K4)
    if not active:
        hline(d, 0, w - 1, h - 1, G0)
    return im

def slot(s, state):
    im = new(s, s); d = ImageDraw.Draw(im)
    if state == 'selected':
        # focus overlay (keyboard/gamepad navigation): frame only, see-through
        for k, c in enumerate([K0, G3, G2, K0]):
            d.rectangle([k, k, s - 1 - k, s - 1 - k], outline=rgba(c))
        for (x, y) in [(3, 3), (s - 4, 3), (3, s - 4), (s - 4, s - 4)]:
            px(d, x, y, R3)
        return im
    inset(d, 0, 0, s - 1, s - 1, K1)
    # faint diamond pattern in empty slots
    c = (24, 11, 12)
    for y in range(3, s - 3):
        for x in range(3, s - 3):
            if (x + y) % 6 == 0 or (x - y) % 6 == 0:
                px(d, x, y, c)
    if state == 'selected':
        gold_border(d, 0, 0, s - 1, s - 1, True)
        for (x, y) in [(2, 2), (s - 3, 2), (2, s - 3), (s - 3, s - 3)]:
            px(d, x, y, G3)
    return im

def tooltip_bg(w, h):
    im = new(w, h); d = ImageDraw.Draw(im)
    rect(d, 0, 0, w - 1, h - 1, K0)
    rect(d, 1, 1, w - 2, h - 2, G0)
    rect(d, 2, 2, w - 3, h - 3, (12, 6, 7, 240))
    return im


# ---------------------------------------------------------------- bars
def bar_fill(w, h, kind):
    im = new(w, h); d = ImageDraw.Draw(im)
    cols = {'hp': [R3, R2, R1, R0], 'mp': [(80, 70, 180), (52, 44, 140), (34, 26, 96), (20, 14, 60)],
            'xp': [G3, G2, G1], 'enemy': [R3, R2, R1]}[kind]
    vgrad_bands(d, 0, 0, w - 1, h - 1, cols)
    if h > 3:
        hline(d, 0, w - 1, 0, G3 if kind == 'xp' else (255, 140, 110) if kind in ('hp', 'enemy') else (150, 140, 255))
    # pixel sheen every few px
    for x in range(0, w, 6):
        if h > 4: px(d, x, 1, (255, 255, 255, 60))
    return im

def bar_background(w, h, fill_w, fill_h, offx, offy, label_room=0):
    """Frame drawn around a bar; the fill image is rendered at (offx, offy)."""
    im = new(w, h); d = ImageDraw.Draw(im)
    x0, y0 = offx - 2, offy - 2
    x1, y1 = offx + fill_w + 1, offy + fill_h + 1
    rect(d, x0 - 1, y0 - 1, x1 + 1, y1 + 1, K0)
    gold_border(d, x0 - 1, y0 - 1, x1 + 1, y1 + 1)
    rect(d, x0 + 1, y0 + 1, x1 - 1, y1 - 1, K0)
    rect(d, offx, offy, offx + fill_w - 1, offy + fill_h - 1, (24, 6, 8))
    # end caps
    for cx in (x0 - 1, x1 + 1):
        rect(d, cx - 1, (y0 + y1) // 2 - 2, cx + 1, (y0 + y1) // 2 + 2, G1)
        px(d, cx, (y0 + y1) // 2 - 1, G3)
    return im



# ---------------------------------------------------------------- pixel icons
# 16x16 grids. k=outline, d/g/G/h = gold dark..highlight, r/R = red, w = light,
# b/B = blue, '.' = transparent.
ICON_COLORS = {'k': K0, 'd': G0, 'g': G1, 'G': G2, 'h': G3, 'r': R1, 'R': R3, 'm': R2,
               'w': (240, 226, 200), 's': (150, 140, 130), 'S': (96, 88, 84),
               'b': (52, 44, 140), 'B': (110, 100, 230), 'x': K3}
ICONS = {
'helmet': """
................
.....kkkkkk.....
....kGGhGGgk....
...kGhhGGGggk...
..kGhGGGGGggdk..
..kGGGGGGGggdk..
..kkkkkkkkkkkk..
..kGgk.kk.kgdk..
..kGgk.kk.kgdk..
..kGgkkggkkgdk..
..kGGggggggddk..
...kGGgggggdk...
....kkgkkgdkk...
.....kk..kk.....
................
................""",
'chest': """
................
................
...kkkkkkkkkk...
..kdGGGGGGGGdk..
..kGhhhGGGGGgk..
..kGGGGGGGGGgk..
..kkkkkkkkkkkk..
..kdrrrkkrrrdk..
..kdrRrkGkrrdk..
..kdrrrkgkrrdk..
..kdrrrrkrrrdk..
..kdrrrrrrrrdk..
..kddddddddddk..
...kkkkkkkkkk...
................
................""",
'flame': """
................
.......k........
......kGk.......
......kGk..k....
.....kGhGk.kk...
....kGhhGk.kGk..
...kGhhRGGkGGk..
...kGhRRRGGGGk..
..kGhRRmRRGGgk..
..kGRRmmmRRGgk..
..kGRmmrmmRggk..
..kgRmrrrmRgdk..
...kgRrrrRgdk...
....kkgggdkk....
......kkkk......
................""",
'scroll': """
................
..kkkkkkkkkkk...
.kGGhhhhhhhGgk..
.kgGkkkkkkkgdk..
..kkwwwwwwwk....
...kwsSSSswk....
...kwwwwwwwk....
...kwsSSSSwk....
...kwwwwwwwk....
...kwsSSswwk....
...kwwwwwwwk....
..kkkkkkkkkkk...
.kGGhhhhhhhGgk..
.kgGgggggggddk..
..kkkkkkkkkkk...
................""",
'pause': """
................
................
................
....kkkk.kkkk...
....kGhk.kGhk...
....kGgk.kGgk...
....kGgk.kGgk...
....kGgk.kGgk...
....kGgk.kGgk...
....kGgk.kGgk...
....kGgk.kGgk...
....kgdk.kgdk...
....kkkk.kkkk...
................
................
................""",
'sword': """
................
.............kk.
............khk..
...........khGk.
..........khGk..
.........khGk...
........khGk....
.......khGk.....
..kk..khGk......
..kGkkhGk.......
...kGhGk........
....kGk.........
...kdkGk........
..kdk.kGk.......
.kkk...kk.......
................""",
'hand': """
................
.....kk.kk......
....kGkkGkk.....
....kGkkGkGk....
....kGkkGkGkk...
..kkkGkkGkGkGk..
.kGkkGGGGGGkGk..
.kGGkGGGGGGGGk..
..kGGGGGGGGGGk..
..kGGGGGGGGGk...
...kGGGGGGGGk...
....kGGGGGGk....
.....kGGGGk.....
.....kkkkkk.....
................
................""",
'map': """
................
................
..kkkkkkkkkkkk..
..kwwwwkwwwwwk..
..kwsswkwwRwwk..
..kwwwskwwwRwk..
..kwwwwksswwwk..
..kwRwwkwwswwk..
..kwwRwkwwwswk..
..kwwwwkwwwwwk..
..kkkkkkkkkkkk..
................
................
................
................
................""",
}

def paste_icon(screen_im, name, cx, cy, scale):
    """Icon drawn straight on a screen-resolution image, centred on (cx, cy),
    with chunkier pixels (scale) so it reads at button size."""
    ic = new(16, 16)
    draw_icon(ic, name, 0, 0)
    ic = ic.resize((16 * scale, 16 * scale), Image.NEAREST)
    screen_im.alpha_composite(ic, (cx - 8 * scale, cy - 8 * scale))

def draw_icon(im, name, x, y, scale=1):
    grid = [r for r in ICONS[name].strip('\n').split('\n')]
    d = ImageDraw.Draw(im)
    for j, row in enumerate(grid):
        for i, ch in enumerate(row):
            if ch in ICON_COLORS:
                rect(d, x + i * scale, y + j * scale, x + i * scale + scale - 1, y + j * scale + scale - 1, ICON_COLORS[ch])


# ---------------------------------------------------------------- HUD
def write_layout(rel, text):
    open(out(rel), 'w').write('# GENERATED by tools/gen_theme.py\n' + text.strip() + '\n')

# screen-space geometry (art = /2)
PLATE_W = 368

def status_plate():
    """HP (top), MP (middle), XP (bottom) pieces of one plate, top-left."""
    # HP piece: 368x44 screen -> 184x22 art. Fill 336x22 at (16,12) screen.
    W = PLATE_W // 2
    hp = new(W, 22); d = ImageDraw.Draw(hp)
    rect(d, 0, 0, W - 1, 21, K0)
    rect(d, 1, 1, W - 2, 21, G1); hline(d, 1, W - 2, 1, G2); vline(d, 1, 1, 21, G2); vline(d, W - 2, 1, 21, G0)
    rect(d, 2, 2, W - 3, 21, K0)
    stone_texture(d, 3, 3, W - 4, 21, K2, K1, K3, 3)
    inset(d, 7, 5, 7 + 168 + 1, 5 + 11 + 1, (24, 6, 8))
    corner_ornament(d, 4, 4, 1, 1); corner_ornament(d, W - 5, 4, -1, 1)
    save(hp, 'images/menus/hud_hp_plate.png')
    # MP piece: 368x28 screen -> 184x14 art. Fill 336x14 at (16,6).
    mp = new(W, 14); d = ImageDraw.Draw(mp)
    rect(d, 0, 0, W - 1, 13, K0)
    rect(d, 1, 0, W - 2, 13, G1); vline(d, 1, 0, 13, G2); vline(d, W - 2, 0, 13, G0)
    rect(d, 2, 0, W - 3, 13, K0)
    stone_texture(d, 3, 0, W - 4, 13, K2, K1, K3, 4)
    inset(d, 7, 2, 7 + 168 + 1, 2 + 7 + 1, (8, 6, 24))
    save(mp, 'images/menus/hud_mp_plate.png')
    # XP piece: 368x18 screen -> 184x9 art. Fill 336x4 at (16,4).
    xp = new(W, 9); d = ImageDraw.Draw(xp)
    rect(d, 0, 0, W - 1, 8, K0)
    rect(d, 1, 0, W - 2, 7, G1); vline(d, 1, 0, 7, G2); vline(d, W - 2, 0, 7, G0); hline(d, 1, W - 2, 7, G0)
    rect(d, 2, 0, W - 3, 6, K0)
    stone_texture(d, 3, 0, W - 4, 6, K2, K1, K3, 5)
    inset(d, 7, 1, 7 + 168 + 1, 1 + 2 + 1, K0)
    save(xp, 'images/menus/hud_xp_plate.png')

    save(bar_fill(168, 11, 'hp'), 'images/menus/bar_hp.png')
    save(bar_fill(168, 7, 'mp'), 'images/menus/bar_mp.png')
    save(bar_fill(168, 2, 'xp'), 'images/menus/bar_xp.png')

    write_layout('menus/hp.txt', """
pos=8,8,368,44
bar_pos=0,0,368,44
bar_fill_size=336,22
bar_fill_offset=16,12
align=topleft
orientation=0
text_pos=184,12,center,top
bar_gfx=images/menus/bar_hp.png
bar_gfx_background=images/menus/hud_hp_plate.png
""")
    write_layout('menus/mp.txt', """
pos=8,52,368,28
bar_pos=0,0,368,28
bar_fill_size=336,14
bar_fill_offset=16,6
align=topleft
orientation=0
text_pos=184,2,center,top
bar_gfx=images/menus/bar_mp.png
bar_gfx_background=images/menus/hud_mp_plate.png
""")
    write_layout('menus/xp.txt', """
pos=8,80,368,18
bar_pos=0,0,368,18
bar_fill_size=336,4
bar_fill_offset=16,4
align=topleft
orientation=0
text_pos=184,20,center,top
bar_gfx=images/menus/bar_xp.png
bar_gfx_background=images/menus/hud_xp_plate.png
""")

# action bar: 10 hotkeys + 2 mouse + 4 menu buttons
AB_SLOT = 64
AB_GAP = 4
def actionbar():
    xs = [16 + i * (AB_SLOT + AB_GAP) for i in range(10)]
    m1 = xs[-1] + AB_SLOT + 24
    m2 = m1 + AB_SLOT + AB_GAP
    menus_x = [m2 + AB_SLOT + 24 + i * (AB_SLOT + AB_GAP) for i in range(4)]
    W = menus_x[-1] + AB_SLOT + 16
    H = 84
    Wa, Ha = W // 2, H // 2
    im = new(Wa, Ha); d = ImageDraw.Draw(im)
    # body: raised plate with gold top rim, open at the bottom edge
    rect(d, 0, 0, Wa - 1, Ha - 1, K0)
    rect(d, 1, 1, Wa - 2, Ha - 1, G1); hline(d, 1, Wa - 2, 1, G2); vline(d, 1, 1, Ha - 1, G2); vline(d, Wa - 2, 1, Ha - 1, G0)
    rect(d, 2, 2, Wa - 3, Ha - 1, K0)
    rect(d, 3, 3, Wa - 4, Ha - 1, R0)
    stone_texture(d, 4, 4, Wa - 5, Ha - 1, K2, K1, K3, 9)
    y = 10 // 2
    for x in xs + [m1, m2]:
        inset(d, x // 2 - 1, y - 1, x // 2 + AB_SLOT // 2, y + AB_SLOT // 2, K1)
    # separators with a gem
    for sx in (xs[-1] + AB_SLOT + 12, m2 + AB_SLOT + 12):
        vline(d, sx // 2, 6, Ha - 6, G0); vline(d, sx // 2 + 1, 6, Ha - 6, K0)
        rect(d, sx // 2 - 1, Ha // 2 - 2, sx // 2 + 2, Ha // 2 + 1, G1)
        px(d, sx // 2, Ha // 2 - 1, R3); px(d, sx // 2 + 1, Ha // 2 - 1, R2); px(d, sx // 2, Ha // 2, R2); px(d, sx // 2 + 1, Ha // 2, R0)
    # menu buttons baked in (the engine only overlays click areas)
    for x in menus_x:
        im.paste(button_face(AB_SLOT // 2, AB_SLOT // 2, 'normal'), (x // 2, y))
    corner_ornament(d, 4, 4, 1, 1); corner_ornament(d, Wa - 5, 4, -1, 1)
    big = up(im)
    for x, icon in zip(menus_x, ('helmet', 'chest', 'flame', 'scroll')):
        paste_icon(big, icon, x + AB_SLOT // 2, 10 + AB_SLOT // 2, 3)
    big.save(out('images/menus/actionbar_trim.png'))
    lines = ['pos=0,0,%d,%d' % (W, H), 'align=bottom']
    for i, x in enumerate(xs):
        lines.append('slot=%d,%d,10' % (i + 1, x))
    lines += ['slot_M1=%d,10' % m1, 'slot_M2=%d,10' % m2]
    for key, x in zip(('char_menu', 'inv_menu', 'powers_menu', 'log_menu'), menus_x):
        lines.append('%s=%d,10' % (key, x))
    lines.append('tooltip_length=long_all')
    write_layout('menus/actionbar.txt', '\n'.join(lines))

def minimap():
    W, H = 256, 288
    im = panel(W // 2, H // 2, seed=12, title=True)
    d = ImageDraw.Draw(im)
    inset(d, 1, 17, W // 2 - 2, H // 2 - 2, K0)
    save(im, 'images/menus/minimap.png')
    write_layout('menus/minimap.txt', """
pos=-8,8,256,288
align=topright
background=images/menus/minimap.png
map_pos=4,36,248,248
text_pos=128,6,center,top
button_config=192,292
default_zoom_level=3
""")

def enemy_bar():
    W, H = 320, 56
    im = new(W // 2, H // 2); d = ImageDraw.Draw(im)
    rect(d, 0, 0, W // 2 - 1, H // 2 - 1, K0)
    gold_border(d, 0, 0, W // 2 - 1, H // 2 - 1)
    stone_texture(d, 2, 2, W // 2 - 3, H // 2 - 3, K2, K1, K3, 21)
    inset(d, 9, 15, 9 + 142 + 1, 15 + 7 + 1, (24, 6, 8))
    corner_ornament(d, 3, 3, 1, 1); corner_ornament(d, W // 2 - 4, 3, -1, 1)
    save(im, 'images/menus/bar_enemy_hp_background.png')
    save(bar_fill(142, 7, 'enemy'), 'images/menus/bar_enemy_hp.png')
    write_layout('menus/enemy.txt', """
pos=0,8,320,56
bar_pos=0,0,320,56
bar_fill_size=284,14
align=top
bar_fill_offset=20,32
text_pos=160,4,center,top
bar_gfx=images/menus/bar_enemy_hp.png
background=images/menus/bar_enemy_hp_background.png
""")


def devkit():
    """Dev Kit (MenuDevKit) art: same widgets, its own sizes."""
    save(button_sheet(80, 13), 'images/menus/devkit/button.png')
    save(button_sheet(17, 13), 'images/menus/devkit/button_small.png')
    save(button_sheet(46, 13), 'images/menus/devkit/button_tab.png')
    save(button_sheet(165, 13), 'images/menus/devkit/button_wide.png')
    save(listbox_sheet(165, 11), 'images/menus/devkit/listbox.png')
    save(panel(200, 270, seed=31), 'images/menus/devkit/panel.png')

def hud():
    status_plate()
    actionbar()
    minimap()
    enemy_bar()
    # icons sheet for other code (touch buttons etc.)
    for name in ICONS:
        im = new(16, 16)
        draw_icon(im, name, 0, 0)
        save(im, 'images/menus/icons/%s.png' % name)



# ---------------------------------------------------------------- panels
# Side panels are 640x832 screen px (fantasycore geometry: title at y=24,
# close button at 571,5, footer text at y=823). Drawn in art space (/2).
PW, PH = 640, 832
HEADER = 48   # screen px
FOOTER = 52

def side_panel(seed):
    W, H = PW // 2, PH // 2
    im = panel(W, H, seed=seed, ornaments=True)
    d = ImageDraw.Draw(im)
    # header band
    hb = HEADER // 2
    vgrad_bands(d, 4, 4, W - 5, hb - 1, [R1, R0, K1])
    hline(d, 4, W - 5, 4, R2)
    hline(d, 4, W - 5, hb, G0); hline(d, 4, W - 5, hb + 1, K0)
    # little gold wings either side of the title
    cx = W // 2
    for sgn in (-1, 1):
        for i in range(36, 70):
            px(d, cx + sgn * i, hb // 2, G1 if i % 2 else G0)
        rect(d, cx + sgn * 71 - 1, hb // 2 - 1, cx + sgn * 71 + 1, hb // 2 + 1, G2)
    # footer band
    fb = H - FOOTER // 2
    hline(d, 4, W - 5, fb - 1, K0); hline(d, 4, W - 5, fb, G0)
    vgrad_bands(d, 4, fb + 1, W - 5, H - 5, [K1, R0, K1])
    corner_ornament(d, 5, 5, 1, 1); corner_ornament(d, W - 6, 5, -1, 1)
    corner_ornament(d, 5, H - 6, 1, -1); corner_ornament(d, W - 6, H - 6, -1, -1)
    return im, d

def s2a(v):
    return v // 2

def inset_screen(d, x, y, w, h, fill=K1):
    inset(d, s2a(x) - 1, s2a(y) - 1, s2a(x + w) , s2a(y + h), fill)

def slot_grid(im, x, y, cols, rows, size=64):
    s = slot(size // 2, 'empty')
    for j in range(rows):
        for i in range(cols):
            im.paste(s, (s2a(x) + i * size // 2, s2a(y) + j * size // 2))

def frame_group(d, x0, y0, x1, y1):
    """Thin gold-edged recess grouping a set of widgets (screen coords)."""
    ax0, ay0, ax1, ay1 = s2a(x0), s2a(y0), s2a(x1), s2a(y1)
    rect(d, ax0, ay0, ax1, ay1, K0)
    rect(d, ax0 + 1, ay0 + 1, ax1 - 1, ay1 - 1, G0)
    rect(d, ax0 + 2, ay0 + 2, ax1 - 2, ay1 - 2, K0)
    stone_texture(d, ax0 + 3, ay0 + 3, ax1 - 3, ay1 - 3, K1, (12, 6, 7), K2, ax0 * 7 + ay0)

def panel_character():
    im, d = side_panel(41)
    frame_group(d, 24, 56, 616, 280)
    inset_screen(d, 168, 66, 240, 28)
    inset_screen(d, 562, 66, 60, 28)
    for y in (110, 154, 198, 242):
        inset_screen(d, 162, y, 60, 28)
        hline(d, s2a(236), s2a(600), s2a(y + 30), G0)
    frame_group(d, 12, 290, 628, 776)
    save(im, 'images/menus/character.png')

def panel_tabbed(name, tab_area):
    x, y, w, h = tab_area
    im, d = side_panel(43 if name == 'powers' else 44)
    frame_group(d, x - 8, y + 44, x + w + 8, y + h + 8)
    save(im, 'images/menus/%s.png' % name)

def panel_powers():
    """Skills sheet, EK style (MenuPowers detail_area): tabs, a 5x5 icon
    grid with rank pips, then the selected skill's details and Learn."""
    im, d = side_panel(43)
    frame_group(d, 56, 104, 584, 548)      # icon grid (random_dungeon gen_skills GRID_*)
    frame_group(d, 56, 578, 584, 764)      # detail panel
    save(im, 'images/menus/powers.png')
    write_layout('menus/powers.txt', '\n'.join([
        'pos=0,-35,640,832', 'align=right',
        'soundfx_open=soundfx/inventory/inventory_page.ogg',
        'soundfx_close=soundfx/inventory/inventory_book.ogg',
        'label_title=320,24,center,center', 'close=571,5',
        'tab_area=64,60,512,672',
        'unspent_points=320,563,center,center',
        'detail_area=72,586,496,174',
        'learn_button=128,774']))

def panel_storage():
    im, d = side_panel(45)
    frame_group(d, 56, 104, 584, 760)
    slot_grid(im, 64, 112, 8, 10)
    save(im, 'images/menus/storage_generic.png')

# Inventory: our own layout at the shared panel size.
INV_EQUIP = [  # (x, y, type) screen, 64px slots, a paper-doll
    (288, 64, 'head'),
    (200, 144, 'hands'), (288, 144, 'chest'), (376, 144, 'artifact'),
    (200, 224, 'ring'), (288, 224, 'legs'), (376, 224, 'ring'),
    (200, 304, 'main'), (288, 304, 'feet'), (376, 304, 'off'),
]
INV_CARRIED = (64, 432, 8, 5)

def panel_inventory():
    im, d = side_panel(46)
    frame_group(d, 104, 56, 536, 392)
    # silhouette hint behind the paper doll
    for (x, y, t) in INV_EQUIP:
        s = slot(32, 'empty')
        im.paste(s, (s2a(x), s2a(y)))
    x, y, c, r = INV_CARRIED
    frame_group(d, x - 8, y - 8, x + c * 64 + 8, y + r * 64 + 8)
    slot_grid(im, x, y, c, r)
    save(im, 'images/menus/inventory.png')
    lines = ['pos=0,-35,640,832', 'align=right',
             'soundfx_open=soundfx/inventory/inventory_page.ogg',
             'soundfx_close=soundfx/inventory/inventory_book.ogg',
             'label_title=320,24,center,center',
             'currency=600,806,right,center',
             'close=571,5', 'help=5,5,37,37']
    for set_id in (1, 2):
        for (ex, ey, t) in INV_EQUIP:
            lines.append('equipment_slot=%d,%d,%s,%d' % (ex, ey, t, set_id))
    lines += ['set_previous=120,316,images/menus/buttons/left.png',
              'set_next=464,316,images/menus/buttons/right.png',
              'label_equipment_set=320,408,center,center',
              'carried_area=%d,%d' % (x, y), 'carried_cols=%d' % c, 'carried_rows=%d' % r,
              'sort_enabled=true', 'sort_pos=46,5']
    write_layout('menus/inventory.txt', '\n'.join(lines))

def panel_config():
    """Pause / settings screen (config.png, drawn at background_offset 0,48)."""
    W, H = 1674 // 2, 752 // 2
    im = panel(W, H, seed=51)
    save(im, 'images/menus/config.png')

def panel_dialog():
    """NPC talk box (dialog_box.png 960x384) and modal windows."""
    im = panel(480, 192, seed=52, title=False)
    save(im, 'images/menus/dialog_box.png')
    save(panel(384, 150, seed=53), 'images/menus/modal_window.png')
    # portrait frame: drawn OVER the portrait, so the middle must stay see-through
    pb = new(160, 160); d = ImageDraw.Draw(pb)
    for k, c in enumerate([K0, G2, G1, G0, K0]):
        d.rectangle([k, k, 159 - k, 159 - k], outline=rgba(c))
    hline(d, 1, 158, 1, G3); vline(d, 1, 1, 158, G3)
    corner_ornament(d, 5, 5, 1, 1); corner_ornament(d, 154, 5, -1, 1)
    corner_ornament(d, 5, 154, 1, -1); corner_ornament(d, 154, 154, -1, -1)
    save(pb, 'images/menus/portrait_border.png')

def run_card():
    """Infinite Run upgrade card (MenuRunUpgrade): 300x270, icon well at the top."""
    im = panel(150, 135, seed=80)
    d = ImageDraw.Draw(im)
    inset(d, 57, 12, 92, 47, K1)      # icon well (64px icon at y=28)
    hline(d, 20, 129, 70, G0)
    save(im, 'images/menus/run_card.png')
    save(button_sheet(100, 18), 'images/menus/run_button.png')   # 200x36 per state
    # Sanctuary blessing card (MenuSanctuary): 280x200, icon well at the top
    im = panel(140, 100, seed=81)
    d = ImageDraw.Draw(im)
    inset(d, 53, 5, 86, 38, K1)       # icon well (64px icon at y=12)
    hline(d, 20, 119, 64, G0)
    save(im, 'images/menus/sanctuary_card.png')
    # Game Over (a run's summary + the Sanctuary button)
    write_layout('menus/game_over.txt', '\n'.join([
        'pos=0,-192,768,300', 'align=center', 'background=images/menus/modal_window.png',
        'label_title=384,23,center,center', 'button_continue=192,126', 'button_exit=192,174',
        'button_sanctuary=192,222']))

def panels():
    run_card()
    panel_character()
    panel_powers()
    panel_tabbed('log', (64, 60, 482, 672))
    panel_storage()
    panel_inventory()
    panel_config()
    panel_dialog()



# ---------------------------------------------------------------- menu screens
def text_art(text, size, fontfile=None):
    """Pixel text mask (no anti-aliasing) at art resolution."""
    fontfile = fontfile or TITLE
    ft = ImageFont.truetype(os.path.join(ROOT, 'tools', 'fonts', fontfile), size)
    tmp = Image.new('L', (1, 1)); dd = ImageDraw.Draw(tmp); dd.fontmode = '1'
    bb = dd.textbbox((0, 0), text, font=ft)
    w, h = bb[2] - bb[0], bb[3] - bb[1]
    m = Image.new('L', (w + 2, h + 2), 0)
    dm = ImageDraw.Draw(m); dm.fontmode = '1'
    dm.text((1 - bb[0], 1 - bb[1]), text, font=ft, fill=255)
    return m

def gold_text(text, size, fontfile=None):
    """Gold banded letters, black outline, red drop shadow."""
    m = text_art(text, size, fontfile)
    w, h = m.size
    im = new(w + 6, h + 6)
    mask = m.point(lambda v: 255 if v > 127 else 0)
    # shadow (red), outline (black), fill (gold bands)
    for (dx, dy, col) in [(3, 3, R0), (2, 2, R1)]:
        im.paste(Image.new('RGBA', m.size, rgba(col)), (dx + 1, dy + 1), mask)
    for dx in (-1, 0, 1):
        for dy in (-1, 0, 1):
            im.paste(Image.new('RGBA', m.size, rgba(K0)), (1 + dx + 1, 1 + dy + 1), mask)
    fill = new(w, h); d = ImageDraw.Draw(fill)
    vgrad_bands(d, 0, 0, w - 1, h - 1, [G3, G2, G2, G1, G0])
    im.paste(fill, (2, 2), mask)
    return im

def logo():
    t = gold_text('Random Dungeon', 48)
    W = t.width + 20; H = t.height + 22
    im = new(W, H)
    im.alpha_composite(t, ((W - t.width) // 2, 0))
    d = ImageDraw.Draw(im)
    y = t.height + 6; cx = W // 2
    for i in range(10, W // 2 - 6):
        px(d, cx - i, y, G1 if i % 3 else G0); px(d, cx + i, y, G1 if i % 3 else G0)
    # centre gem + side studs
    rect(d, cx - 3, y - 3, cx + 3, y + 3, K0); rect(d, cx - 2, y - 2, cx + 2, y + 2, R2)
    px(d, cx - 1, y - 1, R3); px(d, cx + 1, y + 1, R0)
    for sx in (cx - W // 2 + 6, cx + W // 2 - 6):
        rect(d, sx - 1, y - 1, sx + 1, y + 1, G2)
    save(im, 'images/menus/df_logo.png')
    return im.size[0] * SCALE, im.size[1] * SCALE

def background():
    """Title/menu backdrop: pixel-art dungeon hall, 480x270 art -> 1920x1080."""
    W, H = 480, 270
    im = new(W, H, K1); d = ImageDraw.Draw(im)
    rnd = random.Random(99)
    # brick wall
    for y in range(0, H, 10):
        off = 10 if (y // 10) % 2 else 0
        for x in range(-off, W, 20):
            shade = rnd.choice([K2, K2, K3, (36, 16, 16)])
            rect(d, x + 1, y + 1, x + 18, y + 8, shade)
            hline(d, x + 1, x + 18, y + 1, K4 if shade == K3 else K3)
    # glow from below (dithered red)
    for y in range(H // 2, H):
        t = (y - H // 2) / (H / 2)
        for x in range(W):
            dx = abs(x - W / 2) / (W / 2)
            a = t * (1 - dx * 0.8)
            b = [[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]][y % 4][x % 4] / 16
            if a * 0.55 > b:
                c = im.getpixel((x, y))
                im.putpixel((x, y), (min(255, c[0] + 40), c[1] + 4, c[2] + 2, 255))
    # gothic arch
    cx = W // 2; top = 8; hw = 124
    for r in range(hw, hw - 9, -1):
        col = [K0, G0, G1, G2, G1, G0, K0, R0, K0][hw - r]
        for a in range(0, 1800):
            import math
            ang = math.radians(a / 10)
            x = cx + int(r * math.cos(ang)); y = top + hw - int(r * math.sin(ang))
            if 0 <= y < H and 0 <= x < W:
                px(d, x, y, col)
        vline(d, cx - r, top + hw, H - 1, col); vline(d, cx + r, top + hw, H - 1, col)
    # darkness inside the arch
    for y in range(top + 9, H):
        for x in range(cx - hw + 9, cx + hw - 8):
            import math
            if y < top + hw and (x - cx) ** 2 + (top + hw - y) ** 2 > (hw - 9) ** 2:
                continue
            c = im.getpixel((x, y))
            im.putpixel((x, y), (c[0] // 3, c[1] // 3, c[2] // 3, 255))
    # torches
    for tx in (cx - hw - 40, cx + hw + 40):
        ty = 120
        rect(d, tx - 2, ty, tx + 2, ty + 14, G0); rect(d, tx - 3, ty, tx + 3, ty + 2, G1)
        flame = [(0, -1, R1), (0, -2, R2), (-1, -2, R1), (1, -2, R1), (0, -3, R3), (-1, -3, R2), (1, -3, R2),
                 (0, -4, G2), (0, -5, R3), (-1, -4, R3), (1, -4, R3), (0, -6, G3), (0, -7, R3)]
        for (fx, fy, col) in flame:
            rect(d, tx + fx * 2, ty + fy * 2, tx + fx * 2 + 1, ty + fy * 2 + 1, col)
        # light halo
        for rr in range(8, 40, 4):
            for k in range(0, 360, 3):
                import math
                x = tx + int(rr * math.cos(math.radians(k))); y = ty - 8 + int(rr * math.sin(math.radians(k)) * 0.8)
                if 0 <= x < W and 0 <= y < H and rnd.random() < 0.35 * (1 - rr / 40):
                    c = im.getpixel((x, y)); im.putpixel((x, y), (min(255, c[0] + 30), min(255, c[1] + 14), c[2], 255))
    # embers
    for _ in range(90):
        x = rnd.randint(cx - 140, cx + 140); y = rnd.randint(H // 2, H - 4)
        px(d, x, y, rnd.choice([R3, G2, R2, G3]))
    # floor line
    rect(d, 0, H - 14, W - 1, H - 1, K0)
    for x in range(0, W, 3):
        px(d, x, H - 14, R0)
    # vignette (dithered to black)
    for y in range(H):
        for x in range(W):
            vx = abs(x - W / 2) / (W / 2); vy = abs(y - H / 2) / (H / 2)
            v = max(0.0, (vx * vx + vy * vy) ** 0.5 - 0.62) * 1.8
            b = [[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]][y % 4][x % 4] / 16
            if v > b:
                im.putpixel((x, y), rgba(K0))
    big = up(im, 4).convert('RGB')
    big.save(out('images/menus/backgrounds/df_menu.png'))
    open(out('engine/menu_backgrounds.txt'), 'w').write('# GENERATED by tools/gen_theme.py\nbackground=images/menus/backgrounds/df_menu.png\n')

def game_slots():
    """Load screen: 4 save cards (864x288 each) + the selection overlay."""
    W, H = 864 // 2, 288 // 2
    sheet = new(W, H * 4)
    for i in range(4):
        card = panel(W, H, seed=60 + i)
        d = ImageDraw.Draw(card)
        inset(d, W - 118, 8, W - 9, H - 9, K1)   # character preview
        sheet.paste(card, (0, i * H))
    save(sheet, 'images/menus/game_slots.png')
    sel = new(W, H); d = ImageDraw.Draw(sel)
    for k, c in enumerate([G3, G2, G1]):
        d.rectangle([k, k, W - 1 - k, H - 1 - k], outline=rgba(c))
    for (x, y) in [(3, 3), (W - 4, 3), (3, H - 4), (W - 4, H - 4)]:
        rect(d, x - 2, y - 2, x + 2, y + 2, R2); px(d, x, y, R3)
    save(sel, 'images/menus/game_slot_select.png')

def screens():
    lw, lh = logo()
    background()
    game_slots()
    # Multiplayer screen panel (GameStateMultiplayer), title band 44px
    mp = panel(320, 230, seed=70, title=True)
    save(mp, 'images/menus/df_panel_multiplayer.png')
    return lw, lh



# ---------------------------------------------------------------- touch controls
def disc(d, cx, cy, r, fill, rim_light, rim_dark, alpha=255, ring=2):
    """Pixel circle: dark outline, gold bevelled rim, filled centre."""
    for y in range(cy - r - 1, cy + r + 2):
        for x in range(cx - r - 1, cx + r + 2):
            dist = ((x - cx) ** 2 + (y - cy) ** 2) ** 0.5
            if dist <= r + 0.5:
                if dist > r - 0.7:
                    px(d, x, y, K0)
                elif dist > r - 0.7 - ring:
                    top_left = (x - cx) + (y - cy) < 0
                    px(d, x, y, rim_light if top_left else rim_dark)
                elif dist > r - 1.7 - ring:
                    px(d, x, y, K0)
                else:
                    px(d, x, y, fill[:3] + (alpha,) if len(fill) == 3 else fill)

def touch_button(size, pressed, big=False):
    r = size // 2 - 1
    im = new(size, size); d = ImageDraw.Draw(im)
    fill = (R0 if pressed else K2) + ((235 if pressed else 190),)
    disc(d, size // 2, size // 2, r, fill, G3 if pressed else G2, G0, ring=2 if not big else 3)
    # inner dithered sheen on the upper-left
    for y in range(size):
        for x in range(size):
            dx, dy = x - size // 2, y - size // 2
            dist = (dx * dx + dy * dy) ** 0.5
            if r * 0.35 < dist < r * 0.75 and dx + dy < -r * 0.5 and (x + y) % 2 == 0:
                px(d, x, y, R1 if not pressed else R2)
    return im

def touch_sheet(size, big=False):
    sheet = new(size, size * 2)
    sheet.paste(touch_button(size, False, big), (0, 0))
    sheet.paste(touch_button(size, True, big), (0, size))
    return sheet

def touch():
    save(touch_sheet(96, True), 'images/menus/touch/button_big.png')
    save(touch_sheet(62), 'images/menus/touch/button_small.png')
    # pause: small button with the pause icon
    sheet = touch_sheet(40)
    big = up(sheet)
    for i in range(2):
        paste_icon(big, 'pause', 40, 40 + i * 80 + (2 if i else 0), 3)
    big.save(out('images/menus/touch/button_pause.png'))
    # joystick base: faint ring with four arrow ticks
    S = 144
    base = new(S, S); d = ImageDraw.Draw(base)
    disc(d, S // 2, S // 2, S // 2 - 2, K1, G1, G0, alpha=110, ring=2)
    c = S // 2
    for k in range(5):
        hline(d, c - k, c + k, 8 + k, G2); hline(d, c - k, c + k, S - 9 - k, G2)
        vline(d, 8 + k, c - k, c + k, G2); vline(d, S - 9 - k, c - k, c + k, G2)
    save(base, 'images/menus/touch/joystick_base.png')
    # knob: red orb, brighter while held
    K = 56
    sheet = new(K, K * 2)
    for i, (f, l) in enumerate([(R1, G2), (R2, G3)]):
        k = new(K, K); dk = ImageDraw.Draw(k)
        disc(dk, K // 2, K // 2, K // 2 - 1, f, l, G0, ring=2)
        rect(dk, K // 2 - 8, K // 2 - 10, K // 2 - 4, K // 2 - 7, R3)
        sheet.paste(k, (0, i * K))
    save(sheet, 'images/menus/touch/joystick_knob.png')
    touch_actionbar()

def touch_actionbar():
    """Mobile action bar: slots 5-8 + the four menu buttons, bottom centre.
    The slots feeding the touch buttons (1-4, M1, M2) are placed by the
    engine under those buttons; 9 and 10 are parked off-screen."""
    xs = [16 + i * (AB_SLOT + AB_GAP) for i in range(4)]
    menus_x = [xs[-1] + AB_SLOT + 24 + i * (AB_SLOT + AB_GAP) for i in range(4)]
    W = menus_x[-1] + AB_SLOT + 16
    H = 84
    Wa, Ha = W // 2, H // 2
    im = new(Wa, Ha); d = ImageDraw.Draw(im)
    rect(d, 0, 0, Wa - 1, Ha - 1, K0)
    rect(d, 1, 1, Wa - 2, Ha - 1, G1); hline(d, 1, Wa - 2, 1, G2); vline(d, 1, 1, Ha - 1, G2); vline(d, Wa - 2, 1, Ha - 1, G0)
    rect(d, 2, 2, Wa - 3, Ha - 1, K0)
    rect(d, 3, 3, Wa - 4, Ha - 1, R0)
    stone_texture(d, 4, 4, Wa - 5, Ha - 1, K2, K1, K3, 19)
    y = 5
    for x in xs:
        inset(d, x // 2 - 1, y - 1, x // 2 + AB_SLOT // 2, y + AB_SLOT // 2, K1)
    sx = xs[-1] + AB_SLOT + 12
    vline(d, sx // 2, 6, Ha - 6, G0); vline(d, sx // 2 + 1, 6, Ha - 6, K0)
    for x in menus_x:
        im.paste(button_face(AB_SLOT // 2, AB_SLOT // 2, 'normal'), (x // 2, y))
    corner_ornament(d, 4, 4, 1, 1); corner_ornament(d, Wa - 5, 4, -1, 1)
    big = up(im)
    for x, icon in zip(menus_x, ('helmet', 'chest', 'flame', 'scroll')):
        paste_icon(big, icon, x + AB_SLOT // 2, 10 + AB_SLOT // 2, 3)
    big.save(out('images/menus/actionbar_touch_trim.png'))
    lines = ['pos=0,0,%d,%d' % (W, H), 'align=bottom', 'background=images/menus/actionbar_touch_trim.png']
    for n, x in zip((5, 6, 7, 8), xs):
        lines.append('slot=%d,%d,10' % (n, x))
    for n in (1, 2, 3, 4, 9, 10):
        lines.append('slot=%d,-4000,10' % n)
    lines += ['slot_M1=-4000,10', 'slot_M2=-4000,10']
    for key, x in zip(('char_menu', 'inv_menu', 'powers_menu', 'log_menu'), menus_x):
        lines.append('%s=%d,10' % (key, x))
    lines.append('tooltip_length=long_all')
    write_layout('menus/actionbar_touch.txt', '\n'.join(lines))


# ---------------------------------------------------------------- font
def install_fonts():
    """Pixel fonts (SIL OFL), copied in from tools/fonts/."""
    src = os.path.join(ROOT, 'tools', 'fonts')
    dst = os.path.join(ROOT, 'fonts')
    os.makedirs(dst, exist_ok=True)
    for f in os.listdir(src):
        with open(os.path.join(src, f), 'rb') as a, open(os.path.join(dst, f), 'wb') as b:
            b.write(a.read())


# Pixel fonts render crisp only at multiples of their design grid:
# Jersey 10 -> 20/30/40, Jacquard 12 -> 24/36/48. Blend 0 = no anti-aliasing.
BODY = 'Jersey10-Regular.ttf'
TITLE = 'Jacquard12-Regular.ttf'
FONT_STYLES = {
    'font_regular':       (BODY, 30),
    'font_bold':          (BODY, 30),
    'font_small':         (BODY, 20),
    'font_captions':      (BODY, 30),
    'font_subtitles':     (BODY, 30),
    'font_slot_quantity': (BODY, 20),
    'font_region_title':  (TITLE, 48),
    'font_title':         (TITLE, 36),
}

TEXT_COLORS = {
    'menu_normal': (238, 206, 120),
    'menu_bonus': (140, 220, 110),
    'menu_penalty': (240, 70, 56),
    'widget_normal': (255, 232, 170),
    'widget_disabled': (116, 96, 78),
    'combat_givedmg': (255, 240, 200),
    'combat_takedmg': (230, 56, 40),
    'combat_crit': (255, 206, 40),
    'combat_buff': (140, 220, 110),
    'combat_miss': (128, 110, 100),
    'requirements_not_met': (240, 70, 56),
    'item_bonus': (140, 220, 110),
    'item_penalty': (240, 70, 56),
    'item_flavor': (196, 164, 120),
    'hardcore_color_name': (240, 70, 56),
}

def engine_files():
    """font_settings.txt (pixel fonts for Latin languages; CJK/Thai/Hindi keep
    fantasycore's fonts) and font_colors.txt."""
    base = open(os.path.join(MODS, 'fantasycore', 'engine', 'font_settings.txt')).read()
    blocks = re.split(r'\n(?=\[font\])', base)
    outb = ['# GENERATED by tools/gen_theme.py\n# Pixel fonts: Jersey 10 / Jacquard 12 (SIL OFL, see fonts/OFL.txt)\n']
    for b in blocks:
        m = re.search(r'^id=(\S+)', b, re.M)
        if not m:
            continue
        fid = m.group(1)
        if fid in FONT_STYLES:
            f, size = FONT_STYLES[fid]
            b = re.sub(r'^style=default,.*$', 'style=default,%s,%d,0' % (f, size), b, flags=re.M)
            # pt/en/es/etc. use the default style; make sure accents render with the pixel font
        outb.append(b.strip() + '\n')
    for fid in FONT_STYLES:
        if not re.search(r'^id=%s$' % fid, base, re.M):
            f, size = FONT_STYLES[fid]
            outb.append('[font]\nid=%s\nstyle=default,%s,%d,0\n' % (fid, f, size))
    p = out('engine/font_settings.txt')
    open(p, 'w').write('\n'.join(outb))
    # fantasycore's fonts are referenced by file name only; copy the ones the
    # non-Latin styles still need is unnecessary -- mods are layered, fantasycore/fonts stays visible.
    col = ['# GENERATED by tools/gen_theme.py']
    for k, v in TEXT_COLORS.items():
        col.append('%s=%d,%d,%d' % ((k,) + v))
    open(out('engine/font_colors.txt'), 'w').write('\n'.join(col) + '\n')


# ---------------------------------------------------------------- main
def widgets():
    save(button_sheet(192, 24), 'images/menus/buttons/button_default.png')
    for name, icon in [('button_x', icon_x), ('button_plus', icon_plus), ('button_sort', icon_sort),
                       ('button_config', icon_gear), ('upgrade', icon_up_arrow)]:
        save(button_sheet(32, 19, icon), 'images/menus/buttons/%s.png' % name)
    save(arrow_sheet(21, 28, 'up'), 'images/menus/buttons/up.png')
    save(arrow_sheet(21, 28, 'down'), 'images/menus/buttons/down.png')
    save(arrow_sheet(28, 21, 'left'), 'images/menus/buttons/left.png')
    save(arrow_sheet(28, 21, 'right'), 'images/menus/buttons/right.png')
    save(checkbox_sheet(19), 'images/menus/buttons/checkbox_default.png')
    save(listbox_sheet(304, 16), 'images/menus/buttons/listbox_default.png')
    save(listbox_sheet(304, 16), 'images/menus/buttons/listbox_char.png')
    save(scrollbar_sheet(16), 'images/menus/buttons/scrollbar_default.png')
    save(slider_sheet(210, 19), 'images/menus/buttons/slider_default.png')
    save(input_sheet(128, 16), 'images/menus/input.png')
    save(tab_image(256, 24, True), 'images/menus/tab_active.png')
    save(tab_image(256, 24, False), 'images/menus/tab_inactive.png')
    save(slot(32, 'empty'), 'images/menus/slot_empty.png')
    save(slot(32, 'selected'), 'images/menus/slot_selected.png')
    t = tooltip_bg(512, 512)
    save(t, 'images/menus/tooltips.png')

def bars():
    save(bar_fill(239, 13, 'hp'), 'images/menus/bar_hp.png')
    save(bar_background(256, 16, 239, 13, 9, 1), 'images/menus/bar_hp_background.png')
    save(bar_fill(239, 13, 'mp'), 'images/menus/bar_mp.png')
    save(bar_background(256, 16, 239, 13, 9, 1), 'images/menus/bar_mp_background.png')
    save(bar_fill(123, 3, 'xp'), 'images/menus/bar_xp.png')
    save(bar_background(128, 8, 123, 3, 2, 2), 'images/menus/bar_xp_background.png')
    save(bar_fill(98, 13, 'enemy'), 'images/menus/bar_enemy_hp.png')

def preview():
    """Contact sheet of everything generated, for eyeballing."""
    files = []
    for dp, dn, fn in os.walk(os.path.join(ROOT, 'images', 'menus')):
        for f in sorted(fn):
            if f.endswith('.png'):
                files.append(os.path.join(dp, f))
    ims = [(os.path.relpath(f, ROOT), Image.open(f).convert('RGBA')) for f in files]
    W = 1400
    x = y = 8; rowh = 0; placed = []
    for name, im in ims:
        w, h = min(im.width, 700), min(im.height, 420)
        if x + w > W - 8:
            x = 8; y += rowh + 24; rowh = 0
        placed.append((name, im.crop((0, 0, w, h)), x, y))
        x += w + 12; rowh = max(rowh, h)
    sheet = Image.new('RGBA', (W, y + rowh + 30), (60, 64, 60, 255))
    d = ImageDraw.Draw(sheet)
    for name, im, x, y in placed:
        sheet.alpha_composite(im, (x, y + 14))
        d.text((x, y), name.split('/')[-1], fill=(255, 255, 255, 255))
    sheet.save(os.path.join(ROOT, 'preview.png'))

if __name__ == '__main__':
    install_fonts()
    engine_files()
    widgets()
    hud()
    devkit()
    panels()
    screens()
    touch()
    if '--preview' in sys.argv:
        preview()
    print('theme written to', ROOT)
