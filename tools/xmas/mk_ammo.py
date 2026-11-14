"""The HUD's ammunition icons (ammo_*.xbt, 32x32) redrawn at 256x256 into tools/hd_ammo/*.png for mk_hud.py.
The cartridges, shells and rockets are drawn anew (shaded solids, in the place and size the console's picture has them); the
rest are the console's pictures grown eight times with a smooth outline and cleaned colours. Run in the folder with xbalpha.pkl:
python -I mk_ammo.py <out dir>"""
import os,pickle,sys
import numpy as np,cv2
from PIL import Image
X=pickle.load(open('xbalpha.pkl','rb'))
out=sys.argv[1];os.makedirs(out,exist_ok=True)
S=8;N=32*S;SS=3  # a drawing made at SS times, brought down
def src(b):
    for k,v in X.items():
        if v[0]==32 and any(s[1].endswith('/'+b+'.xbt') for s in v[3]): return v[2]
def bbox(a):
    ys,xs=np.where(a[...,3]>40);return xs.min()*S,ys.min()*S,(xs.max()+1)*S,(ys.max()+1)*S
def canvas(): return np.zeros((N*SS,N*SS,4),np.float32)
def paint(c,mask,rgb,alpha=1.0):
    m=(np.clip(mask,0,1)*alpha)[...,None]
    c[...,:3]=rgb*m+c[...,:3]*(1-m)*1 if False else (rgb*m+c[...,:3]*c[...,3:4]*(1-m))/np.maximum(m+c[...,3:4]*(1-m),1e-4)
    c[...,3:4]=m+c[...,3:4]*(1-m)
def finish(c):
    d=cv2.resize(c,(N,N),interpolation=cv2.INTER_AREA)
    return np.clip(d,0,255).astype(np.uint8) if False else np.dstack([np.clip(d[...,:3],0,255),np.clip(d[...,3]*255,0,255)]).astype(np.uint8)
def cyl_shade(xs,x0,x1,base,hi=0.55,lo=0.45):
    t=np.clip((xs-x0)/max(x1-x0,1),0,1)*2-1  # -1..1 across
    lit=np.sqrt(np.clip(1-t*t,0,1))
    k=lo+(1-lo)*(0.35+0.65*lit)+hi*np.exp(-((t+0.35)/0.18)**2)*0.5
    return np.array(base,np.float32)[None,None,:]*k[...,None]
def solid(c,shape_mask,xs,x0,x1,base,**kw):
    col=cyl_shade(xs,x0,x1,base,**kw);
    m=np.clip(shape_mask,0,1)[...,None]
    new_a=m+c[...,3:4]*(1-m)
    c[...,:3]=np.where(new_a>1e-4,(col*m+c[...,:3]*c[...,3:4]*(1-m))/np.maximum(new_a,1e-4),0)
    c[...,3:4]=new_a
def grid():
    ys,xs=np.mgrid[0:N*SS,0:N*SS].astype(np.float32);return xs,ys
def aa(d,soft=1.4):  # signed distance in pixels -> coverage
    return np.clip(0.5-d/soft,0,1)
def cartridge(box,casing,tip,tip_frac=0.34,rim=True):
    x0,y0,x1,y1=[v*SS for v in box];xs,ys=grid()
    cx=(x0+x1)/2;w=(x1-x0)/2;H=y1-y0;ty=y0+H*tip_frac
    c=canvas()
    # the bullet: an ogive nose over the casing's top
    nose=np.where(ys<ty,w*0.86*np.sqrt(np.clip(1-((ty-ys)/(ty-y0+1e-3))**1.7,0,1)),w*0.86)
    inside_tip=(ys>=y0)&(ys<ty+H*0.06)&(np.abs(xs-cx)<=nose)
    solid(c,aa(np.where(inside_tip,-1,1)*1.0),xs,cx-w*0.86,cx+w*0.86,tip,hi=0.7)
    cas_top=ty
    mask=(ys>=cas_top)&(ys<=y1)&(np.abs(xs-cx)<=w)
    # the casing narrows a little to the neck
    solid(c,aa(np.where(mask,-1,1)*1.0),xs,cx-w,cx+w,casing,hi=0.6)
    if rim:
        rm=(ys>y1-H*0.09)&(ys<=y1)&(np.abs(xs-cx)<=w)
        solid(c,aa(np.where(rm,-1,1)*1.0),xs,cx-w,cx+w,tuple(v*0.7 for v in casing),hi=0.3)
        gm=(ys>y1-H*0.2)&(ys<y1-H*0.17)&(np.abs(xs-cx)<=w)
        paint(c,aa(np.where(gm,-1,1)*1.0),np.array([40,30,10],np.float32),0.55)
    nm=(ys>=ty-H*0.015)&(ys<ty+H*0.03)&(np.abs(xs-cx)<=w)
    paint(c,aa(np.where(nm,-1,1)*1.0),np.array([50,35,15],np.float32),0.5)
    return finish(c)
