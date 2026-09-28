#!/usr/bin/env python3
"""
Packs the Windows build of Encantados into a folder + zip for itch.io.

  mingw64-cmake -S . -B build-win -DCMAKE_BUILD_TYPE=Release \\
      -DENET_INCLUDE_DIR=$HOME/win-deps/include -DENET_LIBRARY=$HOME/win-deps/lib/libenet.a
  make -C build-win -j8
  python3 distribution/windows/package_windows.py [--edition run|full] [--no-zip]

Output: dist/Encantados-<edition>-win64/ and the .zip next to it.

The folder has Encantados.exe, the MinGW/SDL DLLs it needs (found by
walking the import tables), the game mods, mods/mods.txt, the licenses and
CREDITS.txt, and itch's .itch.toml launch manifest. Saves go to
%APPDATA%\\RandomDungeon (see PlatformWin32.cpp).

Left out, like flare-android-project/pack_data.py: tooling and sources,
the CJK/Thai/Hindi fonts, and ek_icons (Exiled Kingdoms art, personal use
only -- never distribute it).
"""
import os, sys, shutil, subprocess, zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
ENGINE = os.path.dirname(os.path.dirname(HERE))
GAME = os.path.join(os.path.dirname(ENGINE), 'flare-game')
BUILD = os.path.join(ENGINE, 'build-win')
SYSROOT_BIN = '/usr/x86_64-w64-mingw32/sys-root/mingw/bin'
OBJDUMP = 'x86_64-w64-mingw32-objdump'
DLOPEN_DLLS = ['SDL3.dll']

MODS = ['default', 'fantasycore', 'empyrean_campaign', 'random_dungeon', 'darkfantasy_sprites', 'darkfantasy_gui']
SKIP_DIRS = {'tools', '__pycache__', '.git'}
SKIP_EXT = {'.xcf', '.psd', '.kra', '.blend', '.py', '.md', '.pyc'}
SKIP_FILES = {'preview.png'}
SKIP_FONT_PREFIX = ('NotoSerif',)
# assets replaced wholesale by a later mod are never loaded, so they're left
# out (darkfantasy_sprites re-grades every hero/enemy sprite). Text files
# aren't deduplicated: they can APPEND to the earlier mod's copy.
BINARY_EXT = {'.png', '.jpg', '.ogg', '.wav', '.ttf', '.otf'}


def keep(parts):
    name = parts[-1]
    if any(p in SKIP_DIRS for p in parts[:-1]):
        return False
    if name in SKIP_FILES or os.path.splitext(name)[1].lower() in SKIP_EXT:
        return False
    if len(parts) >= 2 and parts[-2] == 'fonts' and name.startswith(SKIP_FONT_PREFIX):
        return False
    return True


def dll_imports(path):
    out = subprocess.check_output([OBJDUMP, '-p', path], text=True)
    return [l.split(':', 1)[1].strip() for l in out.splitlines() if 'DLL Name:' in l]


def copy_dlls(exe, dest):
    """Every DLL from the MinGW sysroot the exe needs, recursively
    (system DLLs like KERNEL32 aren't in the sysroot and are skipped)."""
    have = {f.lower(): f for f in os.listdir(SYSROOT_BIN)}
    todo, seen = [exe], set()
    # Fedora's SDL2.dll is sdl2-compat, which loads SDL3.dll at run time
    # (not in any import table)
    for extra in DLOPEN_DLLS:
        seen.add(extra.lower())
        src = os.path.join(SYSROOT_BIN, have[extra.lower()])
        shutil.copy2(src, os.path.join(dest, have[extra.lower()]))
        todo.append(src)
    while todo:
        for dll in dll_imports(todo.pop()):
            key = dll.lower()
            if key in seen or key not in have:
                continue
            seen.add(key)
            src = os.path.join(SYSROOT_BIN, have[key])
            shutil.copy2(src, os.path.join(dest, have[key]))
            todo.append(src)
    return sorted(seen)


def pack_data(out, edition):
    """Copies the game mods into out/mods (also used by distribution/linux)."""
    def rel_files(mod):
        root = os.path.realpath(os.path.join(ENGINE, 'mods', mod))
        for dp, dn, fn in os.walk(root, followlinks=True):
            dn[:] = [d for d in dn if d not in SKIP_DIRS]
            for f in fn:
                yield os.path.relpath(os.path.join(dp, f), root)
    overridden = {}
    later = set()
    for mod in reversed(MODS):
        overridden[mod] = set(r for r in rel_files(mod) if r in later and os.path.splitext(r)[1].lower() in BINARY_EXT)
        later.update(rel_files(mod))

    files = total = skipped = 0
    for mod in MODS:
        root = os.path.realpath(os.path.join(ENGINE, 'mods', mod))
        for dp, dn, fn in os.walk(root, followlinks=True):
            dn[:] = [d for d in dn if d not in SKIP_DIRS]
            for f in fn:
                src = os.path.join(dp, f)
                rel = os.path.relpath(src, root)
                if not keep(rel.split(os.sep)):
                    continue
                if rel in overridden[mod]:
                    skipped += os.path.getsize(src)
                    continue
                dst = os.path.join(out, 'mods', mod, rel)
                os.makedirs(os.path.dirname(dst), exist_ok=True)
                shutil.copy2(src, dst)
                files += 1
                total += os.path.getsize(src)

    missing = verify_images(out)
    if missing:
        sys.exit('ERROR: %d images used by animations are missing from the package, e.g.:\n  %s\n'
                 '(darkfantasy_sprites: python3 mods/darkfantasy_sprites/tools/grade.py; biome tilesets: '
                 'python3 mods/random_dungeon/tools/gen_world.py --tilesets)' % (len(missing), '\n  '.join(missing[:10])))

    with open(os.path.join(out, 'mods', 'mods.txt'), 'w') as f:
        f.write('## Encantados\n' + '\n'.join(m for m in MODS if m != 'default') + '\n')
    with open(os.path.join(out, 'mods', 'random_dungeon', 'engine', 'edition.txt'), 'w') as f:
        f.write('# run = commercial edition (Infinite Run only); full = everything\nedition=%s\n' % edition)

    return files, total, skipped


