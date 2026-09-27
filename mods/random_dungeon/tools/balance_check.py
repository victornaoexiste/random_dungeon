#!/usr/bin/env python3
"""
Random Dungeon balance / consistency report for tools/ek_data/*.csv.

  python3 tools/balance_check.py          # report
  python3 tools/balance_check.py --quiet  # only the problem count per section

Read-only: it never edits anything. Fix what it reports in the CSVs and run
gen_content.py again.
"""
import csv, os, re, sys, collections, statistics

TOOLS = os.path.dirname(os.path.abspath(__file__))
DATA = os.path.join(TOOLS, 'ek_data')
MOD = os.path.dirname(TOOLS)
MODS = os.path.dirname(MOD)

items = list(csv.DictReader(open(os.path.join(DATA, 'items.csv'), encoding='utf8')))
enemies = list(csv.DictReader(open(os.path.join(DATA, 'enemies.csv'), encoding='utf8')))
problems = collections.defaultdict(list)
notes = collections.defaultdict(list)   # reported, not counted as problems


def num(v, default=0.0):
    try:
        return float(v)
    except (TypeError, ValueError):
        return default


def kind(it):
    return it['base'].split('/')[-1][:-4]


# (war hammers and staves hit like one-handed maces/wands in EK, so they stay
# one-handed; bows are blocked by their base file's disable_slots=main)
TWO_HANDED = {'greatsword', 'battle_axe', 'maul', 'greatstaff'}
ONE_HANDED = {'dagger', 'shortsword', 'longsword', 'mace', 'hand_axe', 'wand', 'rod', 'war_hammer', 'staff'}
RANGED = {'longbow', 'greatbow', 'shortbow'}
MENTAL = {'wand', 'staff', 'greatstaff', 'rod'}
ARMOR = {'chest', 'hands', 'legs', 'feet', 'head', 'belt', 'wood', 'iron', 'kite', 'steel'}
BAND_RANK = {'common': 0, 'uncommon': 1, 'rare': 2, 'unique': 3}
VALID_BONUS = {'hp', 'mp', 'crit', 'hp_regen', 'mp_regen', 'poise', 'physical', 'mental', 'offense', 'defense',
               'fire_resist', 'ice_resist', 'lightning_resist', 'dark_resist', 'xp_gain', 'item_find',
               'currency_find', 'avoidance', 'accuracy', 'absorb_min', 'absorb_max', 'speed', 'stealth',
               'dmg_fire_min', 'dmg_fire_max', 'dmg_ice_min', 'dmg_ice_max', 'dmg_lightning_min',
               'dmg_lightning_max', 'dmg_dark_min', 'dmg_dark_max', 'hp_steal', 'mp_steal',
               'dmg_melee_min', 'dmg_melee_max', 'dmg_ranged_min', 'dmg_ranged_max', 'dmg_ment_min', 'dmg_ment_max'}


def bonuses(s):
    out = []
    for part in (s or '').split(';'):
        if part:
            k, v = part.split(':')
            out.append((k, num(v)))
    return out


# ------------------------------------------------------------------ items
names = collections.Counter(i['name'] for i in items)
for n, c in names.items():
    if c > 1:
        problems['items: duplicate names'].append('%s x%d' % (n, c))

