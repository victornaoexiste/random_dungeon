#!/usr/bin/env python3
"""
Bootstrap: builds items.csv and enemies.csv from the Exiled Kingdoms wiki
tables cached in wiki_cache/ (see parse_wiki_table.py for how they were
scraped from exiledkingdoms.com/wiki: Weapon_Table, Armor_Table, Bestiary).

Run this ONCE to (re)create the spreadsheets. After that, edit items.csv /
enemies.csv by hand and run ../gen_content.py -- re-running this bootstrap
OVERWRITES your manual edits (it refuses unless you pass --force).

EK -> Flare conversion rules (all tweakable below):
  * Weapon damage: Flare scale avg = (7 + 3*level) * type_mult * material_mult,
    spread (min/max ratio) copied from the EK item. EK speed doesn't exist in
    Flare, so fast weapons get EK's crit% as a bonus instead.
  * Elemental "+N": same proportion of the physical damage as in EK.
    EK Fire->fire, Cold->ice, Shock->lightning, Poison/Death->dark, Holy->fire.
  * Armor: abs centered on (EK armor + level/4); EK health/mana x4 (Flare hero
    has ~4x EK's hp pool); EK resistances copied 1:1 (capped at 50).
  * Enemies: Flare "il_*" model (level-1 stats + per-level growth, level set by
    hero level in the dungeon or by wave in horde mode). EK hp/dps relative to
    a typical EK monster of the same level becomes a strength multiplier.
"""
import csv, math, os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
CACHE = os.path.join(HERE, 'wiki_cache')


def load(name):
    rows = list(csv.reader(open(os.path.join(CACHE, name), encoding='utf8')))
    out = {}
    for r in rows:
        if len(r) < 3 or r[0] in ('Name', ''):
            continue
        out.setdefault(r[0], r)
    return out


W = load('weapons.csv')    # Name,Type,Category,Class,Rarity,Damage,Speed,Crit,DPS,Attribute,icon
A = load('armor.csv')      # Name,Group,Slot,Class,Rarity,Armor,Health,Mana,Attribute,Resistances,Traits,icon
B = load('bestiary.csv')   # Name,Type,Level,XP,Health,Armor,Damage,DPS,Resistances,icon

ELEM = {'Fire': 'fire', 'Cold': 'ice', 'Shock': 'lightning', 'Poison': 'dark', 'Death': 'dark', 'Holy': 'fire'}
RARITY = {'Common': 'common', 'Limited': 'common', 'Uncommon': 'uncommon', 'Crafted': 'rare', 'Unique': 'unique'}

# key: (flare base item, type damage multiplier, two-handed)
WTYPES = {
    'Dagger':     ('items/base/weapons/melee/dagger.txt',      0.80, False),
    'Shortsword': ('items/base/weapons/melee/shortsword.txt',  0.88, False),
    'Longsword':  ('items/base/weapons/melee/longsword.txt',   1.00, False),
    'Greatsword': ('items/base/weapons/melee/greatsword.txt',  1.40, True),
    'Axe':        ('items/base/weapons/melee/hand_axe.txt',    0.98, False),
    'Greataxe':   ('items/base/weapons/melee/battle_axe.txt',  1.45, True),
    'Mace':       ('items/base/weapons/melee/mace.txt',        0.95, False),
    'Hammer':     ('items/base/weapons/melee/war_hammer.txt',  0.92, False),
    'Maul':       ('items/base/weapons/melee/maul.txt',        1.40, True),
    'Bow':        ('items/base/weapons/ranged/shortbow.txt',   1.00, False),
    'Longbow':    ('items/base/weapons/ranged/longbow.txt',    1.10, False),
    'Greatbow':   ('items/base/weapons/ranged/greatbow.txt',   1.20, False),
    'Wand':       ('items/base/weapons/magic/wand.txt',        1.00, False),
    'Rod':        ('items/base/weapons/magic/rod.txt',         1.05, False),
    'Staff':      ('items/base/weapons/magic/staff.txt',       1.15, False),
    'Greatstaff': ('items/base/weapons/magic/greatstaff.txt',  1.30, False),
}
DMG_TYPE = {'Bow': 'ranged', 'Longbow': 'ranged', 'Greatbow': 'ranged',
            'Wand': 'ment', 'Rod': 'ment', 'Staff': 'ment', 'Greatstaff': 'ment'}
EK_CAT = {'Bow': 'Bow', 'Longbow': 'Bow', 'Greatbow': 'Bow', 'Wand': 'Wand', 'Rod': 'Wand',
          'Staff': 'Staff', 'Greatstaff': 'Staff'}

