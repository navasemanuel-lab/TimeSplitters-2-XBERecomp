"""The Xbox textures of the sizes the HD fonts/HUD pack has, decoded with their alpha (xbtex.pkl keeps colour only): pickled as
xbalpha.pkl {key: (w, h, rgba, [sources])}, key as in xbtex.pkl. Run in the folder with xbtex.pkl."""
import struct,glob,os,pickle,sys
import numpy as np
sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)))
import xb,xb2
SIZES={(32,32),(64,32),(64,64),(128,256),(128,64),(256,256),(128,128),(16,8),(256,128),(64,16),(32,64),(128,32),(512,512),(512,256)}
def dxt_rgba(data,w,h,bpb):
    rgb=xb.dxt_rgb(data,w,h,bpb)
    bw,bh=w//4,h//4
    a=np.frombuffer(data[:bw*bh*bpb],dtype=np.uint8).reshape(bh,bw,bpb)
    out=np.full((h,w),255,np.uint8)
    if bpb==16:
        a0=a[:,:,0].astype(np.int32);a1=a[:,:,1].astype(np.int32)
        bits=np.zeros((bh,bw),np.uint64)
        for i in range(6): bits|=a[:,:,2+i].astype(np.uint64)<<np.uint64(8*i)
        for k in range(16):
            idx=((bits>>np.uint64(3*k))&np.uint64(7)).astype(np.int32)
            v=np.where(idx==0,a0,np.where(idx==1,a1,0))
            gt=(a0>a1)
            for j in range(1,7):
                v=np.where(idx==j+1,np.where(gt,((7-j)*a0+j*a1)//7,np.where(j<5,((5-j)*a0+j*a1)//5,0)),v)
            v=np.where(idx==7,np.where(gt,(1*a0+6*a1)//7,255),v)
            v=np.where(idx==6,np.where(gt,(2*a0+5*a1)//7,0),v)
            out[(k//4)::4,(k%4)::4]=v.astype(np.uint8)
    else:
        c=a[:,:,0:8]
        c0=c[:,:,0].astype(np.uint16)|(c[:,:,1].astype(np.uint16)<<8)
        c1=c[:,:,2].astype(np.uint16)|(c[:,:,3].astype(np.uint16)<<8)
        bits=c[:,:,4].astype(np.uint32)|(c[:,:,5].astype(np.uint32)<<8)|(c[:,:,6].astype(np.uint32)<<16)|(c[:,:,7].astype(np.uint32)<<24)
        three=~(c0>c1)
        for k in range(16):
            idx=(bits>>(2*k))&3
            out[(k//4)::4,(k%4)::4]=np.where(three&(idx==3),0,255).astype(np.uint8)
    return np.dstack([rgb,out])
def run(root='C:/TS2-XBE/data'):
    res={}
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
            if (w,h) not in SIZES or w%4 or h%4: continue
            body=sz-128
            bpb=None
            for b in (8,16):
                tot=0;ww,hh=w,h
                for lv in range(13):
                    tot+=max(1,ww//4)*max(1,hh//4)*b
                    if abs(tot-body)<=16: bpb=b;break
                    if ww==1 and hh==1: break
                    ww=max(1,ww//2);hh=max(1,hh//2)
                if bpb: break
            if bpb:
                l0=d[off+128:off+128+w*h*bpb//16]
                k=(w,h,xb.gpu_hash(l0),bpb)
                if k not in res: res[k]=(w,h,dxt_rgba(l0,w,h,bpb),[])
            else:
                tot=0;ww,hh=w,h;ok=False
                for lv in range(13):
                    tot+=ww*hh*4
                    if tot==body: ok=True;break
                    if ww==1 and hh==1: break
                    ww=max(1,ww//2);hh=max(1,hh//2)
                if not ok: continue
                l0=d[off+128:off+128+w*h*4]
                k=(w,h,xb.gpu_hash(l0),'argb')
                if k not in res: res[k]=(w,h,xb2.unswizzle(w,h,l0)[:,:,[2,1,0,3]],[])
            res[k][3].append((os.path.relpath(p,root).replace(chr(92),'/'),nm))
    return res
if __name__=='__main__':
    r=run();print(len(r));pickle.dump(r,open('xbalpha.pkl','wb'))
