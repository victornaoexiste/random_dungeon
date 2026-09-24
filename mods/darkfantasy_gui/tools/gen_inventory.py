# Gera images/menus/inventory.png (600x690) a partir das MESMAS coordenadas de menus/inventory.txt.
import re, os
from PIL import Image, ImageDraw, ImageFilter
import random
here = os.path.dirname(os.path.abspath(__file__)); root = os.path.dirname(here)
W, H, S = 600, 690, 64
cfg = open(os.path.join(root, 'menus/inventory.txt')).read()
slots = {(int(m[0]), int(m[1]), m[2]) for m in re.findall(r'equipment_slot=(\d+),(\d+),(\w+),1', cfg)}
cx, cy = map(int, re.search(r'carried_area=(\d+),(\d+)', cfg).groups())
cols = int(re.search(r'carried_cols=(\d+)', cfg).group(1)); rows = int(re.search(r'carried_rows=(\d+)', cfg).group(1))
random.seed(7)
img = Image.new('RGBA', (W, H), (18, 14, 16, 255))
px = img.load()
for y in range(H):
    for x in range(W):                      # textura de pedra escura
        n = random.randint(-6, 6); r, g, b, a = px[x, y]; px[x, y] = (r+n, g+n, b+n+1, 255)
d = ImageDraw.Draw(img)
def slot(x, y, w=S, h=S, hl=False):
    d.rectangle([x, y, x+w-1, y+h-1], fill=(8, 6, 8, 255))
    d.rectangle([x, y, x+w-1, y+h-1], outline=(70, 52, 40, 255))
    d.line([x+1, y+1, x+w-2, y+1], fill=(0, 0, 0, 255)); d.line([x+1, y+1, x+1, y+h-2], fill=(0, 0, 0, 255))
    d.line([x+1, y+h-2, x+w-2, y+h-2], fill=(40, 30, 26, 255))
    if hl: d.rectangle([x+3, y+3, x+w-4, y+h-4], outline=(110, 24, 24, 255))
def panel(x, y, w, h):
    d.rectangle([x, y, x+w-1, y+h-1], fill=(12, 9, 11, 255), outline=(96, 72, 44, 255))
    d.rectangle([x+2, y+2, x+w-3, y+h-3], outline=(38, 28, 22, 255))
# moldura externa dourada envelhecida + faixa do titulo
d.rectangle([0, 0, W-1, H-1], outline=(120, 92, 52, 255), width=2)
d.rectangle([4, 4, W-5, H-5], outline=(46, 34, 26, 255))
d.rectangle([6, 6, W-7, 36], fill=(26, 12, 14, 255), outline=(110, 24, 24, 255))
for cxx in (12, W-13):                       # rebites nos cantos
    for cyy in (12, H-13): d.ellipse([cxx-3, cyy-3, cxx+3, cyy+3], fill=(120, 92, 52, 255))
panel(32, 32, 208, 272)        # area do paperdoll
for (x, y, _) in slots: slot(x, y, hl=True)
panel(cx-8, cy-8, cols*S + 16, rows*S + 16) # area da mochila
for r in range(rows):
    for c in range(cols): slot(cx + c*S, cy + r*S)
panel(360, 652, 208, 32)            # faixa do ouro
img = img.filter(ImageFilter.GaussianBlur(0.35)) if False else img
img.save(os.path.join(root, 'images/menus/inventory.png'))
print('ok', img.size, len(slots), 'slots de equipamento;', cols*rows, 'da mochila')
