#!/usr/bin/env python3
"""
Packs the Linux build of Random Dungeon into a folder + .tar.gz.

  cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release && make -C build-release -j8
  python3 distribution/linux/package_linux.py [--edition run|full] [--no-tar]

Output: dist/RandomDungeon-<edition>-linux64/ and the .tar.gz next to it.

Bundles the SDL/ENet libraries next to the game (lib/, found through
jogar.sh's LD_LIBRARY_PATH), so other distros don't need them installed;
glibc, the graphics drivers and the audio/display servers come from the
system, as they must. Fedora's SDL2 is sdl2-compat, which loads SDL3 at run
time, so that one is bundled too. Controllers work out of the box (SDL
GameController; enable_joystick defaults to on).

Same data rules as distribution/windows/package_windows.py (no tooling, no
ek_icons, overridden assets left out).
"""
import os, sys, shutil, subprocess, tarfile

HERE = os.path.dirname(os.path.abspath(__file__))
ENGINE = os.path.dirname(os.path.dirname(HERE))
BUILD = os.path.join(ENGINE, 'build-release')
sys.path.insert(0, os.path.join(ENGINE, 'distribution', 'windows'))
import package_windows as pw   # shared data packing

# libraries that must come from the system
SYSTEM = ('linux-vdso', 'ld-linux', 'libc.so', 'libm.so', 'libdl.so', 'libpthread.so', 'librt.so',
          'libgcc_s', 'libGL', 'libEGL', 'libGLX', 'libGLdispatch', 'libX', 'libxcb', 'libwayland',
          'libasound', 'libpulse', 'libpipewire', 'libdbus', 'libsystemd', 'libudev', 'libdrm',
          'libgbm', 'libvulkan', 'libdecor', 'libxkbcommon', 'libcap', 'libz.so', 'libstdc++')
DLOPEN = ('libSDL3.so.0',)


def ldd(path):
    out = subprocess.check_output(['ldd', path], text=True)
    libs = {}
    for line in out.splitlines():
        if '=>' in line:
            name, rest = line.strip().split(' => ', 1)
            p = rest.split(' (')[0].strip()
            if p.startswith('/'):
                libs[name] = p
    return libs


def bundle_libs(binary, dest):
    os.makedirs(dest, exist_ok=True)
    todo = [binary]
    for extra in DLOPEN:
        for d in ('/usr/lib64', '/usr/lib', '/usr/lib/x86_64-linux-gnu'):
            if os.path.exists(os.path.join(d, extra)):
                todo.append(os.path.join(d, extra))
                break
    seen = set()
    while todo:
        cur = todo.pop()
        if cur != binary and os.path.basename(cur) not in seen:
            seen.add(os.path.basename(cur))
            shutil.copy2(os.path.realpath(cur), os.path.join(dest, os.path.basename(cur)))
        for name, p in ldd(cur).items():
            if name in seen or any(name.startswith(s) for s in SYSTEM):
                continue
            todo.append(p)
    return sorted(seen)


RUN_SH = '''#!/bin/sh
# Random Dungeon -- run from anywhere
DIR="$(cd "$(dirname "$0")" && pwd)"
export LD_LIBRARY_PATH="$DIR/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
exec "$DIR/RandomDungeon" --data-path="$DIR/" "$@"
'''


def main():
    edition = 'run'
    if '--edition' in sys.argv:
        edition = sys.argv[sys.argv.index('--edition') + 1]
    exe = os.path.join(BUILD, 'flare')
    if not os.path.exists(exe):
        sys.exit('build-release/flare not found; build it first (see the top of this file)')

    name = 'RandomDungeon-%s-linux64' % edition
    out = os.path.join(ENGINE, 'dist', name)
    if os.path.isdir(out):
        shutil.rmtree(out)
    os.makedirs(out)

    shutil.copy2(exe, os.path.join(out, 'RandomDungeon'))
    subprocess.call(['strip', os.path.join(out, 'RandomDungeon')])
    libs = bundle_libs(exe, os.path.join(out, 'lib'))
    with open(os.path.join(out, 'jogar.sh'), 'w') as f:
        f.write(RUN_SH)
    os.chmod(os.path.join(out, 'jogar.sh'), 0o755)

    files, total, skipped = pw.pack_data(out, edition)
    pw.copy_licenses(out)
    pw.copy_trailer_tools(out, False)
    print('%s: %d data files (%.0f MB, %.0f MB of overridden assets left out), libs: %s' % (
        name, files, total / 1e6, skipped / 1e6, ', '.join(libs)))

    if '--no-tar' not in sys.argv:
        t = out + '.tar.gz'
        with tarfile.open(t, 'w:gz', compresslevel=6) as tf:
            tf.add(out, arcname=name)
        print('%s (%.0f MB)' % (t, os.path.getsize(t) / 1e6))


if __name__ == '__main__':
    main()
