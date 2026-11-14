"""HD versions of Boston's 32x32 ammo icons: edge-directed upscale (Scale2x, three steps to 256) on premultiplied colour,
then a light edge-preserving smooth and unsharp, so the drawing stays exactly theirs but with clean smooth edges."""
import sys,glob,os,numpy as np,cv2
src,out=sys.argv[1],sys.argv[2]
def scale2x(a):
    h,w=a.shape[:2];p=np.pad(a,((1,1),(1,1),(0,0)),mode='edge')
    B=p[:-2,1:-1];D=p[1:-1,:-2];E=a;F=p[1:-1,2:];H=p[2:,1:-1]
    eq=lambda x,y:np.all(x==y,axis=2,keepdims=True)
    c=eq(B,H)|eq(D,F)  # no edge direction: keep
    E0=np.where(eq(D,B)&~c,D,E);E1=np.where(eq(B,F)&~c,F,E)
    E2=np.where(eq(D,H)&~c,D,E);E3=np.where(eq(H,F)&~c,F,E)
    o=np.zeros((h*2,w*2,a.shape[2]),a.dtype);o[0::2,0::2]=E0;o[0::2,1::2]=E1;o[1::2,0::2]=E2;o[1::2,1::2]=E3;return o
os.makedirs(out,exist_ok=True)
for f in sorted(glob.glob(src+'/ammo_*.png')):
    im=cv2.imread(f,cv2.IMREAD_UNCHANGED).astype(np.float32)
    a=im[...,3:]/255;pm=np.concatenate([im[...,:3]*a,im[...,3:]],2)
    q=np.round(pm).astype(np.uint8)
    for _ in range(2): q=scale2x(q)          # 128
    q=cv2.resize(q,(256,256),interpolation=cv2.INTER_CUBIC).astype(np.float32)
    q=cv2.GaussianBlur(q,(0,0),2.2)          # round the steps
    bl=cv2.GaussianBlur(q,(0,0),3);q=np.clip(q+0.9*(q-bl),0,255)   # sharpen edges back
    al=q[...,3:]/255;rgb=np.where(al>0.01,q[...,:3]/np.maximum(al,0.01),0)
    o=np.concatenate([np.clip(rgb,0,255),np.clip(q[...,3:],0,255)],2).astype(np.uint8)
    cv2.imwrite(os.path.join(out,os.path.basename(f)),o)
