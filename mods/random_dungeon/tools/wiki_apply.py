#!/usr/bin/env python3
"""
Applies edits made on the Random Dungeon wiki (Códice) back to the spreadsheets.

The wiki's shared database is read by Claude (ArtifactData "query", saved
with out_dir), which gives one JSON file per document:
  <dir>/items/<id>.json, <dir>/enemies/<id>.json, <dir>/loot/<grade>.json
Each file is {"id": ..., "data": {...}, "version": N} or the bare document.

  python3 tools/wiki_apply.py <dir>            show what would change
  python3 tools/wiki_apply.py <dir> --write    update the CSVs

Only documents with pending=true are applied, and only the editable fields.
"notes" are free-text requests for Claude and are printed, never applied.
After --write, run gen_content.py (and gen_world.py if enemies changed) and
mark the documents as applied on the wiki (pending=false).
"""
import csv, json, os, sys

TOOLS = os.path.dirname(os.path.abspath(__file__))
DATA = os.path.join(TOOLS, 'ek_data')

TABLES = {
    'items': ('items.csv', 'id', ['name', 'name_pt', 'level', 'band', 'dmg_min', 'dmg_max', 'abs_min', 'abs_max',
                                  'price', 'tint', 'bonuses', 'slayer', 'classes']),
    'enemies': ('enemies.csv', 'id', ['name', 'name_pt', 'ek_level', 'grade', 'hp_mult', 'dmg_mult', 'tint', 'resists']),
    'loot': ('loot_bands.csv', 'grade', ['gold', 'potion', 'common', 'uncommon', 'rare', 'unique', 'gold_qty',
                                         'gold_qty_per_level', 'level_below', 'level_above', 'drops_min', 'drops_max']),
}


def load_docs(folder):
    docs = {}
    if not os.path.isdir(folder):
        return docs
    for f in os.listdir(folder):
        if f.endswith('.json'):
            d = json.load(open(os.path.join(folder, f), encoding='utf8'))
            body = d.get('data', d) if isinstance(d, dict) else d
            docs[f[:-5]] = body
    return docs


def cell(v):
    if v is None:
        return ''
    if isinstance(v, float) and v.is_integer():
        return str(int(v))
    return str(v)


def main():
    src = sys.argv[1]
    write = '--write' in sys.argv
    total = 0
    for coll, (fname, key, fields) in TABLES.items():
        docs = {k: v for k, v in load_docs(os.path.join(src, coll)).items() if v.get('pending')}
        if not docs:
            continue
        path = os.path.join(DATA, fname)
        rows = list(csv.DictReader(open(path, encoding='utf8')))
        cols = list(rows[0].keys())
        for r in rows:
            d = docs.get(r[key])
            if not d:
                continue
            for f in fields:
                if f in d and f in r and cell(d[f]) != r[f]:
                    print('%s %s: %s  %r -> %r' % (coll, r[key], f, r[f], cell(d[f])))
                    r[f] = cell(d[f])
                    total += 1
            if d.get('notes'):
                print('%s %s: NOTE for Claude: %s' % (coll, r[key], d['notes']))
        if write:
            with open(path, 'w', newline='', encoding='utf8') as fh:
                w = csv.DictWriter(fh, cols)
                w.writeheader()
                w.writerows(rows)
    print('%d field changes %s' % (total, 'written' if write else '(dry run, pass --write)'))


if __name__ == '__main__':
    main()