# material: level, band, damage mult, tint, extra bonuses
MATERIALS = [
    ('Iron',       'de Ferro',      1,  1.00, 'iron',       ''),
    ('Steel',      'de Aço',        4,  1.00, '',           ''),
    ('Silver',     'de Prata',      6,  0.95, 'silver',     ''),
    ('Bluesteel',  'de Aço Azul',   9,  1.00, 'bluesteel',  ''),
    ('Adamantite', 'de Adamantita', 13, 1.10, 'adamantite', 'crit:2'),
]
TYPE_PT = {'Dagger': 'Adaga', 'Shortsword': 'Espada Curta', 'Longsword': 'Espada Longa',
           'Greatsword': 'Espadão', 'Axe': 'Machado', 'Greataxe': 'Machado de Guerra',
           'Mace': 'Maça', 'Hammer': 'Martelo', 'Maul': 'Marreta'}
TYPE_EN = {'Hammer': 'Hammer', 'Axe': 'Axe'}

# magic (uncommon) weapons straight from EK: name, type key, level
MAGIC = [
    ('Goblin Flesh-Cutter', 'Dagger', 2, 'Retalhadora Goblin'),
    ('Dagger of Ice', 'Dagger', 3, 'Adaga de Gelo'),
    ('Dagger of Poison', 'Dagger', 3, 'Adaga Envenenada'),
    ('Shock Dagger', 'Dagger', 3, 'Adaga de Choque'),
    ('Hunter\'s Fang', 'Dagger', 4, 'Presa do Caçador'),
    ('Sharp Orc Knife', 'Dagger', 4, 'Faca Orc Afiada'),
    ('Shortsword of Frost', 'Shortsword', 5, 'Espada Curta do Gelo'),
    ('Thief\'s Blade', 'Shortsword', 5, 'Lâmina do Ladrão'),
    ('Corsair Falchion', 'Shortsword', 6, 'Falcione do Corsário'),
    ('Sword of Flames', 'Longsword', 6, 'Espada das Chamas'),
    ('Sword of Shock', 'Longsword', 6, 'Espada do Choque'),
    ('Orcslicer', 'Longsword', 7, 'Fatiador de Orcs'),
    ('Firesplitter', 'Axe', 7, 'Racha-Fogo'),
    ('Lightning Axe', 'Axe', 7, 'Machado Relâmpago'),
    ('Heavy Scimitar', 'Greatsword', 8, 'Cimitarra Pesada'),
    ('Bone Mace', 'Mace', 8, 'Maça de Osso'),
    ('Hammer of Corrosion', 'Hammer', 8, 'Martelo da Corrosão'),
    ('Icy Greatsword', 'Greatsword', 9, 'Espadão Gélido'),
    ('Icy Maul', 'Maul', 9, 'Marreta Gélida'),
    ('Screaming Mace', 'Mace', 9, 'Maça Uivante'),
    ('Justice', 'Longsword', 9, 'Justiça'),
    ('Lightning Greataxe', 'Greataxe', 10, 'Machado de Guerra Relâmpago'),
    ('Minotaur Axe', 'Greataxe', 10, 'Machado do Minotauro'),
    ('Bone Maul', 'Maul', 10, 'Marreta de Osso'),
    ('Magma Shortsword', 'Shortsword', 10, 'Espada Curta de Magma'),
    ('Magma Dagger', 'Dagger', 10, 'Adaga de Magma'),
    ('Magma Longsword', 'Longsword', 11, 'Espada Longa de Magma'),
    ('Executioner\'s Axe', 'Greataxe', 11, 'Machado do Carrasco'),
    ('Lightning Blade', 'Longsword', 12, 'Lâmina Relâmpago'),
    ('Soul Cleaver', 'Axe', 12, 'Cutelo de Almas'),
    ('Soul Drinker', 'Greataxe', 14, 'Bebedor de Almas'),
    # ranged / magic
    ('Short Bow', 'Bow', 1, 'Arco Curto'),
    ('Hunting Bow', 'Bow', 3, 'Arco de Caça'),
    ('Oaken Bow', 'Longbow', 5, 'Arco de Carvalho'),
    ('Silver Bow', 'Longbow', 7, 'Arco de Prata'),
    ('Frost Bow', 'Longbow', 8, 'Arco do Gelo'),
    ('Poisonous Longbow', 'Longbow', 11, 'Arco Longo Venenoso'),
    ('Skeletal Bow', 'Greatbow', 12, 'Arco Esquelético'),
    ('Oak Wand', 'Wand', 1, 'Varinha de Carvalho'),
    ('Staff of Force', 'Staff', 3, 'Cajado da Força'),
    ('Elm Wand', 'Wand', 4, 'Varinha de Olmo'),
    ('Caduceus of Scorching', 'Staff', 6, 'Caduceu Escaldante'),
    ('Staff of Mighty Missiles', 'Staff', 7, 'Cajado dos Mísseis'),
    ('Rod of Incineration', 'Rod', 8, 'Bastão da Incineração'),
    ('Staff of Storms', 'Staff', 9, 'Cajado das Tempestades'),
    ('Staff of the Arcanist', 'Greatstaff', 11, 'Cajado do Arcanista'),
    ('Magma Greatstaff', 'Greatstaff', 14, 'Grande Cajado de Magma'),
]
UNIQUES = [
    ('Stiletto', 'Dagger', 8, 'Estilete'),
    ('Hammer of the White Priest', 'Hammer', 8, 'Martelo do Sacerdote Branco'),
    ('Winter Wail', 'Longsword', 9, 'Lamento do Inverno'),
    ('Cruelty', 'Greatsword', 10, 'Crueldade'),
    ('Magma Axe', 'Axe', 10, 'Machado de Magma'),
    ('Lord\'s Fist', 'Mace', 10, 'Punho do Lorde'),
    ('Despair', 'Hammer', 10, 'Desespero'),
    ('Gurguth\'s Maul', 'Maul', 10, 'Marreta de Gurguth'),
    ('Composite Bow', 'Greatbow', 10, 'Arco Composto'),
    ('Queen\'s Heart', 'Dagger', 12, 'Coração da Rainha'),
    ('Dancing Flame', 'Shortsword', 12, 'Chama Dançante'),
    ('Melporth\'s Sword', 'Longsword', 12, 'Espada de Melporth'),
    ('Maul of Garrak', 'Maul', 12, 'Marreta de Garrak'),
    ('Ancestral Blade of the Deer', 'Longsword', 13, 'Lâmina Ancestral do Cervo'),
    ('Permafrost Bow', 'Greatbow', 13, 'Arco do Permafrost'),
    ('Scepter of the Underworld', 'Wand', 13, 'Cetro do Submundo'),
    ('Shadow Blade', 'Shortsword', 14, 'Lâmina Sombria'),
    ('Axe of the Minotaur Kings', 'Greataxe', 14, 'Machado dos Reis Minotauros'),
    ('Midnight', 'Mace', 14, 'Meia-Noite'),
    ('Demonclaw', 'Greatsword', 15, 'Garra Demoníaca'),
    ('Wand of Power', 'Wand', 15, 'Varinha do Poder'),
    ('Staff of the Dead Kings', 'Greatstaff', 16, 'Cajado dos Reis Mortos'),
]

