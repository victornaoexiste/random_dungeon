#!/usr/bin/env python3
"""
One-off: builds skills.csv (our skill list) from ek_skill_table.csv (the
Exiled Kingdoms wiki "Skill Table"). Run once; afterwards skills.csv is the
source of truth and is edited by hand (names, costs, requirements).

Every skill gets an original name (EN + PT). The mechanics, ranks and
point costs follow EK; guild/faction requirements (which don't exist here)
become a level requirement, and EK traits map to our primary stats:
  Strength -> physical, Endurance -> defense, Agility/Awareness -> offense,
  Intellect/Personality -> mental;   trait N -> primary 4*N+1
"""
import csv, os, re

HERE = os.path.dirname(os.path.abspath(__file__))

# ek name: (id, name, name_pt)
NAMES = {
    # Cleric
    "Arbenos' Might": ('zealots_might', "Zealot's Might", 'Força do Zelote'),
    'Crusader': ('templar', 'Templar', 'Templário'),
    'Heal Wounds': ('mend_flesh', 'Mend Flesh', 'Sarar a Carne'),
    'Holy Shield': ('hallowed_aegis', 'Hallowed Aegis', 'Égide Sagrada'),
    'Intervention': ('last_rite', 'Last Rite', 'Último Rito'),
    "Nivaria's Barrier": ('warding_faith', 'Warding Faith', 'Fé Protetora'),
    'Sacred Fire': ('cleansing_flame', 'Cleansing Flame', 'Chama Purificadora'),
    "Thelume's Wisdom": ('seers_sight', "Seer's Sight", 'Visão do Vidente'),
    # Mage
    'Fireball': ('ember_orb', 'Ember Orb', 'Orbe de Brasa'),
    'Ice Storm': ('frost_squall', 'Frost Squall', 'Rajada Gélida'),
    'Lesser Summoning': ('call_wisp', 'Call Wisp', 'Invocar Fagulha'),
    'Lightning Bolt': ('storm_lance', 'Storm Lance', 'Lança da Tempestade'),
    'Mage Armor': ('arcane_mantle', 'Arcane Mantle', 'Manto Arcano'),
    'Mana Surge': ('deep_well', 'Deep Well', 'Poço Profundo'),
    'Staff Mastery': ('staff_adept', 'Staff Adept', 'Mestre do Cajado'),
    'Wand Mastery': ('wand_adept', 'Wand Adept', 'Mestre da Varinha'),
    # Rogue
    'Archery': ('keen_arrows', 'Keen Arrows', 'Flechas Afiadas'),
    'Evasion': ('slip_away', 'Slip Away', 'Esquiva Sombria'),
    'Kick': ('heel_strike', 'Heel Strike', 'Chute Brutal'),
    'Sneak Attack': ('backstab', 'Backstab', 'Golpe Traiçoeiro'),
    'Sprint': ('dash', 'Dash', 'Disparada'),
    'Stab': ('gut_strike', 'Gut Strike', 'Estocada'),
    'Stealth': ('fade', 'Fade', 'Sumir nas Sombras'),
    'Trap Master': ('snare_craft', 'Snare Craft', 'Armadilheiro'),
    # Warrior
    'Bash': ('shield_slam', 'Shield Slam', 'Pancada de Escudo'),
    'Charge': ('bull_rush', 'Bull Rush', 'Investida Taurina'),
    'Cleave': ('wide_swing', 'Wide Swing', 'Golpe Amplo'),
    'Fury': ('berserk_blood', 'Berserk Blood', 'Sangue Berserker'),
    'Resilience': ('iron_will', 'Iron Will', 'Vontade de Ferro'),
    'Shield Expert': ('bulwark', 'Bulwark', 'Baluarte'),
    'Two Handed Expert': ('heavy_arms', 'Heavy Arms', 'Armas Pesadas'),
    'Whirlwind': ('blade_storm', 'Blade Storm', 'Tempestade de Lâminas'),
    # General
    'Beast Master': ('beast_friend', 'Beast Friend', 'Amigo das Feras'),
    'Dungeoneering': ('delver', 'Delver', 'Explorador'),
    'Extra Recovery': ('second_wind', 'Second Wind', 'Segundo Fôlego'),
    'Gossip': ('silver_tongue', 'Silver Tongue', 'Língua de Prata'),
    # Advanced
    'Arcane Blade': ('runed_edge', 'Runed Edge', 'Lâmina Rúnica'),
    'Arcanist': ('spell_thrift', 'Spell Thrift', 'Economia Arcana'),
    'Assassinate': ('killing_flow', 'Killing Flow', 'Fluxo Assassino'),
    'Battle Prayer': ('war_hymn', 'War Hymn', 'Hino de Guerra'),
    'Battle Rage': ('blood_frenzy', 'Blood Frenzy', 'Frenesi Sangrento'),
    'Bloodlust': ('thrill_of_the_kill', 'Thrill of the Kill', 'Êxtase da Matança'),
    'Body Development': ('hardened_body', 'Hardened Body', 'Corpo Calejado'),
    'Combustion': ('immolate', 'Immolate', 'Imolação'),
    'Death Cloud': ('plague_mist', 'Plague Mist', 'Névoa da Peste'),
    'Death Ward': ('grave_ward', 'Grave Ward', 'Proteção Tumular'),
    'Disintegrate': ('unmake', 'Unmake', 'Desfazer'),
    'Duel': ('duelist', 'Duelist', 'Duelista'),
    'Earth Mastery': ('stone_caller', 'Stone Caller', 'Chamado da Pedra'),
    'Explosive Traps': ('blast_snares', 'Blast Snares', 'Armadilhas Explosivas'),
    'Fire Mastery': ('flame_caller', 'Flame Caller', 'Chamado da Chama'),
    'Fire Ward': ('ember_ward', 'Ember Ward', 'Proteção Ígnea'),
    'Flames of Faith': ('pyre_of_faith', 'Pyre of Faith', 'Pira da Fé'),
    'Flurry': ('blade_dance', 'Blade Dance', 'Dança das Lâminas'),
    'Gate': ('pilgrims_step', "Pilgrim's Step", 'Passo do Peregrino'),
    'Guardian Wolf': ('spirit_hound', 'Spirit Hound', 'Cão Espiritual'),
    'Heavyhand': ('crushing_blows', 'Crushing Blows', 'Golpes Esmagadores'),
    'Ice Mastery': ('frost_caller', 'Frost Caller', 'Chamado do Gelo'),
    'Ice Ward': ('frost_ward', 'Frost Ward', 'Proteção Gélida'),
    'Infantry Training': ('veteran_stance', 'Veteran Stance', 'Postura Veterana'),
    'Magical Training': ('blood_casting', 'Blood Casting', 'Magia de Sangue'),
    'Mage Barrier': ('arc_repulse', 'Arc Repulse', 'Repulsão Arcana'),
    'Massive Criticals': ('brutal_criticals', 'Brutal Criticals', 'Críticos Brutais'),
    'Poison Master': ('venomcraft', 'Venomcraft', 'Arte do Veneno'),
    'Precision Shots': ('deadeye', 'Deadeye', 'Olho Mortal'),
    'Precision Strikes': ('keen_strikes', 'Keen Strikes', 'Golpes Precisos'),
    'Rapid Fire': ('quickdraw', 'Quickdraw', 'Saque Rápido'),
    'Retribution': ('soul_harvest', 'Soul Harvest', 'Colheita de Almas'),
    'Shock Ward': ('storm_ward', 'Storm Ward', 'Proteção Elétrica'),
    'Smoke Bomb': ('smoke_veil', 'Smoke Veil', 'Véu de Fumaça'),
    'Spiritual Ward': ('soul_ward', 'Soul Ward', 'Proteção da Alma'),
    'Summoner': ('binder', 'Binder', 'Invocador Mestre'),
    'Toxic Ward': ('venom_ward', 'Venom Ward', 'Proteção Tóxica'),
    'Turn Undead': ('banish_dead', 'Banish Dead', 'Banir Mortos'),
    'Vampiric Blade': ('leeching_edge', 'Leeching Edge', 'Lâmina Sanguessuga'),
}