def shell(box):
    x0,y0,x1,y1=[v*SS for v in box];xs,ys=grid();cx=(x0+x1)/2;w=(x1-x0)/2;H=y1-y0;c=canvas()
    body=(ys>=y0)&(ys<=y1-H*0.28)&(np.abs(xs-cx)<=w)
    solid(c,aa(np.where(body,-1,1)*1.0),xs,cx-w,cx+w,(205,55,30),hi=0.5)
    crimp=(ys>=y0)&(ys<=y0+H*0.07)&(np.abs(xs-cx)<=w)
    paint(c,aa(np.where(crimp,-1,1)*1.0),np.array([120,25,12],np.float32),0.6)
    base=(ys>y1-H*0.28)&(ys<=y1)&(np.abs(xs-cx)<=w*1.0)
    solid(c,aa(np.where(base,-1,1)*1.0),xs,cx-w,cx+w,(222,184,96),hi=0.7)
    rim=(ys>y1-H*0.06)&(ys<=y1)&(np.abs(xs-cx)<=w)
    paint(c,aa(np.where(rim,-1,1)*1.0),np.array([120,95,40],np.float32),0.7)
    return finish(c)
def rocket(box,body,nose,fin):
    x0,y0,x1,y1=[v*SS for v in box];xs,ys=grid();cx=(x0+x1)/2;w=(x1-x0)/2;H=y1-y0;c=canvas()
    ty=y0+H*0.26
    prof=np.where(ys<ty,w*0.8*np.sqrt(np.clip(1-((ty-ys)/(ty-y0+1e-3))**1.6,0,1)),w*0.8)
    m1=(ys>=y0)&(ys<=y0+H*0.82)&(np.abs(xs-cx)<=prof)
    solid(c,aa(np.where(m1&(ys>=ty),-1,1)*1.0),xs,cx-w*0.8,cx+w*0.8,body,hi=0.6)
    m0=(ys>=y0)&(ys<ty+H*0.02)&(np.abs(xs-cx)<=prof)
    solid(c,aa(np.where(m0,-1,1)*1.0),xs,cx-w*0.8,cx+w*0.8,nose,hi=0.7)
    band=(ys>ty+H*0.2)&(ys<ty+H*0.27)&(np.abs(xs-cx)<=w*0.8)
    paint(c,aa(np.where(band,-1,1)*1.0),np.array(fin,np.float32)*0.9,0.9)
    # fins: two swept shapes either side of the tail, a nozzle between
    fy=y0+H*0.66
    for sgn in (-1,1):
        t=(ys-fy)/(y1-fy+1e-3)
        edge=cx+sgn*(w*0.7+w*0.3*np.clip(t,0,1))
        inner=cx+sgn*w*0.55
        mk=(ys>=fy)&(ys<=y1)&(((xs-inner)*sgn>=0)&((xs-edge)*sgn<=0))&(ys<=y1-H*0.04*(1-np.clip(t,0,1)))
        solid(c,aa(np.where(mk,-1,1)*1.0),xs,cx-w,cx+w,fin,hi=0.2,lo=0.6)
    noz=(ys>y0+H*0.8)&(ys<=y1)&(np.abs(xs-cx)<=w*0.5)
    solid(c,aa(np.where(noz,-1,1)*1.0),xs,cx-w*0.5,cx+w*0.5,(70,70,76),hi=0.3)
    return finish(c)
def smooth_up(a):
    rgb=a[...,:3].astype(np.float32);al=a[...,3].astype(np.float32)/255
    m=(al>=0.5).astype(np.float32);acc=rgb*m[...,None];w=m.copy()
    for _ in range(6):
        k=np.ones((3,3),np.float32);a2=cv2.filter2D(acc,-1,k);w2=cv2.filter2D(w,-1,k);new=(w==0)&(w2>0)
        acc=np.where(new[...,None],a2/np.maximum(w2,1e-6)[...,None],acc);w=np.where(new,1,w);acc=acc*(w>0)[...,None]
    rgb=np.where((m>0)[...,None],rgb,acc)
    big=cv2.resize(rgb,(N,N),interpolation=cv2.INTER_CUBIC)
    for _ in range(2): big=cv2.bilateralFilter(big.astype(np.float32),9,22,6)
    ab=cv2.resize(al,(N,N),interpolation=cv2.INTER_CUBIC);ab=cv2.GaussianBlur(ab,(0,0),S*0.8);ab=np.clip((ab-0.5)*4+0.5,0,1)
    bl=cv2.GaussianBlur(big,(0,0),1.6);big=np.clip(big+(big-bl)*0.8,0,255)
    return np.dstack([big,ab*255]).astype(np.uint8)