# armor sets: EK group, flare base dir, level, tint, pt name
SETS = [
    ('Leather', 'leather', 1, '', 'de Couro'),
    ('Apprentice', 'mage', 1, '', 'de Aprendiz'),
    ('Hardened Leather', 'leather', 4, 'hardened', 'de Couro Endurecido'),
    ('Journeyman', 'mage_alt1', 4, '', 'de Viajante'),
    ('Chainmail', 'chain', 5, '', 'de Cota de Malha'),
    ('Forest Chain', 'chain', 6, 'forest', 'de Malha da Floresta'),
    ('Blessed Chain', 'chain', 7, 'blessed', 'de Malha Abençoada'),
    ('Conjurer', 'mage_alt2', 7, '', 'de Conjurador'),
    ('Assassin\'s', 'leather', 8, 'shadow', 'do Assassino'),
    ('Platemail', 'plate', 9, '', 'de Placas'),
    ('Legion', 'plate', 11, 'gold', 'da Legião'),
    ('Primal Ice', 'chain', 11, 'ice', 'de Gelo Primordial'),
    ('Blessed Platemail', 'plate', 12, 'blessed', 'de Placas Abençoadas'),
    ('Myrosian', 'plate', 14, 'crimson', 'de Mirósia'),
    ('Ashen', 'plate', 15, 'ashen', 'das Cinzas'),
]
SLOT = {'Head': 'head', 'Body': 'chest', 'Hands': 'hands', 'Legs': 'legs', 'Feet': 'feet'}
SLOT_PT = {'Head': 'Elmo', 'Body': 'Peitoral', 'Hands': 'Luvas', 'Legs': 'Calças', 'Feet': 'Botas'}
SLOT_PT_SOFT = {'Head': 'Capuz', 'Body': 'Túnica'}  # cloth/leather/robes

