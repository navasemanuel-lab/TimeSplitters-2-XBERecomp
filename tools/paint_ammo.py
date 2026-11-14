"""Repaints Boston's 32x32 ammo icons as smooth HD art (256x256): the colours of each icon are grouped into flat regions
(k-means), the region borders are smoothed into clean vector-like curves, each region is filled with the soft gradient
of the original colours inside it, then shaded: a rim light, a thin darker line along the borders, a gloss from the
original's own lights. Same drawing, same colours, painted rather than stretched."""
import sys,glob,os,numpy as np,cv2
src,out=sys.argv[1],sys.argv[2]
exec(open(os.path.join(os.path.dirname(os.path.abspath(__file__)),'up_ammo.py')).read().split("os.makedirs")[0].split("src,out=")[0]) if False else None
def scale2x(a):
    h,w=a.shape[:2];p=np.pad(a,((1,1),(1,1),(0,0)),mode='edge')
    B=p[:-2,1:-1];D=p[1:-1,:-2];E=a;F=p[1:-1,2:];H=p[2:,1:-1]
    eq=lambda x,y:np.all(x==y,axis=2,keepdims=True);c=eq(B,H)|eq(D,F)
    o=np.zeros((h*2,w*2,a.shape[2]),a.dtype)
    o[0::2,0::2]=np.where(eq(D,B)&~c,D,E);o[0::2,1::2]=np.where(eq(B,F)&~c,F,E)
    o[1::2,0::2]=np.where(eq(D,H)&~c,D,E);o[1::2,1::2]=np.where(eq(H,F)&~c,F,E);return o
R=512
os.makedirs(out,exist_ok=True)
for f in sorted(glob.glob(src+'/ammo_*.png')):
    im=cv2.imread(f,cv2.IMREAD_UNCHANGED);a0=im[...,3]
    lab=cv2.cvtColor(im[...,:3],cv2.COLOR_BGR2LAB).astype(np.float32)
    feat=np.concatenate([lab,(a0[...,None]/255.0*150).astype(np.float32)],2)
    # edge-directed upscale of the feature image to 512 (nearest-aware), colours only where visible
    big=cv2.resize(im,(R,R),interpolation=cv2.INTER_NEAREST)
    q=im.copy()
    for _ in range(2): q=scale2x(q)
    big=cv2.resize(q,(R,R),interpolation=cv2.INTER_CUBIC)
    # regions
    m=a0>8
    pts=feat[m].reshape(-1,4);k=int(np.clip(len(pts)//40,4,9))
    _,lbl,cen=cv2.kmeans(pts,k,None,(cv2.TERM_CRITERIA_EPS|cv2.TERM_CRITERIA_MAX_ITER,50,0.1),4,cv2.KMEANS_PP_CENTERS)
    L=np.full(a0.shape,-1,np.int32);L[m]=lbl.ravel()
    # transparent pixels join as an own "empty" class so shapes get a soft outline
    prob=np.zeros((R,R,k+1),np.float32)
    for c in range(k+1):
        mc=(L==c) if c<k else (L<0)
        pm=cv2.resize(mc.astype(np.float32),(R,R),interpolation=cv2.INTER_CUBIC)
        prob[...,c]=cv2.GaussianBlur(pm,(0,0),R/32*0.75)
    cls=prob.argmax(2);empty=(cls==k)
    # colour of each pixel: normalised blur of the original colours inside its class, gradient-rich
    bg=big[...,:3].astype(np.float32)
    col=np.zeros((R,R,3),np.float32)
    for c in range(k):
        w=(cls==c).astype(np.float32)
        wb=cv2.GaussianBlur(w,(0,0),R/32*2.2)
        cb=cv2.GaussianBlur(bg*w[...,None],(0,0),R/32*2.2)
        col+=np.where((cls==c)[...,None],cb/np.maximum(wb,1e-3)[...,None],0)
    # detail: the original's lights/darks inside the region, softly
    lum=cv2.cvtColor(big[...,:3],cv2.COLOR_BGR2GRAY).astype(np.float32)
    det=lum-cv2.GaussianBlur(lum,(0,0),R/32*2.2)
    col=col+det[...,None]*0.75
    # lighting: form from the alpha silhouette (top-left light)
    sil=(~empty).astype(np.float32);h=cv2.GaussianBlur(sil,(0,0),R/32*2.0)
    gy,gx=np.gradient(h);shade=np.clip(-(gx*0.7+gy*0.7)*R/32*1.6,-1,1)
    col*=1+0.22*shade[...,None]
    # thin darker line along region borders and the outer edge
    edge=cv2.Laplacian(cls.astype(np.float32),cv2.CV_32F)!=0
    edge=cv2.dilate(edge.astype(np.uint8),np.ones((3,3),np.uint8)).astype(np.float32)
    line=cv2.GaussianBlur(edge,(0,0),1.1)
    col*=1-0.28*line[...,None]
    # rim light on the lower-right inner edge
    inner=cv2.erode((~empty).astype(np.uint8),np.ones((9,9),np.uint8)).astype(np.float32)
    rim=np.clip(cv2.GaussianBlur(sil,(0,0),3)-cv2.GaussianBlur(inner,(0,0),3),0,1)
    col+=rim[...,None]*14
    # alpha: soft shape of the silhouette with anti-aliasing, keeping the original's own translucency
    al=cv2.GaussianBlur(sil,(0,0),1.2)
    ao=cv2.resize(a0.astype(np.float32)/255,(R,R),interpolation=cv2.INTER_CUBIC);ao=cv2.GaussianBlur(ao,(0,0),R/32*0.9)
    al=np.clip(np.minimum(al,1)*np.clip(ao*1.6,0,1)**0.5,0,1)
    o=np.concatenate([np.clip(col,0,255),(al*255)[...,None]],2).astype(np.uint8)
    o=cv2.resize(o,(256,256),interpolation=cv2.INTER_AREA)
    cv2.imwrite(os.path.join(out,os.path.basename(f)),o)
