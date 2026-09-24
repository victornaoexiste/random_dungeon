# Arte do menu principal / multiplayer / botoes, gerada do zero (nada reaproveitado do fantasycore).
import os, random
from PIL import Image, ImageDraw, ImageFilter, ImageFont, ImageChops
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FONT = os.path.join(os.path.dirname(ROOT), 'fantasycore/fonts/BonaNovaSC-Bold.ttf')
random.seed(11)
BRASS=(150,116,66); BRASS_D=(72,54,34); EMBER=(150,34,30); INK=(9,7,8)
def out(p): 
    p=os.path.join(ROOT,p); os.makedirs(os.path.dirname(p),exist_ok=True); return p
def noise(size,sigma,blur=0):
    n=Image.effect_noise(size,sigma).convert('L')
    return n.filter(ImageFilter.GaussianBlur(blur)) if blur else n

# ---------- fundo 1920x1080: pedra escura, arco gotico, brasa no chao, vinheta ----------
def background():
    W,H=1920,1080
    base=Image.new('RGB',(W,H),(13,10,11))
    stone=noise((W,H),38,1.2); stone=Image.eval(stone,lambda v:int(v*0.20)+4)
    base=ImageChops.add(base,Image.merge('RGB',(stone,stone,stone)))
    d=ImageDraw.Draw(base,'RGBA')
    for y in range(0,H,72):                                   # juntas de bloco de pedra
        d.line([0,y,W,y],fill=(0,0,0,70)); off=36*((y//72)%2)
        for x in range(-off,W,144): d.line([x,y,x,y+72],fill=(0,0,0,55))
    cx=W//2; top=120                                          # arco gotico central
    for i,(w,a) in enumerate([(560,90),(530,60),(500,40)]):
        d.arc([cx-w,top,cx+w,top+2*w*0.9],180,360,fill=BRASS+(a,),width=3)
        d.line([cx-w,top+w*0.9,cx-w,H],fill=BRASS+(a,),width=3); d.line([cx+w,top+w*0.9,cx+w,H],fill=BRASS+(a,),width=3)
    glow=Image.new('L',(W,H),0); g=ImageDraw.Draw(glow)      # brasa vermelha subindo do chao
    g.ellipse([cx-700,H-260,cx+700,H+340],fill=170); glow=glow.filter(ImageFilter.GaussianBlur(120))
    red=Image.new('RGB',(W,H),EMBER); base=Image.composite(red,base,Image.eval(glow,lambda v:int(v*0.55)))
    for _ in range(90):                                       # fagulhas
        x=random.randint(cx-520,cx+520); y=random.randint(H//2,H-40); r=random.choice([1,1,2]); a=random.randint(70,200)
        ImageDraw.Draw(base,'RGBA').ellipse([x-r,y-r,x+r,y+r],fill=(210,90,40,a))
    vig=Image.radial_gradient('L').resize((W,H)); vig=Image.eval(vig,lambda v:min(255,int(v*1.25)))
    base=Image.composite(Image.new('RGB',(W,H),INK),base,vig.point(lambda v:max(0,v-60)))
    base.save(out('images/menus/backgrounds/df_menu.jpg'),quality=90)

# ---------- logo ----------
def logo():
    W,H=760,170; im=Image.new('RGBA',(W,H),(0,0,0,0)); d=ImageDraw.Draw(im)
    txt='RANDOM DUNGEON'; size=104
    f1=ImageFont.truetype(FONT,size)
    while d.textlength(txt,font=f1)>W-60: size-=2; f1=ImageFont.truetype(FONT,size)
    w=d.textlength(txt,font=f1); ty=10+(104-size)//2
    d.text(((W-w)/2+3,ty+4),txt,font=f1,fill=(0,0,0,220)); d.text(((W-w)/2,ty),txt,font=f1,fill=(214,198,170,255))
    y=140; d.line([70,y,W-70,y],fill=BRASS+(255,),width=2)
    for x,sg in ((70,1),(W-70,-1)): d.polygon([(x,y),(x+10*sg,y-6),(x+20*sg,y),(x+10*sg,y+6)],fill=BRASS+(255,))
    im.save(out('images/menus/df_logo.png'))

# ---------- botoes: folha 384x192 = 4 estados de 384x48 (normal, pressionado, hover, desativado) ----------
def button_state(kind):
    W,H=384,48; im=Image.new('RGBA',(W,H),(0,0,0,0)); d=ImageDraw.Draw(im)
    fill={'n':(26,20,21),'p':(15,11,12),'h':(40,22,22),'x':(22,20,20)}[kind]
    edge={'n':BRASS_D,'p':BRASS_D,'h':(176,52,44),'x':(50,44,40)}[kind]
    hi=(60,48,44) if kind!='x' else (34,32,30)
    d.polygon([(10,0),(W-11,0),(W-1,10),(W-1,H-11),(W-11,H-1),(10,H-1),(0,H-11),(0,10)],fill=fill,outline=edge)  # cantos chanfrados
    d.line([12,2,W-13,2],fill=hi)                                                        # luz de cima
    d.line([12,H-3,W-13,H-3],fill=(0,0,0,255))
    if kind=='p': d.line([12,3,W-13,3],fill=(0,0,0,255))
    if kind=='h': d.rectangle([14,5,W-15,H-6],outline=(120,32,30,255))
    for x in (6,W-7): d.ellipse([x-2,H//2-2,x+2,H//2+2],fill=edge if kind!='x' else (44,40,38,255))
    return im
def buttons():
    sheet=Image.new('RGBA',(384,192),(0,0,0,0))
    for i,k in enumerate('nphx'): sheet.alpha_composite(button_state(k),(0,48*i))
    sheet.save(out('images/menus/buttons/button_default.png'))

# ---------- campo de texto: 256x64 = normal / edicao ----------
def input_field():
    W,H=256,32; sheet=Image.new('RGBA',(W,H*2),(0,0,0,0))
    for i,edge in enumerate([BRASS_D,(176,52,44)]):
        im=Image.new('RGBA',(W,H),(0,0,0,0)); d=ImageDraw.Draw(im)
        d.rectangle([0,0,W-1,H-1],fill=(8,6,7,255),outline=edge+(255,)); d.line([1,1,W-2,1],fill=(0,0,0,255)); d.line([1,H-2,W-2,H-2],fill=(40,32,30,255))
        sheet.alpha_composite(im,(0,H*i))
    sheet.save(out('images/menus/input.png'))

# ---------- painel do multiplayer 640x460 ----------
def panel(name,W,H):
    im=Image.new('RGBA',(W,H),(14,11,12,238)); d=ImageDraw.Draw(im)
    tex=noise((W,H),30,0.8); tex=Image.eval(tex,lambda v:int(v*0.06)); im=Image.composite(im,Image.merge('RGBA',(tex,tex,tex,Image.new('L',(W,H),0))),Image.new('L',(W,H),255))
    d=ImageDraw.Draw(im)
    d.rectangle([0,0,W-1,H-1],outline=BRASS+(255,),width=2); d.rectangle([6,6,W-7,H-7],outline=(48,36,30,255))
    d.rectangle([8,8,W-9,52],fill=(26,12,14,255)); d.line([8,52,W-9,52],fill=(120,32,30,255))
    for x,y in ((14,14),(W-15,14),(14,H-15),(W-15,H-15)): d.ellipse([x-3,y-3,x+3,y+3],fill=BRASS+(255,))
    im.save(out(f'images/menus/{name}.png'))

background(); logo(); buttons(); input_field(); panel('df_panel_multiplayer',640,460)
print('arte gerada')