# shields & accessories: EK name, flare base, level, tint, pt name, extra bonuses
OTHERS = [
    ('Buckler', 'items/base/shields/wood.txt', 1, '', 'Broquel', ''),
    ('Iron Shield', 'items/base/shields/iron.txt', 4, '', 'Escudo de Ferro', ''),
    ('Charred Buckler', 'items/base/shields/wood.txt', 6, 'fire', 'Broquel Chamuscado', ''),
    ('Anointed Shield', 'items/base/shields/kite.txt', 7, '', 'Escudo Ungido', ''),
    ('Petrified Wood Buckler', 'items/base/shields/wood.txt', 9, 'ashen', 'Broquel de Madeira Petrificada', ''),
    ('Hero\'s Shield', 'items/base/shields/steel.txt', 10, '', 'Escudo do Herói', ''),
    ('Lesser Ring of Flames', 'items/base/rings/gold_2.txt', 2, '', 'Anel Menor das Chamas', ''),
    ('Lesser Ring of Ice', 'items/base/rings/silver_3.txt', 2, '', 'Anel Menor do Gelo', ''),
    ('Lesser Ring of Learning', 'items/base/rings/silver_1.txt', 2, '', 'Anel Menor do Aprendizado', 'xp_gain:5'),
    ('Lesser Ring of Death Ward', 'items/base/rings/silver_4.txt', 3, '', 'Anel Menor da Proteção Mortal', ''),
    ('Ring of Flames', 'items/base/rings/gold_2.txt', 6, '', 'Anel das Chamas', ''),
    ('Ring of Ice', 'items/base/rings/silver_3.txt', 6, '', 'Anel do Gelo', ''),
    ('Ring of Health', 'items/base/rings/gold_5.txt', 6, '', 'Anel da Saúde', 'hp_regen:5'),
    ('Ring of Death Ward', 'items/base/rings/silver_4.txt', 7, '', 'Anel da Proteção Mortal', ''),
    ('Ivory Ring', 'items/base/rings/silver_5.txt', 9, '', 'Anel de Marfim', ''),
    ('Greater Ring of Flames', 'items/base/rings/gold_2.txt', 11, '', 'Anel Maior das Chamas', ''),
    ('Greater Ring of Ice', 'items/base/rings/silver_3.txt', 11, '', 'Anel Maior do Gelo', ''),
    ('Greater Ring of Endurance', 'items/base/rings/gold_5.txt', 11, '', 'Anel Maior da Resistência', 'hp:60'),
    ('Blessed Necklace', 'items/base/necklaces/silver_1.txt', 3, '', 'Colar Abençoado', ''),
    ('Purple Medallion', 'items/base/necklaces/gold_1.txt', 8, '', 'Medalhão Púrpura', ''),
    ('Imperial College Emblem', 'items/base/necklaces/gold_2.txt', 10, '', 'Emblema do Colégio Imperial', ''),
    ('Necklace of the Depths', 'items/base/necklaces/silver_2.txt', 12, '', 'Colar das Profundezas', 'xp_gain:5'),
    ('Belt of Might', 'items/base/armor/belt.txt', 2, '', 'Cinto do Poder', 'physical:1'),
    ('Belt of Agility', 'items/base/armor/belt.txt', 2, '', 'Cinto da Agilidade', 'offense:1'),
    ('Belt of Flowing Thought', 'items/base/armor/belt.txt', 2, '', 'Cinto do Pensamento Fluido', 'mental:1'),
    ('Girdle of Might', 'items/base/armor/belt.txt', 7, '', 'Cinturão do Poder', 'physical:2'),
    ('Girdle of Agility', 'items/base/armor/belt.txt', 7, '', 'Cinturão da Agilidade', 'offense:2'),
    ('Girdle of Intellect', 'items/base/armor/belt.txt', 7, '', 'Cinturão do Intelecto', 'mental:2'),
    ('Girdle of the Warrior', 'items/base/armor/belt.txt', 12, '', 'Cinturão do Guerreiro', 'physical:3;defense:1'),
    ('Girdle of the Rogue', 'items/base/armor/belt.txt', 12, '', 'Cinturão do Ladino', 'offense:3;defense:1'),
    ('Girdle of Supreme Intellect', 'items/base/armor/belt.txt', 12, '', 'Cinturão do Intelecto Supremo', 'mental:3;defense:1'),
]

ATTR_MAP = [  # EK attribute text -> flare bonus
    (r'Stability', 'poise:10'), (r'Wisdom (\d)', 'xp_gain:{0}*3'), (r'Detection (\d)', 'item_find:{0}*5'),
    (r'Tinkering (\d)', 'currency_find:{0}*5'), (r'Vicious (\d)', 'crit:{0}*3'), (r'Arcane (\d)', 'mp:{0}*8'),
]


def rng(s):
    m = re.findall(r'\d+', s or '')
    if not m:
        return 1, 1
    return (int(m[0]), int(m[1])) if len(m) > 1 else (int(m[0]), int(m[0]))


def mid(s):
    a, b = rng(s)
    return (a + b) / 2


def parse_damage(s):
    """'[Sword] 4-8 +[Fire] 3' -> ((4,8), [('fire',3)])"""
    phys = re.search(r'(\d+)(?:-(\d+))?', s)
    pmin = int(phys.group(1)); pmax = int(phys.group(2) or phys.group(1))
    el = [(ELEM[e], int(n)) for e, n in re.findall(r'\+\[(\w+)\]\s*(\d+)', s) if e in ELEM]
    return (pmin, pmax), el


def parse_res(s):
    out = {}
    for e, n in re.findall(r'\[(\w+)\]\s*(-?\d+)', s or ''):
        if e in ELEM:
            k = ELEM[e] + '_resist'
            out[k] = max(out.get(k, -999), int(n))
    return out


def attrs(s):
    out = []
    for pat, tmpl in ATTR_MAP:
        m = re.search(pat, s or '')
        if m:
            k, v = tmpl.split(':')
            if '{0}' in v:
                a, b = v.split('*')
                v = str(int(m.group(1)) * int(b))
            out.append(k + ':' + v)
    return out