def verify_images(out):
    """Every image= an animation or tileset asks for must exist in some packed
    mod -- otherwise heroes/enemies silently vanish in the game (this is how a
    package once shipped without darkfantasy_sprites' images)."""
    mods_dir = os.path.join(out, 'mods')
    def exists(rel):
        return any(os.path.exists(os.path.join(mods_dir, m, rel)) for m in MODS)
    missing = set()
    for mod in MODS:
        for sub in ('animations', 'tilesetdefs'):
            root = os.path.join(mods_dir, mod, sub)
            for dp, dn, fn in os.walk(root):
                for f in fn:
                    if not f.endswith('.txt'):
                        continue
                    for line in open(os.path.join(dp, f), encoding='utf8', errors='replace'):
                        line = line.strip()
                        if line.startswith('img=') or line.startswith('image='):
                            rel = line.split('=', 1)[1].split(',')[0].strip()
                            if rel.endswith('.png') and not exists(rel):
                                missing.add('%s (%s)' % (rel, os.path.relpath(os.path.join(dp, f), mods_dir)))
    return sorted(missing)


def copy_trailer_tools(out, windows):
    """Trailer mode helpers: instructions, make_gif.py and a launcher."""
    dist = os.path.dirname(HERE)
    shutil.copy2(os.path.join(dist, 'TRAILER.txt'), os.path.join(out, 'TRAILER.txt'))
    shutil.copy2(os.path.join(dist, 'make_gif.py'), os.path.join(out, 'make_gif.py'))
    if windows:
        with open(os.path.join(out, 'Encantados-trailer.bat'), 'w', newline='\r\n') as f:
            f.write('@echo off\nstart "" "%~dp0Encantados.exe" --trailer\n')


def copy_licenses(out):
    """Licenses and credits: engine GPL-3, Flare content CC-BY-SA 3.0, fonts OFL."""
    # licenses: engine GPL-3, Flare content CC-BY-SA 3.0, fonts OFL, credits
    shutil.copy2(os.path.join(ENGINE, 'COPYING'), os.path.join(out, 'LICENSE-engine-GPL3.txt'))
    shutil.copy2(os.path.join(GAME, 'LICENSE.txt'), os.path.join(out, 'LICENSE-content-CC-BY-SA-3.0.txt'))
    shutil.copy2(os.path.join(HERE, 'CREDITS.txt'), os.path.join(out, 'CREDITS.txt'))
    shutil.copy2(os.path.join(GAME, 'CREDITS.txt'), os.path.join(out, 'CREDITS-flare-game.txt'))
    shutil.copy2(os.path.join(ENGINE, 'CREDITS.engine.txt'), os.path.join(out, 'CREDITS-flare-engine.txt'))


def main():
    edition = 'run'
    if '--edition' in sys.argv:
        edition = sys.argv[sys.argv.index('--edition') + 1]
    exe = os.path.join(BUILD, 'flare.exe')
    if not os.path.exists(exe):
        sys.exit('build-win/flare.exe not found; build it first (see the top of this file)')

    name = 'Encantados-%s-win64' % edition
    out = os.path.join(ENGINE, 'dist', name)
    if os.path.isdir(out):
        shutil.rmtree(out)
    os.makedirs(out)

    shutil.copy2(exe, os.path.join(out, 'Encantados.exe'))
    subprocess.call(['x86_64-w64-mingw32-strip', os.path.join(out, 'Encantados.exe')])
    dlls = copy_dlls(exe, out)

    files, total, skipped = pack_data(out, edition)
    copy_licenses(out)
    copy_trailer_tools(out, True)
    shutil.copy2(os.path.join(HERE, 'Liberar-firewall.bat'), os.path.join(out, 'Liberar-firewall.bat'))
    with open(os.path.join(out, '.itch.toml'), 'w') as f:
        f.write('[[actions]]\nname = "play"\npath = "Encantados.exe"\nplatform = "windows"\n')

    print('%s: %d data files (%.0f MB, %.0f MB of overridden assets left out), DLLs: %s' % (name, files, total / 1e6, skipped / 1e6, ', '.join(dlls)))

    if '--no-zip' not in sys.argv:
        z = out + '.zip'
        with zipfile.ZipFile(z, 'w', zipfile.ZIP_DEFLATED, compresslevel=6) as zf:
            for dp, dn, fn in os.walk(out):
                for f in fn:
                    p = os.path.join(dp, f)
                    zf.write(p, os.path.join(name, os.path.relpath(p, out)))
        print('%s (%.0f MB)' % (z, os.path.getsize(z) / 1e6))


if __name__ == '__main__':
    main()
