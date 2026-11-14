import struct,glob,os,pickle,sys
import numpy as np
M64=(1<<64)-1;K0,K1=0x9E3779B97F4A7C15,0xE7037ED1A0B428DB
def mix(a,b):
    r=(a&M64)*(b&M64);return (r^(r>>64))&M64
def gpu_hash(b,h=1469598103934665603):
    n=len(b);seed=h^K0;i=0
    while i+16<=n:
        x,y=struct.unpack_from('<QQ',b,i);seed=mix(x^K1,y^seed);i+=16
    r=n-i
    if r:
        if r>=8:
            x=struct.unpack_from('<Q',b,i)[0];y=struct.unpack_from('<Q',b,n-8)[0]
        else:
            x=int.from_bytes(b[i:n],'little');y=0
        seed=mix(x^K1,y^seed^r)
    return mix(seed^K0,n^K1)
def chain(w,h,bpb):
    t=0
    while True:
        t+=max(1,(w+3)//4)*max(1,(h+3)//4)*bpb
        if w==1 and h==1:break
        w=max(1,w//2);h=max(1,h//2)
    return t
def dxt_rgb(data,w,h,bpb):
    bw,bh=w//4,h//4
    a=np.frombuffer(data[:bw*bh*bpb],dtype=np.uint8).reshape(bh,bw,bpb)
    c=a[:,:,bpb-8:]
    c0=c[:,:,0].astype(np.uint16)|(c[:,:,1].astype(np.uint16)<<8)
    c1=c[:,:,2].astype(np.uint16)|(c[:,:,3].astype(np.uint16)<<8)
    def rgb(c):
        r=((c>>11)&31)*255//31;g=((c>>5)&63)*255//63;b=(c&31)*255//31
        return np.stack([r,g,b],-1).astype(np.int32)
    p0,p1=rgb(c0),rgb(c1)
    four=(c0>c1)[...,None]|(bpb==16)
    p2=np.where(four,(2*p0+p1)//3,(p0+p1)//2);p3=np.where(four,(p0+2*p1)//3,0)
    pal=np.stack([p0,p1,p2,p3],2) # bh,bw,4,3
    idx=np.zeros((bh,bw,16),np.uint8)
    bits=c[:,:,4].astype(np.uint32)|(c[:,:,5].astype(np.uint32)<<8)|(c[:,:,6].astype(np.uint32)<<16)|(c[:,:,7].astype(np.uint32)<<24)
    for k in range(16): idx[:,:,k]=(bits>>(2*k))&3
    out=np.zeros((h,w,3),np.uint8)
    for k in range(16):
        px=pal[np.arange(bh)[:,None],np.arange(bw)[None,:],idx[:,:,k]]
        out[(k//4)::4,(k%4)::4]=px.astype(np.uint8)
    return out
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
            if w%4 or h%4: bpb=None
            else:
                c1=chain(w,h,8)
                bpb=8 if abs(body-c1)<=16 else 16 if abs(body-2*c1)<=32 else None
            if bpb is None:
                tex.setdefault(('?',w,h,sz),[]).append((os.path.relpath(p,root),nm));continue
            l0=w*h*bpb//16
            data=d[off+128:off+128+l0]
            k=(w,h,gpu_hash(data),bpb)
            if k not in tex: tex[k]=dict(w=w,h=h,bpb=bpb,rgb=dxt_rgb(data,w,h,bpb),src=[])
            tex[k]['src'].append((os.path.relpath(p,root).replace(chr(92),'/'),nm))
    return tex
if __name__=='__main__':
    t=scan('C:/TS2-XBE/data')
    unk={k:v for k,v in t.items() if k[0]=='?'}
    good={k:v for k,v in t.items() if k[0]!='?'}
    print('unique',len(good),'unknown-format groups',len(unk),sum(len(v) for v in unk.values()))
    for k,v in list(unk.items())[:10]:print(k,v[:2])
    pickle.dump(good,open('xbtex.pkl','wb'))