# EK "slayer" attributes -> extra % damage vs an enemy category (engine: item slayer=)
SLAYER_MAP = [(r'\bSilver\b', [('undead', 25)]), (r'Holy (\d)', [('undead', 10), ('demon', 10)]),
              (r'Beast Slayer (\d)', [('beast', 15)]), (r'Orc Slayer (\d)', [('greenskin', 15)]),
              (r'Banishing (\d)', [('demon', 10)])]


def slayers(attr_text):
    out = {}
    for pat, effects in SLAYER_MAP:
        m = re.search(pat, attr_text or '')
        if m:
            n = int(m.group(1)) if m.groups() else 1
            for cat, pct in effects:
                out[cat] = out.get(cat, 0) + pct * n
    return ';'.join('%s:%d' % kv for kv in sorted(out.items()))


CLASS_LETTER = {'W': 'Warrior', 'R': 'Rogue', 'C': 'Cleric', 'M': 'Mage'}


def classes(ek_class):
    """EK 'W, R' -> 'Warrior;Rogue' (empty = every class)."""
    names = [CLASS_LETTER[c.strip()] for c in (ek_class or '').split(',') if c.strip() in CLASS_LETTER]
    return ';'.join(names) if len(names) < 4 else ''


def price(level, band):
    return int(round((15 + 10 * level ** 1.4) * {'common': 1, 'uncommon': 2, 'rare': 4, 'unique': 8}[band]))


def fmt_bonus(d):
    merged = {}
    for k, v in d:
        merged[k] = merged.get(k, 0) + v
    return ';'.join('%s:%s' % (k, v) for k, v in merged.items())


rows = []


def weapon_row(iid, name, name_pt, tkey, level, band, ek, mult=1.0, tint='', extra=(), flavor=''):
    base, tmult, two = WTYPES[tkey]
    (pmin, pmax), els = parse_damage(ek[5]) if ek else ((4, 8), [])
    avg = (7 + 3 * level) * tmult * mult
    r = pmin / pmax
    dmin = int(round(2 * avg * r / (1 + r))); dmax = int(round(2 * avg / (1 + r)))
    bonus = list(extra)
    if ek and ek[7].strip().isdigit() and int(ek[7]) >= 6:
        bonus.append(('crit', int(ek[7]) - 4))
    for e, n in els:
        eavg = avg * n / ((pmin + pmax) / 2) * 0.8
        bonus.append(('dmg_%s_min' % e, max(1, int(round(eavg * 0.75)))))
        bonus.append(('dmg_%s_max' % e, max(1, int(round(eavg * 1.25)))))
    if ek:
        for b in attrs(ek[9]):
            k, v = b.split(':'); bonus.append((k, int(v)))
    rows.append(dict(id=iid, name=name, name_pt=name_pt, base=base, level=level, band=band,
                     dmg_type=DMG_TYPE.get(tkey, 'melee'), dmg_min=dmin, dmg_max=dmax,
                     abs_min='', abs_max='', two_handed=int(two), tint=tint, bonuses=fmt_bonus(bonus),
                     price=price(level, band), ek_icon=ek[-1] if ek else '', flavor=flavor,
                     slayer=slayers(ek[9]) if ek else '', classes=classes(ek[3]) if ek else ''))


