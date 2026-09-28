#!/usr/bin/env python3
"""
Packs Random Dungeon's game data into the APK's assets.

  python3 flare-android-project/pack_data.py [--edition run|full]

Writes app/src/main/assets/ with mods/<mod>/... for the mods the game uses,
mods/mods.txt, and rd_manifest.txt (first line: a stamp that changes every
pack; then one asset path per line). On the phone, AndroidData.cpp copies
the listed files to the app's storage whenever the stamp differs.

Files are hard-linked, not copied (same disk), so this costs no extra space.
Left out: tooling, sources and fonts for languages the game doesn't ship,
and ek_icons (Exiled Kingdoms art, personal use only -- never distribute it).
"""
import os, sys, shutil, time, subprocess

HERE = os.path.dirname(os.path.abspath(__file__))
ENGINE = os.path.dirname(HERE)
MODS_SRC = os.path.join(ENGINE, 'mods')
ASSETS = os.path.join(HERE, 'app', 'src', 'main', 'assets')

# order matters: later mods override earlier ones ('default' is implicit)
MODS = ['default', 'fantasycore', 'empyrean_campaign', 'random_dungeon', 'darkfantasy_gui']

SKIP_DIRS = {'tools', '__pycache__', '.git'}
SKIP_EXT = {'.xcf', '.psd', '.kra', '.blend', '.py', '.md', '.pyc'}
SKIP_FILES = {'preview.png'}
SKIP_FONT_PREFIX = ('NotoSerif',)   # CJK/Thai/Hindi fonts (~37MB); pt/en use the pixel fonts


def keep(rel):
    parts = rel.split('/')
    if any(p in SKIP_DIRS for p in parts):
        return False
    name = parts[-1]
    if name in SKIP_FILES or os.path.splitext(name)[1].lower() in SKIP_EXT:
        return False
    if len(parts) >= 2 and parts[-2] == 'fonts' and name.startswith(SKIP_FONT_PREFIX):
        return False
    return True


def link(src, dst):
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    try:
        os.link(src, dst)
    except OSError:
        shutil.copy2(src, dst)


def app_icon():
    """Launcher icon in the UI theme's pixel style (sword on a red crest)."""
    sys.path.insert(0, os.path.join(MODS_SRC, 'darkfantasy_gui', 'tools'))
    import gen_theme as t
    from PIL import Image, ImageDraw
    A = 24
    im = t.new(A, A); d = ImageDraw.Draw(im)
    t.rect(d, 0, 0, A - 1, A - 1, t.K0)
    t.rect(d, 1, 1, A - 2, A - 2, t.G1)
    t.hline(d, 1, A - 2, 1, t.G3); t.vline(d, 1, 1, A - 2, t.G2)
    t.hline(d, 1, A - 2, A - 2, t.G0); t.vline(d, A - 2, 1, A - 2, t.G0)
    t.rect(d, 2, 2, A - 3, A - 3, t.K0)
    t.vgrad_bands(d, 3, 3, A - 4, A - 4, [t.R2, t.R1, t.R0, t.K1])
    t.draw_icon(im, 'sword', 4, 4)
    res = os.path.join(HERE, 'app', 'src', 'main', 'res')
    for folder, size in (('mdpi', 48), ('hdpi', 72), ('xhdpi', 96), ('xxhdpi', 144), ('xxxhdpi', 192)):
        out = os.path.join(res, 'drawable-' + folder)
        os.makedirs(out, exist_ok=True)
        im.resize((size, size), Image.NEAREST).save(os.path.join(out, 'ic_launcher.png'))


def main():
    app_icon()
    if os.path.isdir(ASSETS):
        shutil.rmtree(ASSETS)
    files = []
    total = 0
    for mod in MODS:
        root = os.path.realpath(os.path.join(MODS_SRC, mod))
        for dp, dn, fn in os.walk(root, followlinks=True):
            dn[:] = [d for d in dn if d not in SKIP_DIRS]
            for f in fn:
                src = os.path.join(dp, f)
                rel = 'mods/%s/%s' % (mod, os.path.relpath(src, root).replace(os.sep, '/'))
                if not keep(rel):
                    continue
                link(src, os.path.join(ASSETS, rel))
                files.append(rel)
                total += os.path.getsize(src)

    # --edition run|full: which game the APK is (engine/edition.txt). The
    # asset is a hard link to the source file: unlink before writing.
    if '--edition' in sys.argv:
        edition = sys.argv[sys.argv.index('--edition') + 1]
        ed = os.path.join(ASSETS, 'mods', 'random_dungeon', 'engine', 'edition.txt')
        if os.path.exists(ed):
            os.remove(ed)
        with open(ed, 'w') as f:
            f.write('edition=%s\n' % edition)

    mods_txt = os.path.join(ASSETS, 'mods', 'mods.txt')
    with open(mods_txt, 'w') as f:
        f.write('## Random Dungeon (Android)\n' + '\n'.join(m for m in MODS if m != 'default') + '\n')
    files.append('mods/mods.txt')

    try:
        rev = subprocess.check_output(['git', '-C', ENGINE, 'rev-parse', '--short', 'HEAD'], text=True).strip()
    except Exception:
        rev = 'nogit'
    stamp = 'rd-data-%s-%d' % (rev, int(time.time()))
    with open(os.path.join(ASSETS, 'rd_manifest.txt'), 'w') as f:
        f.write(stamp + '\n' + '\n'.join(files) + '\n')

    print('%d files, %.1f MB -> %s (%s)' % (len(files), total / 1e6, ASSETS, stamp))


if __name__ == '__main__':
    main()