for it in items:
    k, lvl, band = kind(it), int(it['level']), it['band']
    tag = '%s %s (lvl %d, %s)' % (it['id'], it['name'], lvl, band)
    if not it['name_pt']:
        problems['items: missing Portuguese name'].append(tag)
    if it['dmg_type']:
        lo, hi = num(it['dmg_min']), num(it['dmg_max'])
        if lo > hi:
            problems['items: dmg_min > dmg_max'].append('%s %s-%s' % (tag, it['dmg_min'], it['dmg_max']))
        if lo <= 0:
            problems['items: zero/negative min damage'].append(tag)
        want = 'ranged' if k in RANGED else ('ment' if k in MENTAL else 'melee')
        if it['dmg_type'] != want:
            problems['items: damage type does not match the weapon'].append('%s is %s, expected %s' % (tag, it['dmg_type'], want))
    elif k in TWO_HANDED | ONE_HANDED:
        problems['items: weapon without damage'].append(tag)
    if it['abs_min'] != '':
        if num(it['abs_min']) > num(it['abs_max']):
            problems['items: abs_min > abs_max'].append(tag)
    th = it['two_handed'] == '1'
    if k in TWO_HANDED and not th:
        problems['items: two-handed weapon not flagged two_handed'].append(tag)
    if k in ONE_HANDED and th:
        problems['items: one-handed weapon flagged two_handed'].append(tag)
    if num(it['price']) <= 0:
        problems['items: no price'].append(tag)
    for bk, bv in bonuses(it['bonuses']):
        if bk not in VALID_BONUS:
            problems['items: unknown bonus stat'].append('%s %s' % (tag, bk))
        if bv < 0:
            notes['items: negative bonus (e.g. ice gear weak to fire)'].append('%s %s=%s' % (tag, bk, bv))
    for pair in ('fire', 'ice', 'lightning', 'dark'):
        b = dict(bonuses(it['bonuses']))
        if ('dmg_%s_min' % pair in b) != ('dmg_%s_max' % pair in b):
            problems['items: elemental damage with only min or max'].append(tag)
        elif 'dmg_%s_min' % pair in b and b['dmg_%s_min' % pair] > b['dmg_%s_max' % pair]:
            problems['items: elemental min > max'].append(tag)

# progression inside one weapon/armor kind: stronger with level, better with rarity
def avg_dmg(it):
    return (num(it['dmg_min']) + num(it['dmg_max'])) / 2


def avg_abs(it):
    return (num(it['abs_min']) + num(it['abs_max'])) / 2


by_kind = collections.defaultdict(list)
for it in items:
    by_kind[kind(it)].append(it)
for k, group in sorted(by_kind.items()):
    metric = avg_dmg if group[0]['dmg_type'] else (avg_abs if group[0]['abs_min'] != '' else None)
    if not metric:
        continue
    commons = sorted([g for g in group if g['band'] == 'common'], key=lambda g: int(g['level']))
    for a, b in zip(commons, commons[1:]):
        if int(b['level']) > int(a['level']) and metric(b) < metric(a):
            problems['items: higher-level common is weaker than a lower one (same kind)'].append(
                '%s: %s lvl%s %.1f  >  %s lvl%s %.1f' % (k, a['name'], a['level'], metric(a), b['name'], b['level'], metric(b)))
    for g in group:
        if g['band'] == 'common':
            continue
        peers = [c for c in commons if int(c['level']) <= int(g['level'])]
        if peers:
            best = max(peers, key=metric)
            if metric(g) < metric(best) * 0.95:
                problems['items: rarer item weaker than a common of the same kind and level or lower'].append(
                    '%s %s lvl%s (%s) %.1f  <  %s lvl%s %.1f' % (k, g['name'], g['level'], g['band'], metric(g), best['name'], best['level'], metric(best)))
    # price should grow with level/rarity
    for a, b in zip(commons, commons[1:]):
        if int(b['level']) > int(a['level']) and num(b['price']) < num(a['price']):
            problems['items: higher-level common is cheaper'].append('%s: %s %s  >  %s %s' % (k, a['name'], a['price'], b['name'], b['price']))

# damage per level across ALL one-handed / two-handed weapons: outliers
def weapon_class(it):
    k = kind(it)
    if k in RANGED: return 'ranged'
    if k in MENTAL: return 'mental'
    return 'melee-2h' if it['two_handed'] == '1' else 'melee-1h'


wc = collections.defaultdict(list)
for it in items:
    if it['dmg_type']:
        wc[weapon_class(it)].append(it)
for c, group in wc.items():
    # expected damage at a level: median of damage/level-curve ratio over the class
    ratios = [avg_dmg(g) / (4 + 3 * int(g['level'])) for g in group]
    med = statistics.median(ratios)
    for g, r in zip(group, ratios):
        rel = r / med
        lim_hi = 1.9 if g['band'] in ('unique', 'rare') else 1.45
        if rel > lim_hi or rel < 0.55:
            problems['items: damage far from its level curve (%s)' % c].append(
                '%s %s lvl%s %s  avg %.1f = %.0f%% of typical' % (g['id'], g['name'], g['level'], g['band'], avg_dmg(g), rel * 100))

