#!/usr/bin/env python3
"""
Random Dungeon trailer mode: turns recorded frames into a GIF.

  python3 make_gif.py <frames folder> [--width 640] [--fps 15] [--start 0] [--end N] [--out file.gif]

The game records frames with --trailer and F3 (see GameStatePlay::
trailerLogic) into <user folder>/trailer/gif_NNN/frame_NNNN.bmp:
  Linux:   ~/.local/share/flare/trailer/
  Windows: %APPDATA%\\RandomDungeon\\userdata\\trailer\\

Needs Python 3 + Pillow (pip install pillow). With ffmpeg installed it also
writes an .mp4 next to the GIF (better for TikTok/Reels/Shorts).
Without Python: upload the frames to ezgif.com/maker instead.
"""
import os, sys, glob, shutil, subprocess


def arg(name, default):
    return sys.argv[sys.argv.index(name) + 1] if name in sys.argv else default


def main():
    if len(sys.argv) < 2 or sys.argv[1].startswith('--'):
        sys.exit(__doc__)
    from PIL import Image
    folder = sys.argv[1]
    frames = sorted(glob.glob(os.path.join(folder, 'frame_*.bmp')) + glob.glob(os.path.join(folder, 'frame_*.png')))
    start, end = int(arg('--start', 0)), int(arg('--end', len(frames)))
    frames = frames[start:end]
    if not frames:
        sys.exit('no frames in %s' % folder)
    width = int(arg('--width', 640))
    fps = float(arg('--fps', 15))
    out = arg('--out', folder.rstrip('/\\') + '.gif')

    images = []
    for f in frames:
        im = Image.open(f).convert('RGB')
        if im.width > width:
            im = im.resize((width, round(im.height * width / im.width)), Image.NEAREST)
        images.append(im)

    # one shared palette (from a few frames) keeps colours stable, no flicker
    sample = Image.new('RGB', (images[0].width, images[0].height * min(4, len(images))))
    for i in range(min(4, len(images))):
        sample.paste(images[i * (len(images) - 1) // max(1, min(3, len(images) - 1))], (0, images[0].height * i))
    pal = sample.quantize(colors=255, method=Image.MEDIANCUT)
    gif = [im.quantize(palette=pal, dither=Image.NONE) for im in images]
    gif[0].save(out, save_all=True, append_images=gif[1:], duration=round(1000 / fps), loop=0, optimize=True)
    print('%s: %d frames, %.1f MB' % (out, len(gif), os.path.getsize(out) / 1e6))

    if shutil.which('ffmpeg'):
        pattern = os.path.join(folder, 'frame_%04d' + os.path.splitext(frames[0])[1])
        base = ['ffmpeg', '-y', '-loglevel', 'error', '-framerate', str(fps), '-start_number', str(start),
                '-i', pattern, '-frames:v', str(len(frames)), '-vf', 'scale=trunc(iw/2)*2:trunc(ih/2)*2:flags=neighbor',
                '-pix_fmt', 'yuv420p']
        # H.264 if this ffmpeg has it (TikTok/Reels/Shorts), else VP9 .webm
        for ext, codec in (('.mp4', ['-c:v', 'libx264', '-crf', '18']), ('.mp4', ['-c:v', 'libopenh264', '-b:v', '4M']),
                           ('.webm', ['-c:v', 'libvpx-vp9', '-crf', '24', '-b:v', '0'])):
            video = os.path.splitext(out)[0] + ext
            if subprocess.call(base + codec + [video], stderr=subprocess.DEVNULL) == 0 and os.path.exists(video):
                print('%s: %.1f MB' % (video, os.path.getsize(video) / 1e6))
                break


if __name__ == '__main__':
    main()