def main():
    out_items = os.path.join(HERE, 'items.csv')
    out_enemies = os.path.join(HERE, 'enemies.csv')
    if (os.path.exists(out_items) or os.path.exists(out_enemies)) and '--force' not in sys.argv:
        sys.exit('items.csv/enemies.csv already exist; pass --force to overwrite your edits')

    iid = 5000
    # --- material weapons
    for tkey in ['Dagger', 'Shortsword', 'Longsword', 'Greatsword', 'Axe', 'Greataxe', 'Mace', 'Hammer', 'Maul']:
        for mat, mat_pt, lvl, mmult, tint, extra in MATERIALS:
            en_type = {'Axe': 'Axe', 'Greataxe': 'Greataxe'}.get(tkey, tkey)
            name = '%s %s' % (mat, en_type)
            ek = W.get(name)
            if not ek and mat in ('Silver',):
                continue  # EK only has silver swords/daggers/maces
            flavor = ''
            if not ek:
                ek = W.get('Bluesteel %s' % en_type)
                flavor = 'extrapolated'
            band = 'rare' if mat == 'Adamantite' else 'common'
            ex = [tuple([k, int(v)]) for k, v in (e.split(':') for e in extra.split(';') if e)]
            weapon_row(iid, name, '%s %s' % (TYPE_PT[tkey], mat_pt), tkey, lvl, band, ek, mmult, tint, ex, flavor)
            iid += 1
    iid = 5100
    for name, tkey, lvl, pt in MAGIC:
        ek = W[name]
        band = RARITY[ek[4]] if RARITY[ek[4]] != 'unique' else 'uncommon'
        mult = 1.0 if band == 'common' else 1.1
        tint = 'silver' if name.startswith('Silver') else ''
        weapon_row(iid, name, pt, tkey, lvl, band, ek, mult, tint)
        iid += 1
    iid = 5500
    for name, tkey, lvl, pt in UNIQUES:
        weapon_row(iid, name, pt, tkey, lvl, 'unique', W[name], 1.25)
        iid += 1
    # --- armor sets
    iid = 5200
    for group, bdir, lvl, tint, pt in SETS:
        for ek in sorted((r for r in A.values() if r[1] == group and r[2] in SLOT), key=lambda r: list(SLOT).index(r[2])):
            slot = SLOT[ek[2]]
            if bdir.startswith('cloth') and slot == 'head':
                continue
            armor = int(ek[5] or 0)
            c = armor + lvl / 4
            bonus = []
            if ek[6]: bonus.append(('hp', int(ek[6]) * 4))
            if ek[7]: bonus.append(('mp', int(ek[7]) * 4))
            for k, v in parse_res(ek[9]).items(): bonus.append((k, min(50, v)))
            for b in attrs(ek[8]):
                k, v = b.split(':'); bonus.append((k, int(v)))
            band = RARITY.get(ek[4], 'common')
            rows.append(dict(id=iid, name=ek[0], name_pt='%s %s' % ((SLOT_PT_SOFT.get(ek[2]) if bdir.startswith(('mage', 'leather')) else None) or SLOT_PT[ek[2]], pt),
                             base='items/base/armor/%s/%s.txt' % (bdir, slot), level=lvl, band=band,
                             dmg_type='', dmg_min='', dmg_max='', abs_min=max(0, int(round(c - 0.5))),
                             abs_max=max(1, int(round(c + 0.5))), two_handed=0, tint=tint,
                             bonuses=fmt_bonus(bonus), price=price(lvl, band), ek_icon=ek[-1], flavor='', slayer='',
                             classes=classes(ek[3])))
            iid += 1
    # --- shields & accessories
    iid = 5400
    for name, base, lvl, tint, pt, extra in OTHERS:
        ek = A[name]
        bonus = []
        if ek[6]: bonus.append(('hp', int(ek[6]) * 4))
        if ek[7]: bonus.append(('mp', int(ek[7]) * 4))
        for k, v in parse_res(ek[9]).items(): bonus.append((k, min(50, v)))
        for b in attrs(ek[8]):
            k, v = b.split(':'); bonus.append((k, int(v)))
        for e in extra.split(';'):
            if e:
                k, v = e.split(':'); bonus.append((k, int(v)))
        is_shield = '/shields/' in base
        armor = int(ek[5] or 0)
        amin = amax = ''
        if is_shield or armor:
            c = armor + lvl / 5
            amin, amax = max(0, int(round(c - 0.5))), max(1, int(round(c + 0.5)))
        band = RARITY.get(ek[4], 'common')
        rows.append(dict(id=iid, name=name, name_pt=pt, base=base, level=lvl, band=band, dmg_type='',
                         dmg_min='', dmg_max='', abs_min=amin, abs_max=amax, two_handed=0, tint=tint,
                         bonuses=fmt_bonus(bonus), price=price(lvl, band), ek_icon=ek[-1], flavor='', slayer='',
                         classes=classes(ek[3])))
        iid += 1

    cols = ['id', 'name', 'name_pt', 'base', 'level', 'band', 'dmg_type', 'dmg_min', 'dmg_max', 'abs_min',
            'abs_max', 'two_handed', 'tint', 'bonuses', 'price', 'ek_icon', 'flavor', 'slayer', 'classes']
    with open(out_items, 'w', newline='', encoding='utf8') as f:
        w = csv.DictWriter(f, cols); w.writeheader(); w.writerows(rows)
    print('items.csv: %d items' % len(rows))

    write_enemies(out_enemies)


