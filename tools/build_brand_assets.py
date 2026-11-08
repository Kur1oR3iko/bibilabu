#!/usr/bin/env python3
"""Original B/play monogram, authored as vector paths and rasterized for XMB."""
from pathlib import Path
from PIL import Image,ImageDraw,ImageFont,ImageFilter
import math
OUT=Path(__file__).resolve().parents[1]/'assets'
OUT.mkdir(exist_ok=True)
S=4
# One spine, two bowls, two play-shaped negative spaces.
PATH='M166 126 H258 C321 126 346 151 346 195 C346 223 332 246 306 255 C340 265 359 291 359 324 C359 369 324 394 264 394 H166 Z'
HOLES=[[(225,166),(286,198),(225,231)],[(225,283),(292,321),(225,358)]]
def bezier(p,a,b,c):
    return [((1-t)**3*p[0]+3*(1-t)**2*t*a[0]+3*(1-t)*t*t*b[0]+t**3*c[0],
             (1-t)**3*p[1]+3*(1-t)**2*t*a[1]+3*(1-t)*t*t*b[1]+t**3*c[1]) for t in [i/40 for i in range(1,41)]]
points=[(166,126),(258,126)]
for a,b,c in [((321,126),(346,151),(346,195)),((346,223),(332,246),(306,255)),
              ((340,265),(359,291),(359,324)),((359,369),(324,394),(264,394))]:points+=bezier(points[-1],a,b,c)
points += [(166,394)]
def grad(size,top,bottom):
    im=Image.new('RGBA',size);d=ImageDraw.Draw(im)
    for y in range(size[1]):
        t=y/max(1,size[1]-1);col=tuple(round(a*(1-t)+b*t) for a,b in zip(top,bottom))
        d.line((0,y,size[0],y),fill=col)
    return im
def mark(size):
    scale=size/512;mask=Image.new('L',(size,size));d=ImageDraw.Draw(mask)
    d.polygon([(x*scale,y*scale) for x,y in points],fill=255)
    for hole in HOLES:d.polygon([(x*scale,y*scale) for x,y in hole],fill=0)
    im=grad((size,size),(255,255,255,255),(143,153,165,255));im.putalpha(mask);return im
def tile(w,h,radius):
    w*=S;h*=S;im=grad((w,h),(43,48,55,255),(9,11,15,255))
    mask=Image.new('L',(w,h));ImageDraw.Draw(mask).rounded_rectangle((1,1,w-2,h-2),radius*S,fill=255)
    im.putalpha(mask);d=ImageDraw.Draw(im)
    d.rounded_rectangle((S,S,w-S-1,h-S-1),radius*S,outline=(130,138,149,180),width=S)
    d.rounded_rectangle((2*S,2*S,w-2*S-1,h-2*S-1),(radius-1)*S,outline=(255,255,255,24),width=S)
    for j in range(3):
        wave=[(x,(h*0.82+math.sin(x/w*5.2+j*0.3)*h*0.06+j*3*S)) for x in range(0,w,4)]
        d.line(wave,fill=(175,185,198,17+j*5),width=S)
    return im
square=Image.new('RGBA',(512*S,512*S));square.alpha_composite(tile(432,432,90),(40*S,40*S))
symbol=mark(460*S);square.alpha_composite(symbol,(16*S,-4*S))
square.resize((512,512),Image.Resampling.LANCZOS).save(OUT/'logo-512.png')
icon=Image.new('RGBA',(320*S,176*S));icon.alpha_composite(tile(304,160,25),(8*S,8*S))
symbol=mark(208*S);icon.alpha_composite(symbol,(-23*S,-24*S))
fontpaths=[Path('C:/Windows/Fonts/segoeuil.ttf'),Path('/mnt/c/Windows/Fonts/segoeuil.ttf'),Path('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf')]
font=ImageFont.truetype(str(next(p for p in fontpaths if p.exists())),27*S)
ImageDraw.Draw(icon).text((132*S,65*S),'bibilabu',font=font,fill=(235,239,244,255))
d=ImageDraw.Draw(icon);d.line((135*S,109*S,278*S,109*S),fill=(158,169,184,65),width=S)
icon.resize((320,176),Image.Resampling.LANCZOS).save(OUT/'ICON0.PNG')
# Editable source for the standalone mark, with identical contours.
holes=' '.join('M'+' L'.join(f'{x} {y}' for x,y in h)+' Z' for h in HOLES)
svg=f'''<svg xmlns="http://www.w3.org/2000/svg" width="512" height="512" viewBox="0 0 512 512">
<defs><linearGradient id="glass" x2="0" y2="1"><stop stop-color="#2b3037"/><stop offset="1" stop-color="#090b0f"/></linearGradient>
<linearGradient id="silver" x2="0" y2="1"><stop stop-color="#fff"/><stop offset="1" stop-color="#8f99a5"/></linearGradient></defs>
<rect x="40" y="40" width="432" height="432" rx="90" fill="url(#glass)" stroke="#828a95"/>
<path d="{PATH} {holes}" fill="url(#silver)" fill-rule="evenodd"/></svg>'''
(OUT/'logo-mark.svg').write_text(svg,encoding='utf-8')
print('Created original silver B/play assets:',OUT)
