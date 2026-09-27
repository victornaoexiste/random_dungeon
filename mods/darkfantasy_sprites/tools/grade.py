# Tratamento dark fantasy sobre os sprites BASE (fantasycore/empyrean_campaign). Nao altera o base:
# grava em darkfantasy_sprites/images/{avatar,enemies}/... com o mesmo caminho relativo e mesmo tamanho.
import os,sys,glob
from PIL import Image,ImageEnhance
SAT,BRI,CON=0.48,0.80,1.20          # dessatura, escurece, contraste
GAMMA=(1.12,1.08,0.95)               # R,G,B: sombras frias, luzes um pouco mais quentes
LUT=[[int(255*((i/255)**g)+.5) for i in range(256)] for g in GAMMA]
def grade(im):
    im=im.convert('RGBA'); r,g,b,a=im.split(); rgb=Image.merge('RGB',(r,g,b))
    rgb=ImageEnhance.Color(rgb).enhance(SAT); rgb=ImageEnhance.Contrast(rgb).enhance(CON); rgb=ImageEnhance.Brightness(rgb).enhance(BRI)
    r,g,b=rgb.split(); r,g,b=r.point(LUT[0]),g.point(LUT[1]),b.point(LUT[2])
    return Image.merge('RGBA',(r,g,b,a))
HERE=os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MODS=os.path.dirname(HERE)
def sources():
    out={}
    for m in ('fantasycore','empyrean_campaign'):       # empyrean vence em caso de mesmo caminho
        for d in ('avatar','enemies'):
            for f in glob.glob(f'{MODS}/{m}/images/{d}/**/*.png',recursive=True):
                out[os.path.relpath(f,f'{MODS}/{m}')]=f
    return out
def work(item):
    rel,src=item; dst=f'{HERE}/{rel}'
    os.makedirs(os.path.dirname(dst),exist_ok=True)
    im=Image.open(src); g=grade(im); assert g.size==im.size
    g.save(dst,compress_level=6); return rel
if __name__=='__main__':
    from multiprocessing import Pool
    items=sorted(sources().items())
    with Pool(int(os.environ.get('JOBS','6'))) as p:
        for i,rel in enumerate(p.imap_unordered(work,items),1):
            if i%20==0 or i==len(items): print(i,'/',len(items),flush=True)
