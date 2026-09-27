#!/usr/bin/env python3
"""
Exports the game's catalog for the Random Dungeon wiki (a published page with
a shared database where the owner edits stats; see README "Wiki").

  python3 tools/wiki_export.py <out_dir>

writes into <out_dir>:
  docs/items/<id>.json, docs/enemies/<id>.json, docs/loot/<grade>.json
      one JSON document per wiki row (editable fields + read-only extras)
  icons.png    item icons (the Flare-art sheet, tinted; same order as items.csv)
  enemies.png  one portrait per enemy (first stance frame, with its color filter)

Only our own art goes out: the Exiled Kingdoms icons (mods/ek_icons) are
personal-use and are never exported.
"""
import csv, json, os, re, sys
from PIL import Image

TOOLS = os.path.dirname(os.path.abspath(__file__))
MOD = os.path.dirname(TOOLS)
MODS = os.path.dirname(MOD)
DATA = os.path.join(TOOLS, 'ek_data')
sys.path.insert(0, TOOLS)
import gen_content  # noqa: E402  (reuses PALETTE, templates, base-file reader)

PORTRAIT = 96
KIND_LABEL = {'melee': 'Corpo a corpo', 'ranged': 'Distância', 'ment': 'Magia'}


def locate(p):
    for m in [os.path.basename(MOD), 'empyrean_campaign', 'fantasycore']:
        f = os.path.join(MODS, m, p)
        if os.path.exists(f):
            return f


def item_kind(it):
    base = it['base']
    name = os.path.basename(base)[:-4]
    if '/weapons/' in base:
        return 'weapon', name.replace('_', ' ')
    if '/armor/' in base:
        if name == 'belt':
            return 'accessory', 'belt'
        return 'armor', '%s %s' % (base.split('/')[-2], name)
    if '/shields/' in base:
        return 'shield', 'shield'
    if '/rings/' in base:
        return 'accessory', 'ring'
    if '/necklaces/' in base:
        return 'accessory', 'amulet'
    return 'other', name


def export_items(out):
    items = list(csv.DictReader(open(os.path.join(DATA, 'items.csv'), encoding='utf8')))
    for n, it in enumerate(items):
        kind, typ = item_kind(it)
        doc = {k: it.get(k, '') for k in ('name', 'name_pt', 'band', 'tint', 'bonuses', 'dmg_type', 'slayer', 'classes')}
        for k in ('level', 'dmg_min', 'dmg_max', 'abs_min', 'abs_max', 'price'):
            doc[k] = int(it[k]) if it[k] != '' else None
        doc.update(id=int(it['id']), kind=kind, type=typ, two_handed=it['two_handed'] == '1',
                   icon=n, base=it['base'], notes='', pending=False)
        json.dump(doc, open(os.path.join(out, 'docs/items/%s.json' % it['id']), 'w'), ensure_ascii=False)
    # icon sheet = the in-game Flare-art sheet (8 per row, 64px)
    Image.open(os.path.join(MOD, 'images/icons/icons_rd.png')).save(os.path.join(out, 'icons.png'), optimize=True)
    return len(items)


def template_stats(name):
    """level-1 values and per-level growth of an enemy template (before our multipliers)."""
    st = {}
    for line in gen_content.template_lines(name):
        m = re.match(r'(stat|stat_per_level)=(hp|dmg_\w+|absorb_\w+|accuracy|avoidance),(\d+)', line.strip())
        if m:
            st['%s:%s' % (m.group(1), m.group(2))] = int(m.group(3))
    def pick(kind, key):
        return st.get('%s:%s' % (kind, key), 0)
    dmg_min = max(pick('stat', k) for k in ('dmg_melee_min', 'dmg_ranged_min', 'dmg_ment_min'))
    dmg_max = max(pick('stat', k) for k in ('dmg_melee_max', 'dmg_ranged_max', 'dmg_ment_max'))
    dmg_lvl = max(pick('stat_per_level', k) for k in ('dmg_melee_min', 'dmg_ranged_min', 'dmg_ment_min'))
    return dict(base_hp=pick('stat', 'hp'), base_hp_per_level=pick('stat_per_level', 'hp'),
                base_dmg_min=dmg_min, base_dmg_max=dmg_max, base_dmg_per_level=dmg_lvl,
                base_absorb_min=pick('stat', 'absorb_min'), base_absorb_max=pick('stat', 'absorb_max'))


