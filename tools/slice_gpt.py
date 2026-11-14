"""Cuts the 5x4 sheet of redrawn ammo icons (same order as Boston's hud/ammo_*.png, alphabetical) into 256x256 PNGs with alpha,
placed where Boston's original icon sits: python -I slice_gpt.py <sheet.png> <boston hud dir> <out dir>"""
import sys,os,numpy as np,cv2
sheet,bos,out=sys.argv[1:4];os.makedirs(out,exist_ok=True)
NAMES=['big','bow','brick','electro','foam','grenade','homingrocket','laser','petrol','plasma','plasmamachine','prox','remote','rocket','shell','small','sniper','sticky','timed','tnt']
GLOW={'laser','plasma','plasmamachine','sticky'}
im=cv2.imread(sheet).astype(np.float32);H,W=im.shape[:2];bg=np.median(np.concatenate([im[:6].reshape(-1,3),im[-6:].reshape(-1,3)]),0)
cw,ch=W/5,H/4
for i,n in enumerate(NAMES):
    x0,y0=int(round((i%5)*cw)),int(round((i//5)*ch));c=im[y0:int(round(y0+ch)),x0:int(round(x0+cw))]
    d=np.linalg.norm(c-bg,axis=2)
    if n in GLOW:
        a=np.clip((c.max(2)-bg.max())/(255-bg.max()),0,1)**0.9;a=np.clip(a*1.15,0,1)
        n_,lab,st,_=cv2.connectedComponentsWithStats((a>0.04).astype(np.uint8))
        ok=np.zeros(a.shape,np.float32)
        for j in range(1,n_):
            x,y,w_,h_=st[j,:4]
            if abs(x+w_/2-a.shape[1]/2)<a.shape[1]*0.4 and abs(y+h_/2-a.shape[0]/2)<a.shape[0]*0.4: ok[lab==j]=1
        a=a*cv2.dilate(ok,np.ones((25,25),np.uint8))
        rgb=np.where(a[...,None]>0.02,np.clip((c-bg*(1-a[...,None]))/np.maximum(a[...,None],0.02),0,255),0)
    else:
        raw=(d>14).astype(np.uint8)
        n_,lab,st,_=cv2.connectedComponentsWithStats((d>(28 if n=="tnt" else 45)).astype(np.uint8))
        big=st[1:,4].max();sel=np.zeros(d.shape,np.uint8)
        for j in range(1,n_):
            x,y,w_,h_=st[j,:4]
            edge=x==0 or y==0 or x+w_>=d.shape[1] or y+h_>=d.shape[0]
            if st[j,4]==big or (st[j,4]>0.02*big and not edge): sel[lab==j]=1
        keep=cv2.morphologyEx(raw,cv2.MORPH_CLOSE,np.ones((5,5),np.uint8))*cv2.dilate(sel,np.ones((31,31) if n=="tnt" else (13,13),np.uint8))
        # fill holes (the dark outline is not background)
        ff=keep.copy();m=np.zeros((keep.shape[0]+2,keep.shape[1]+2),np.uint8);inv=(1-keep).astype(np.uint8)
        cv2.floodFill(inv,m,(0,0),2);filled=(inv!=2).astype(np.float32)
        a=cv2.GaussianBlur(filled,(0,0),0.9);a=np.clip((a-0.35)*1.8,0,1)
        rgb=np.clip((c-bg*(1-a[...,None]))/np.maximum(a[...,None],0.2),0,255)
    ys,xs=np.where(a>0.08);y1,y2,x1,x2=ys.min(),ys.max()+1,xs.min(),xs.max()+1
    rgba=np.dstack([rgb,a*255])[y1:y2,x1:x2]
    # Boston's footprint in the 256 frame
    b=cv2.imread(os.path.join(bos,'ammo_%s.png'%n),cv2.IMREAD_UNCHANGED);ba=b[...,3]
    by,bx=np.where(ba>40);bx1,bx2,by1,by2=bx.min()*8,(bx.max()+1)*8,by.min()*8,(by.max()+1)*8
    s=min((bx2-bx1)/rgba.shape[1],(by2-by1)/rgba.shape[0])
    nw,nh=max(1,round(rgba.shape[1]*s)),max(1,round(rgba.shape[0]*s))
    pm=rgba.copy();pm[...,:3]*=pm[...,3:]/255
    r=cv2.resize(pm,(nw,nh),interpolation=cv2.INTER_AREA if s<1 else cv2.INTER_CUBIC)
    r[...,:3]=np.where(r[...,3:]>1,r[...,:3]/(r[...,3:]/255),0)
    o=np.zeros((256,256,4),np.float32);ox=int((bx1+bx2-nw)/2);oy=int((by1+by2-nh)/2)
    o[oy:oy+nh,ox:ox+nw]=r
    cv2.imwrite(os.path.join(out,'ammo_%s.png'%n),np.clip(o,0,255).astype(np.uint8))
