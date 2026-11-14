"""Ammo icons redrawn at 256x256 from Boston's 32x32: crisp vector-like silhouette (smooth curve through the original's
shape), colours taken only from inside the shape (no halo), smooth gradients, a thin darker rim line, a soft
inner light. Per-icon overrides give straight-edged shapes as exact polygons."""
import sys,glob,os,numpy as np,cv2
src,out=sys.argv[1],sys.argv[2]
R=1024;O=256;U=R/32.0
def scale2x(a):
    h,w=a.shape[:2];p=np.pad(a,((1,1),(1,1),(0,0)),mode='edge')
    B=p[:-2,1:-1];D=p[1:-1,:-2];E=a;F=p[1:-1,2:];H=p[2:,1:-1]
    eq=lambda x,y:np.all(x==y,axis=2,keepdims=True);c=eq(B,H)|eq(D,F)
    o=np.zeros((h*2,w*2,a.shape[2]),a.dtype)
    o[0::2,0::2]=np.where(eq(D,B)&~c,D,E);o[0::2,1::2]=np.where(eq(B,F)&~c,F,E)
    o[1::2,0::2]=np.where(eq(D,H)&~c,D,E);o[1::2,1::2]=np.where(eq(H,F)&~c,F,E);return o
def fill_out(bgr,solid):
    """colours of the not-solid pixels = nearest solid ones (so no dark/transparent bleed)"""
    d,idx=cv2.distanceTransformWithLabels((~solid).astype(np.uint8),cv2.DIST_L2,3,labelType=cv2.DIST_LABEL_PIXEL)
    ys,xs=np.where(solid);lab=np.zeros(solid.shape,np.int32)
    ids=np.arange(1,len(ys)+1);lab[ys,xs]=ids
    # label->coords
    lut=np.zeros((len(ys)+1,2),np.int32);lut[1:,0]=ys;lut[1:,1]=xs
    c=lut[idx];return bgr[c[...,0],c[...,1]]
POLY={}  # name -> list of polygons in 32-unit coords (override silhouette)
def draw_poly(polys):
    m=np.zeros((R,R),np.uint8)
    for p in polys: cv2.fillPoly(m,[np.round(np.array(p)*U*16).astype(np.int32)],255,lineType=cv2.LINE_AA,shift=4)
    return m.astype(np.float32)/255
for f in sorted(glob.glob(src+'/ammo_*.png')):
    name=os.path.basename(f)[5:-4]
    im=cv2.imread(f,cv2.IMREAD_UNCHANGED);a0=im[...,3]
    solid=a0>=128;soft=name in('timed','bow','electro','laser','plasma','plasmamachine')
    col=fill_out(im[...,:3],solid) if solid.any() else im[...,:3]
    cq=col.copy()
    q=np.dstack([cq,np.zeros_like(a0)]);
    for _ in range(3): q=scale2x(q)           # 256
    c=cv2.resize(q[...,:3],(R,R),interpolation=cv2.INTER_CUBIC).astype(np.float32)
    c=cv2.GaussianBlur(c,(0,0),U*0.55)
    # silhouette
    m0=cv2.resize((a0>96).astype(np.uint8)*255,(32,32),interpolation=cv2.INTER_NEAREST)
    am=np.dstack([a0,a0,a0,a0]);
    for _ in range(3): am=scale2x(am)
    am=cv2.resize(am[...,0],(R,R),interpolation=cv2.INTER_CUBIC).astype(np.float32)/255
    am=cv2.GaussianBlur(am,(0,0),U*0.35)
    if name in POLY: sil=draw_poly(POLY[name])
    elif soft: sil=np.clip(am*1.3,0,1)
    else: sil=np.clip((am-0.30)*U*0.5+0.5,0,1)   # crisp but antialiased
    # thin darker rim along the border, inner light on the upper-left
    inside=cv2.GaussianBlur(sil,(0,0),U*0.45)
    rim=np.clip(1-inside*1.0,0,1)*np.clip(sil*2,0,1)
    d=cv2.GaussianBlur(sil,(0,0),U*0.9);gy,gx=np.gradient(d)
    light=np.clip(-(gx+gy)*U*0.9,-1,1)
    c=c*(1-0.30*rim[...,None])*(1+0.13*light[...,None])
    o=np.dstack([np.clip(c,0,255),np.clip(sil,0,1)*255]).astype(np.uint8)
    o=cv2.resize(o,(O,O),interpolation=cv2.INTER_AREA)
    cv2.imwrite(os.path.join(out,os.path.basename(f)),o)