from PIL import ImageDraw,ImageFilter
def pil_canvas(): return Image.new('RGBA',(N*SS,N*SS),(0,0,0,0))
def done(im): return np.asarray(im.resize((N,N),Image.LANCZOS))
def ring(box,col):
    x0,y0,x1,y1=[v*SS for v in box];cx,cy=float(x0+x1)/2,float(y0+y1)/2;r=float(min(x1-x0,y1-y0))/2*0.94
    im=pil_canvas();glow=Image.new('RGBA',im.size,(0,0,0,0));g=ImageDraw.Draw(glow)
    g.ellipse([cx-r,cy-r,cx+r,cy+r],outline=col+(255,),width=int(r*0.22))
    glow=glow.filter(ImageFilter.GaussianBlur(r*0.08));im.alpha_composite(glow)
    d=ImageDraw.Draw(im)
    d.ellipse([cx-r*0.9,cy-r*0.9,cx+r*0.9,cy+r*0.9],outline=tuple(min(255,int(c*1.15)+40) for c in col)+(255,),width=int(r*0.1))
    d.ellipse([cx-r*0.72,cy-r*0.72,cx+r*0.72,cy+r*0.72],outline=col+(200,),width=int(r*0.04))
    # a gun's side in the middle: a body, a barrel, a grip
    gx,gy=cx-r*0.5,cy-r*0.12
    d.rounded_rectangle([gx,gy,gx+r*0.85,gy+r*0.26],radius=r*0.06,fill=(235,240,245,255))
    d.rounded_rectangle([gx+r*0.8,gy+r*0.05,gx+r*1.05,gy+r*0.17],radius=r*0.04,fill=(210,216,224,255))
    d.polygon([(gx+r*0.12,gy+r*0.24),(gx+r*0.34,gy+r*0.24),(gx+r*0.28,gy+r*0.52),(gx+r*0.1,gy+r*0.52)],fill=(205,212,220,255))
    return done(im)
def diamond(box,fill,edge,glyph):
    x0,y0,x1,y1=[v*SS for v in box];cx,cy=float(x0+x1)/2,float(y0+y1)/2;r=float(min(x1-x0,y1-y0))/2*0.97
    im=pil_canvas();d=ImageDraw.Draw(im)
    pts=lambda k:[(cx,cy-r*k),(cx+r*k,cy),(cx,cy+r*k),(cx-r*k,cy)]
    d.polygon(pts(1.0),fill=edge+(255,));d.polygon(pts(0.86),fill=fill+(255,))
    glyph(d,cx,cy,r)
    return done(im)
def extinguisher(d,cx,cy,r):
    w=(255,255,255,255)
    d.rounded_rectangle([cx-r*0.26,cy-r*0.12,cx+r*0.12,cy+r*0.5],radius=r*0.12,fill=w)
    d.rounded_rectangle([cx-r*0.14,cy-r*0.3,cx,cy-r*0.08],radius=r*0.04,fill=w)
    d.rounded_rectangle([cx-r*0.24,cy-r*0.36,cx+r*0.08,cy-r*0.26],radius=r*0.04,fill=w)
    d.polygon([(cx,cy-r*0.3),(cx+r*0.42,cy-r*0.38),(cx+r*0.44,cy-r*0.26),(cx+r*0.04,cy-r*0.2)],fill=w)
def flame(d,cx,cy,r):
    k=(20,10,10,255)
    d.polygon([(cx,cy-r*0.5),(cx+r*0.2,cy-r*0.16),(cx+r*0.34,cy-r*0.3),(cx+r*0.36,cy+r*0.16),(cx+r*0.18,cy+r*0.4),(cx-r*0.18,cy+r*0.4),(cx-r*0.36,cy+r*0.16),(cx-r*0.3,cy-r*0.2),(cx-r*0.14,cy-r*0.02)],fill=k)
    d.rounded_rectangle([cx-r*0.3,cy+r*0.5,cx+r*0.3,cy+r*0.56],radius=r*0.02,fill=k)
names=sorted({s[1].split('/')[-1][:-4] for v in X.values() if v[0]==32 for s in v[3] if s[1].split('/')[-1].startswith('ammo_')})
for b in names:
    a=src(b);box=bbox(a);img=None
    if b in('ammo_big','ammo_sniper'): img=cartridge(box,(222,178,76),(186,120,60),0.36)
    elif b=='ammo_small': img=cartridge(box,(215,165,60),(176,112,52),0.34)
    elif b=='ammo_shell': img=shell(box)
    elif b=='ammo_rocket': img=rocket(box,(150,176,96),(240,240,236),(60,60,66))
    elif b=='ammo_homingrocket': img=rocket(box,(222,150,110),(246,236,226),(70,64,66))
    elif b=='ammo_laser': img=ring(box,(235,80,40))
    elif b=='ammo_plasma': img=ring(box,(160,80,235))
    elif b=='ammo_plasmamachine': img=ring(box,(30,190,235))
    elif b=='ammo_foam': img=diamond(box,(20,60,235),(0,200,235),extinguisher)
    elif b=='ammo_petrol': img=diamond(box,(255,100,20),(40,10,10),flame)
    if img is None: continue  # the rest keep the console's pictures
    Image.fromarray(img,'RGBA').save(os.path.join(out,b+'.png'))
print(len(os.listdir(out)),'icons')