def first_frame(anim_rel):
    """(image, crop box, color, alpha) of the first stance frame, following INCLUDEs."""
    color, alpha, image, images, frame = (255, 255, 255), 255, None, {}, None
    path = anim_rel
    lines = []
    def load(p):
        for line in open(locate(p), encoding='utf8'):
            line = line.strip()
            if line.startswith('INCLUDE '):
                load(line.split(None, 1)[1])
            else:
                lines.append(line)
    load(path)
    section = ''
    for line in lines:
        if line.startswith('['):
            section = line
        elif line.startswith('color_mod='):
            color = tuple(int(v) for v in line[10:].split(','))
        elif line.startswith('alpha_mod='):
            alpha = int(line[10:])
        elif line.startswith('image='):
            parts = line[6:].split(',')
            images[parts[1] if len(parts) > 1 else ''] = parts[0]
        elif section == '[stance]' and line.startswith('frame=') and frame is None:
            v = line[6:].split(',')
            if v[1] == '6':  # direction 6 faces the camera
                frame = v
    if frame is None:
        return None
    img = images.get(frame[8] if len(frame) > 8 else '', images.get('', list(images.values())[0]))
    x, y, w, h = (int(v) for v in frame[2:6])
    return img, (x, y, x + w, y + h), color, alpha


def export_enemies(out):
    enemies = list(csv.DictReader(open(os.path.join(DATA, 'enemies.csv'), encoding='utf8')))
    enemies.append(dict(id='rd_dummy', name='Training Dummy', name_pt='Boneco de Treino', ek_level='1', family='Dev',
                        template='', animation='fantasycore:skeleton_weak', tint='', grade='normal', hp_mult='1',
                        dmg_mult='0', resists=''))
    cols = 8
    sheet = Image.new('RGBA', (cols * PORTRAIT, ((len(enemies) + cols - 1) // cols) * PORTRAIT), (0, 0, 0, 0))
    for n, e in enumerate(enemies):
        anim = 'animations/enemies/rd/%s.txt' % e['id'] if locate('animations/enemies/rd/%s.txt' % e['id']) \
            else 'animations/enemies/%s.txt' % e['animation'].split(':')[1]
        ff = first_frame(anim)
        if ff:
            img, box, color, alpha = ff
            im = Image.open(locate(img)).convert('RGBA').crop(box)
            r, g, b, a = im.split()
            im = Image.merge('RGBA', (r.point(lambda v: v * color[0] // 255), g.point(lambda v: v * color[1] // 255),
                                      b.point(lambda v: v * color[2] // 255), a.point(lambda v: v * alpha // 255)))
            im.thumbnail((PORTRAIT - 4, PORTRAIT - 4), Image.LANCZOS)
            sheet.alpha_composite(im, ((n % cols) * PORTRAIT + (PORTRAIT - im.width) // 2,
                                       (n // cols) * PORTRAIT + PORTRAIT - 2 - im.height))
        doc = {k: e[k] for k in ('name', 'name_pt', 'family', 'grade', 'tint', 'resists', 'template')}
        doc.update(id=e['id'], ek_level=int(e['ek_level']), hp_mult=float(e['hp_mult']), dmg_mult=float(e['dmg_mult']),
                   portrait=n, notes='', pending=False, editable=e['id'] != 'rd_dummy')
        if e['template']:
            doc.update(template_stats(e['template']))
        else:
            doc.update(base_hp=1000000, base_hp_per_level=0, base_dmg_min=0, base_dmg_max=0, base_dmg_per_level=0,
                       base_absorb_min=0, base_absorb_max=0)
        json.dump(doc, open(os.path.join(out, 'docs/enemies/%s.json' % e['id']), 'w'), ensure_ascii=False)
    sheet.save(os.path.join(out, 'enemies.png'), optimize=True)
    return len(enemies)


def export_loot(out):
    for b in csv.DictReader(open(os.path.join(DATA, 'loot_bands.csv'), encoding='utf8')):
        doc = {'grade': b['grade'], 'gold_qty': b['gold_qty'], 'gold_qty_per_level': b['gold_qty_per_level'],
               'notes': '', 'pending': False}
        for k in ('gold', 'potion', 'common', 'uncommon', 'rare', 'unique'):
            doc[k] = float(b[k])
        for k in ('level_below', 'level_above', 'drops_min', 'drops_max'):
            doc[k] = int(b[k])
        json.dump(doc, open(os.path.join(out, 'docs/loot/%s.json' % b['grade']), 'w'), ensure_ascii=False)


def main():
    out = sys.argv[1]
    for d in ('items', 'enemies', 'loot'):
        os.makedirs(os.path.join(out, 'docs', d), exist_ok=True)
    ni = export_items(out)
    ne = export_enemies(out)
    export_loot(out)
    # everything in one file too: the page's baseline (and "revert"), and its
    # read-only fallback when the shared database isn't available
    cat = {}
    for d in ('items', 'enemies', 'loot'):
        folder = os.path.join(out, 'docs', d)
        cat[d] = [json.load(open(os.path.join(folder, f))) for f in sorted(os.listdir(folder))]
    json.dump(cat, open(os.path.join(out, 'catalog.json'), 'w'), ensure_ascii=False, separators=(',', ':'))
    print('wiki export: %d items, %d enemies, 3 loot grades -> %s' % (ni, ne, out))


if __name__ == '__main__':
    main()
