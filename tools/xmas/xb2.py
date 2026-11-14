"""The Xbox's uncompressed textures (the level photos): 32-bit BGRA, swizzled, with a few smaller levels, in the paks as .xbt
whose size is no DXT chain. Adds them to xbtex.pkl (run in the folder that holds it, after xb.py): key (w, h, gpu_hash of the first
level's bytes, 'argb'), the picture decoded, n the bytes the game hashes (w*h*4)."""
import struct,glob,os,pickle,sys
import numpy as np
sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)))
import xb
def unswizzle(w,h,data):
    arr=np.frombuffer(data,np.uint8).reshape(-1,4)
    ys,xs=np.mgrid[0:h,0:w]
    bx=w.bit_length()-1;by=h.bit_length()-1
    m=np.zeros((h,w),np.int64);pos=0;ix=iy=0
    while ix<bx or iy<by:  # x first, then y, until a side runs out; the rest of the longer side follows
        if ix<bx: m|=((xs>>ix)&1)<<pos;pos+=1;ix+=1
        if iy<by: m|=((ys>>iy)&1)<<pos;pos+=1;iy+=1
    return arr[m]
def scan(root):
    tex={}
    for p in sorted(glob.glob(root+'/**/*.pak',recursive=True)):
        d=open(p,'rb').read()
        if d[:4]!=b'P8CK':continue
        do,n,no,ns=struct.unpack_from('<4I',d,4)
        names=d[no:no+ns]
        for i in range(n):
            a,sz,off=struct.unpack_from('<III',d,do+i*12)
            nm=names[a:names.index(b'\0',a)].decode()
            if not nm.endswith('.xbt'):continue
            w,h=struct.unpack_from('<2I',d,off)
            body=sz-128
            if w%4 or h%4 or w&(w-1) or h&(h-1): continue
            # DXT with fewer levels than a full chain (no smaller levels at all, often)
            dxt=None
            for bpb in (8,16):
                tot=0;ww,hh=w,h
                for lv in range(1,13):
                    tot+=max(1,ww//4)*max(1,hh//4)*bpb
                    if abs(tot-body)<=16: dxt=bpb;break
                    if ww==1 and hh==1: break
                    ww=max(1,ww//2);hh=max(1,hh//2)
                if dxt: break
            if dxt:
                if abs(body-xb.chain(w,h,dxt))<=16: continue  # the ordinary scan has it
                l0=d[off+128:off+128+w*h*dxt//16]
                k=(w,h,xb.gpu_hash(l0),dxt)
                if k not in tex: tex[k]=dict(w=w,h=h,bpb=dxt,rgb=xb.dxt_rgb(l0,w,h,dxt),src=[],extra=True)
                tex[k]['src'].append((os.path.relpath(p,root).replace(chr(92),'/'),nm))
                continue
            tot=0;ww,hh=w,h;ok=False
            for lv in range(1,13):
                tot+=ww*hh*4
                if tot==body: ok=True;break
                if ww==1 and hh==1: break
                ww=max(1,ww//2);hh=max(1,hh//2)
            if not ok: continue
            l0=d[off+128:off+128+w*h*4]
            k=(w,h,xb.gpu_hash(l0),'argb')
            if k not in tex: tex[k]=dict(w=w,h=h,bpb=4,rgb=unswizzle(w,h,l0)[:,:,[2,1,0]],src=[],extra=True)
            tex[k]['src'].append((os.path.relpath(p,root).replace(chr(92),'/'),nm))
    return tex
if __name__=='__main__':
    T=pickle.load(open('xbtex.pkl','rb'))
    T={k:v for k,v in T.items() if not v.get('extra')}
    t=scan('C:/TS2-XBE/data')
    print('argb textures',len(t))
    T.update(t)
    pickle.dump(T,open('xbtex.pkl','wb'))