# ---------------------------------------------------------------- enemies
# EK name, pt name, template (il_* file or custom), animation, tint, grade
ENEMIES = [
    # goblinoids
    ('Goblin (weak)', 'Goblin', 'il_goblin', 'fantasycore:goblin', '', 'normal'),
    ('Goblin Warrior', 'Goblin Guerreiro', 'il_goblin', 'fantasycore:goblin', 'rust', 'normal'),
    ('Goblin Spearman', 'Goblin Lanceiro', 'il_goblin_runner', 'fantasycore:goblin_runner', '', 'normal'),
    ('Goblin Guardian', 'Goblin Guardião', 'il_goblin', 'fantasycore:goblin_elite', '', 'normal'),
    ('Goblin Sergeant', 'Goblin Sargento', 'il_goblin', 'fantasycore:goblin_elite', 'gold', 'normal'),
    ('Goblin Beastlord', 'Goblin Senhor das Feras', 'il_goblin', 'fantasycore:goblin_elite', 'shadow', 'normal'),
    ('Goblin Firedancer', 'Goblin Dançarino do Fogo', 'il_goblin_runner', 'fantasycore:goblin_runner', 'fire', 'normal'),
    ('Goblin Firelord', 'Goblin Senhor do Fogo', 'il_goblin', 'fantasycore:goblin_elite', 'fire', 'elite'),
    ('Goblin King', 'Rei Goblin', 'il_goblin', 'fantasycore:goblin_elite', 'royal', 'boss'),
    ('Imp', 'Diabrete', 'il_goblin_runner', 'fantasycore:goblin_runner', 'blood', 'normal'),
    ('Orc Grunt', 'Orc Recruta', 'il_hobgoblin', 'empyrean_campaign:hobgoblin', 'orc', 'normal'),
    ('Orc Warrior', 'Orc Guerreiro', 'il_hobgoblin', 'empyrean_campaign:hobgoblin', '', 'normal'),
    ('Orc Archer', 'Orc Arqueiro', 'il_hobgoblin_archer', 'empyrean_campaign:hobgoblin_archer', 'orc', 'normal'),
    ('Orc Elite', 'Orc de Elite', 'il_hobgoblin', 'empyrean_campaign:hobgoblin', 'rust', 'normal'),
    ('Orc Shooter', 'Orc Atirador', 'il_hobgoblin_archer', 'empyrean_campaign:hobgoblin_archer', 'shadow', 'normal'),
    ('Orc Trooper', 'Orc Soldado', 'il_hobgoblin', 'empyrean_campaign:hobgoblin', 'steel', 'normal'),
    ('Orc Commander', 'Orc Comandante', 'il_hobgoblin', 'empyrean_campaign:hobgoblin', 'blood', 'elite'),
    ('Orc King', 'Rei Orc', 'il_hobgoblin', 'empyrean_campaign:hobgoblin', 'royal', 'boss'),
    ('Hobgoblin', 'Hobgoblin', 'il_hobgoblin', 'empyrean_campaign:hobgoblin', '', 'normal'),
    ('Ice Shaman', 'Xamã do Gelo', 'il_hobgoblin_archer', 'empyrean_campaign:hobgoblin_archer', 'ice', 'normal'),
    ('Dragon Priest', 'Sacerdote Dragão', 'il_hobgoblin_archer', 'empyrean_campaign:hobgoblin_archer', 'crimson', 'normal'),
    ('Ice Warlord', 'Senhor da Guerra do Gelo', 'il_hobgoblin', 'empyrean_campaign:hobgoblin', 'ice', 'elite'),
    ('Demon', 'Demônio', 'il_hobgoblin', 'empyrean_campaign:hobgoblin', 'blood', 'normal'),
    ('Ice Demon', 'Demônio do Gelo', 'il_hobgoblin', 'empyrean_campaign:hobgoblin', 'frost', 'normal'),
    # undead
    ('Skeleton', 'Esqueleto', 'il_skeleton_knight', 'fantasycore:skeleton_weak', '', 'normal'),
    ('Skeletal Warrior', 'Esqueleto Guerreiro', 'il_skeleton_knight', 'fantasycore:skeleton', '', 'normal'),
    ('Skeletal Archer', 'Esqueleto Arqueiro', 'il_skeleton_archer', 'fantasycore:skeleton_archer', '', 'normal'),
    ('Skeletal Evoker', 'Esqueleto Evocador', 'il_skeleton_mage_ice', 'fantasycore:skeleton_mage', 'arcane', 'normal'),
    ('Skeletal Champion', 'Esqueleto Campeão', 'il_skeleton_knight', 'fantasycore:skeleton', 'blessed', 'normal'),
    ('Skeletal Warlord', 'Esqueleto Senhor da Guerra', 'il_skeleton_knight', 'empyrean_campaign:skeleton_knight_boss', '', 'elite'),
    ('Fallen Dragoon', 'Dragão Caído', 'il_skeleton_knight', 'fantasycore:skeleton', 'dusk', 'normal'),
    ('Fallen Hero', 'Herói Caído', 'il_skeleton_knight', 'fantasycore:skeleton', 'gold', 'normal'),
    ('Giant Skeleton', 'Esqueleto Gigante', 'il_skeleton_knight', 'empyrean_campaign:skeleton_knight_boss', 'bone', 'boss'),
    ('Ghoul', 'Carniçal', 'il_zombie', 'fantasycore:zombie', 'ghoul', 'normal'),
    ('Mummy', 'Múmia', 'il_zombie', 'fantasycore:zombie', 'mummy', 'normal'),
    ('Beheaded One', 'Decapitado', 'il_zombie', 'empyrean_campaign:zombie_dark', '', 'normal'),
    ('Ancient Mummy', 'Múmia Anciã', 'il_zombie', 'fantasycore:zombie', 'ancient', 'normal'),
    ('Ghost (weak)', 'Fantasma', 'il_zombie', 'fantasycore:zombie', 'ghost', 'normal'),
    ('Librarian', 'Bibliotecário Espectral', 'il_zombie', 'fantasycore:zombie', 'spectral', 'normal'),
    ('Blood Wraith', 'Espectro de Sangue', 'il_zombie', 'empyrean_campaign:zombie_dark', 'wraith', 'normal'),
    ('Death Knight', 'Cavaleiro da Morte', 'il_skeleton_knight', 'fantasycore:skeleton', 'deathknight', 'elite'),
    # insects
    ('Mirmek Hatchling', 'Filhote de Mirmek', 'il_antlion_hatchling', 'fantasycore:antlion_small', '', 'normal'),
    ('Mirmek', 'Mirmek', 'il_antlion', 'fantasycore:antlion', '', 'normal'),
    ('Fire Mirmek', 'Mirmek de Fogo', 'il_antlion_fire', 'fantasycore:fire_ant', '', 'normal'),
    ('Mirmek Queen', 'Rainha Mirmek', 'il_antlion', 'empyrean_campaign:antlion_armored', 'royal', 'boss'),
    ('Inori Spider', 'Aranha Inori', 'il_antlion_hatchling', 'fantasycore:antlion_small', 'sand', 'normal'),
    ('Giant Spider', 'Aranha Gigante', 'il_antlion_hatchling', 'fantasycore:antlion_small', 'shadow', 'normal'),
    ('Bloodcrawler', 'Rastejante Sangrento', 'il_antlion_hatchling', 'fantasycore:antlion_small', 'blood', 'normal'),
    ('Monstruous Spider', 'Aranha Monstruosa', 'il_antlion', 'fantasycore:antlion', 'venom', 'normal'),
    # minotaurs
    ('Minotaur', 'Minotauro', 'minotaur', 'fantasycore:minotaur', '', 'normal'),
    ('Minotaur Raider', 'Minotauro Saqueador', 'minotaur', 'fantasycore:minotaur', 'rust', 'normal'),
    ('Minotaur Oracle', 'Minotauro Oráculo', 'minotaur_caster', 'fantasycore:minotaur', 'arcane', 'normal'),
    ('Minotaur Lord', 'Lorde Minotauro', 'minotaur', 'fantasycore:minotaur', 'gold', 'normal'),
    ('Minotaur Warlord', 'Minotauro Senhor da Guerra', 'minotaur', 'fantasycore:minotaur', 'blood', 'elite'),
    ('Minotaur Royal Guard', 'Guarda Real Minotauro', 'minotaur', 'fantasycore:minotaur', 'steel', 'normal'),
    ('Ashen Minotaur', 'Minotauro Cinzento', 'minotaur', 'fantasycore:minotaur', 'ashen', 'normal'),
    ('Ashen Oracle', 'Oráculo Cinzento', 'minotaur_caster', 'fantasycore:minotaur', 'ashen', 'normal'),
    ('Ashen Warlord', 'Senhor da Guerra Cinzento', 'minotaur', 'fantasycore:minotaur', 'ashen', 'boss'),
    ('Abyss Demon', 'Demônio do Abismo', 'minotaur', 'fantasycore:minotaur', 'infernal', 'elite'),
    # drakes
    ('Fire Drake', 'Draco de Fogo', 'il_wyvern_fire', 'fantasycore:wyvern_fire', '', 'normal'),
    ('Ice Drake', 'Draco de Gelo', 'il_wyvern_ice', 'fantasycore:wyvern_water', '', 'normal'),
    ('Green Dragon', 'Dragão Verde', 'il_wyvern', 'fantasycore:wyvern', 'venom', 'boss'),
    ('Red Dragon', 'Dragão Vermelho', 'il_wyvern_fire', 'fantasycore:wyvern_fire', 'blood', 'boss'),
]


