#!/usr/bin/env python3
"""Gera as imagens do HUD do Random Dungeon em mods/random_dungeon/images/menus/hud/.
Layout estilo mobile: retrato com nivel no canto, barras de vida/mana ao lado,
quadro de onda/tempo no topo, botao de ataque grande, joystick virtual.
Usa as imagens do fantasycore como base (barras, moldura) e desenha o resto.
Uso: python3 tools/gen_hud_assets.py"""
import os
from PIL import Image, ImageDraw

ROOT = os.path.join(os.path.dirname(__file__), '..')
SRC = os.path.join(ROOT, 'mods/fantasycore/images/menus')
OUT = os.path.join(ROOT, 'mods/random_dungeon/images/menus/hud')
os.makedirs(OUT, exist_ok=True)

GOLD = (196, 160, 92, 255)
GOLD_DARK = (110, 84, 44, 255)
PANEL = (20, 16, 12, 200)


def src(name):
    return Image.open(os.path.join(SRC, name)).convert('RGBA')


def save(im, name):
    im.save(os.path.join(OUT, name))
    print(name, im.size)


def scaled(name, size):
    return src(name).resize(size, Image.LANCZOS)


# barras de vida/mana: fundo 336x24, preenchimento 314x18 (offset 11,3 no menus/hp.txt)
for kind in ('hp', 'mp'):
    save(scaled('bar_%s_background.png' % kind, (336, 24)), 'bar_%s_background.png' % kind)
    save(scaled('bar_%s.png' % kind, (314, 18)), 'bar_%s.png' % kind)

# barra de XP embaixo do retrato: fundo 124x10, preenchimento 118x4 (offset 3,3)
save(scaled('bar_xp_background.png', (124, 10)), 'bar_xp_background.png')
save(scaled('bar_xp.png', (118, 4)), 'bar_xp.png')

# moldura do retrato (124x124, o retrato em si e desenhado em 112x112 no offset 6,6)
save(scaled('portrait_border.png', (124, 124)), 'portrait_frame.png')


def panel(size, radius=8, fill=PANEL, border=GOLD, inner=GOLD_DARK):
    im = Image.new('RGBA', size, (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    w, h = size
    d.rounded_rectangle((0, 0, w - 1, h - 1), radius, fill=fill, outline=border, width=2)
    d.rounded_rectangle((3, 3, w - 4, h - 4), max(1, radius - 3), outline=inner, width=1)
    return im


# selo do nivel (canto do retrato)
save(panel((40, 30), radius=5, fill=(46, 34, 14, 235)), 'level_badge.png')

# quadro do topo: onda + tempo
save(panel((320, 52), radius=12), 'horde_box.png')

# quadro do alvo (canto superior direito)
save(panel((260, 72), radius=10), 'target_box.png')

# moldura do botao de ataque: 112x112, o slot M1 (64x64) fica centralizado
save(panel((112, 112), radius=14, fill=(30, 22, 14, 210)), 'attack_frame.png')

# botao de configuracao 64x64, 4 estados empilhados (normal, pressionado, hover, desabilitado)
slot = src('slot_empty.png')
cfg = src('buttons/button_config.png')  # 64x152 = 4 estados de 64x38
btn = Image.new('RGBA', (64, 256), (0, 0, 0, 0))
for i in range(4):
    state = slot.copy()
    gear = cfg.crop((0, i * 38, 64, i * 38 + 38))
    state.alpha_composite(gear, (0, 13))
    btn.paste(state, (0, i * 64))
save(btn, 'button_config.png')

# joystick virtual (so aparece com touch_controls=1): anel + setas N/S/L/O
J = 256
joy = Image.new('RGBA', (J, J), (0, 0, 0, 0))
d = ImageDraw.Draw(joy)
d.ellipse((4, 4, J - 5, J - 5), fill=(60, 46, 26, 150), outline=GOLD, width=6)
d.ellipse((24, 24, J - 25, J - 25), outline=GOLD_DARK, width=2)
c = J // 2
for dx, dy in ((0, -1), (0, 1), (-1, 0), (1, 0)):
    tip = (c + dx * 108, c + dy * 108)
    base = (c + dx * 80, c + dy * 80)
    side = (dy * 18, dx * 18)
    d.polygon([tip, (base[0] + side[0], base[1] + side[1]), (base[0] - side[0], base[1] - side[1])], fill=GOLD)
save(joy, 'joystick.png')
knob = Image.new('RGBA', (96, 96), (0, 0, 0, 0))
ImageDraw.Draw(knob).ellipse((2, 2, 93, 93), fill=(28, 22, 14, 230), outline=GOLD, width=4)
save(knob, 'joystick_knob.png')

# icones dos botoes de menu: no fantasycore eles fazem parte do actionbar_trim.png
# (que o HUD nao desenha), entao recorta cada um em 64x64
trim = src('actionbar_trim.png')
for name, x in (('character', 960), ('inventory', 1024), ('powers', 1088), ('log', 1152)):
    save(trim.crop((x, 6, x + 64, 70)), 'menu_%s.png' % name)