TRAIT = {'strength': 'physical', 'endurance': 'defense', 'agility': 'offense', 'awareness': 'offense',
         'intellect': 'mental', 'personality': 'mental'}
CLASS_COLS = [('W', 'warrior'), ('R', 'rogue'), ('C', 'cleric'), ('M', 'mage')]


def reqs(text, ids):
    out, level = [], 0
    for part in re.split(r'[;,]', text or ''):
        p = part.strip().rstrip('.')
        if not p or p == 'None':
            continue
        m = re.match(r'([A-Za-z]+) (\d+)$', p)
        if m and m.group(1).lower() in TRAIT:
            out.append('%s:%d' % (TRAIT[m.group(1).lower()], 4 * int(m.group(2)) + 1))
            continue
        m = re.match(r"(.+?) (\d+)$", p)
        if m and m.group(1) in ids and not ('Guild' in p or 'reputation' in p):
            out.append('skill:%s:%s' % (ids[m.group(1)], m.group(2)))
            continue
        # guild / faction membership or reputation: no factions here
        level = max(level, 8)
    return ';'.join(out), level


def main():
    rows = list(csv.reader(open(os.path.join(HERE, 'ek_skill_table.csv'), encoding='utf8')))
    ids = {r[1]: NAMES[r[1]][0] for r in rows[1:]}
    out = []
    for r in rows[1:]:
        ek = r[1]
        sid, name, name_pt = NAMES[ek]
        flags = dict(zip('WRCMGA', [bool(x) for x in r[2:8]]))
        classes = [c for k, c in CLASS_COLS if flags[k]]
        tab = 'advanced' if flags['A'] else ('general' if flags['G'] else 'class')
        if tab == 'general':
            classes = ['all']
        req, level = reqs(r[12], ids)
        if tab == 'advanced':
            level = max(level, 5)
        costs = r[9]
        out.append({
            'id': sid, 'name': name, 'name_pt': name_pt, 'tab': tab,
            'classes': ','.join(classes), 'ranks': len(costs.split('/')), 'cost': costs,
            'cooldown': r[10], 'mana': r[11], 'requires': req, 'requires_level': level or '',
            'ek_name': ek, 'ek_effect': r[8],
        })
    cols = ['id', 'name', 'name_pt', 'tab', 'classes', 'ranks', 'cost', 'cooldown', 'mana',
            'requires', 'requires_level', 'ek_name', 'ek_effect']
    with open(os.path.join(HERE, 'skills.csv'), 'w', newline='', encoding='utf8') as f:
        w = csv.DictWriter(f, cols)
        w.writeheader()
        w.writerows(out)
    print('%d skills' % len(out))


if __name__ == '__main__':
    main()