# ------------------------------------------------------------------ enemies
ENEMY_GRADE = {e['id']: e['grade'] for e in enemies}
ids = collections.Counter(e['id'] for e in enemies)
for i, c in ids.items():
    if c > 1:
        problems['enemies: duplicate id'].append(i)
for e in enemies:
    tag = '%s (EK %s, %s)' % (e['id'], e['ek_level'], e['grade'])
    if not e['name_pt']:
        problems['enemies: missing Portuguese name'].append(tag)
    h, d = num(e['hp_mult']), num(e['dmg_mult'])
    if not (0.5 <= h <= 4.5) or not (0.5 <= d <= 2.0):
        problems['enemies: multiplier out of range'].append('%s hp %.2f dmg %.2f' % (tag, h, d))
    for part in (e['resists'] or '').split(';'):
        if part:
            k, v = part.split(':')
            if k not in ('fire_resist', 'ice_resist', 'lightning_resist', 'dark_resist'):
                problems['enemies: unknown resist'].append('%s %s' % (tag, k))
            if not -50 <= num(v) <= 90:
                problems['enemies: resist out of range'].append('%s %s=%s' % (tag, k, v))
    tpl = e['template']
    if not os.path.exists(os.path.join(MODS, 'empyrean_campaign/enemies/%s.txt' % tpl)) and tpl not in ('minotaur', 'minotaur_caster'):
        problems['enemies: template not found'].append('%s %s' % (tag, tpl))

GRADE_MIN = {'normal': (0.55, 0.55), 'elite': (1.6, 1.15), 'boss': (3.5, 1.4)}
for e in enemies:
    h, d = num(e['hp_mult']), num(e['dmg_mult'])
    mh, md = GRADE_MIN[e['grade']]
    if h < mh or d < md:
        problems['enemies: elite/boss not tougher than its grade minimum'].append('%s hp %.2f dmg %.2f' % (e['id'], h, d))


# (No "higher EK level but weaker" check: hp/dmg multipliers are relative to
# each enemy's own level, and the world spawns them at that level.)

# ------------------------------------------------------------------ world spawns
# enemies are only strong/weak relative to their OWN level: in the generated
# world they must spawn within a few levels of it (see gen_world.EK_BAND)
EK = {e['id']: int(e['ek_level']) for e in enemies}
world = os.path.join(MOD, 'maps', 'world')
for f in sorted(os.listdir(world)) if os.path.isdir(world) else []:
    if not f.endswith('.txt'):
        continue
    txt = open(os.path.join(world, f), encoding='utf8').read()
    for cat, lvl in re.findall(r'category=(rd_\w+)\nnumber=[\d,]+\nspawn_level=fixed,(\d+)', txt):
        if cat in EK and abs(EK[cat] - int(lvl)) > 3 and ENEMY_GRADE.get(cat) == 'normal':
            problems['world: enemy spawns far from its own level'].append('%s: %s EK%d at level %s' % (f, cat, EK[cat], lvl))

# ------------------------------------------------------------------ loot bands
bands = list(csv.DictReader(open(os.path.join(DATA, 'loot_bands.csv'), encoding='utf8')))
prev = None
for b in bands:
    for k in ('common', 'uncommon', 'rare', 'unique'):
        if prev and num(b[k]) < num(prev[k]):
            problems['loot: better grade drops less'].append('%s %s %s < %s %s' % (b['grade'], k, b[k], prev['grade'], prev[k]))
    if not num(b['common']) >= num(b['uncommon']) >= num(b['rare']) >= num(b['unique']):
        problems['loot: rarer band more likely than a common one'].append(b['grade'])
    prev = b

# ------------------------------------------------------------------ report
quiet = '--quiet' in sys.argv
total = 0
for sec in sorted(problems):
    lst = problems[sec]
    total += len(lst)
    print('\n== %s: %d' % (sec, len(lst)))
    if not quiet:
        for l in lst[:40]:
            print('   ' + l)
        if len(lst) > 40:
            print('   ... +%d' % (len(lst) - 40))
for sec in sorted(notes):
    print('\n-- note: %s: %d' % (sec, len(notes[sec])))
print('\n%d findings (%d items, %d enemies)' % (total, len(items), len(enemies)))