def write_enemies(path):
    out = []
    for ek_name, pt, tmpl, anim, tint, grade in ENEMIES:
        r = B[ek_name]
        lvl = mid(r[2])
        hp = int(r[4] or 30)
        dps = float(r[7] or 5)
        s_hp = max(0.6, min(3.0, hp / (10 + 8 * lvl)))
        s_dmg = max(0.6, min(2.5, dps / (2.2 * lvl)))
        if grade == 'boss':
            s_hp = max(s_hp, 4.0); s_dmg = max(s_dmg, 1.5)
        elif grade == 'elite':
            s_hp = max(s_hp, 1.8); s_dmg = max(s_dmg, 1.2)
        res = parse_res(r[8])
        res = {k: max(-50, min(75, int(round(v / 3)))) for k, v in res.items() if v}
        name = ek_name.replace(' (weak)', '')
        out.append(dict(id='rd_' + re.sub(r'\W+', '_', name.lower()).strip('_'), name=name, name_pt=pt,
                        ek_level=int(round(lvl)), template=tmpl, animation=anim, tint=tint, grade=grade,
                        hp_mult='%.2f' % s_hp, dmg_mult='%.2f' % s_dmg,
                        resists=';'.join('%s:%d' % kv for kv in sorted(res.items())), family=r[1]))
    cols = ['id', 'name', 'name_pt', 'ek_level', 'family', 'template', 'animation', 'tint', 'grade',
            'hp_mult', 'dmg_mult', 'resists']
    with open(path, 'w', newline='', encoding='utf8') as f:
        w = csv.DictWriter(f, cols); w.writeheader(); w.writerows(out)
    print('enemies.csv: %d enemies' % len(out))


if __name__ == '__main__':
    main()
