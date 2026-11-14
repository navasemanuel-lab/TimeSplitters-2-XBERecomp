import pickle,struct,re,numpy as np
M=(1<<64)-1
P1,P2,P3,P4,P5=11400714785074694791,14029467366897019727,1609587929392839161,9650029242287828579,2870177450012600261
def rotl(x,r):return ((x<<r)|(x>>(64-r)))&M
def rnd(a,i):return (rotl((a+i*P2)&M,31)*P1)&M
def mrg(a,v):
    v=rnd(0,v);a^=v;return (a*P1+P4)&M
def xxh64(b,seed=0):
    n=len(b);i=0
    if n>=32:
        v=[(seed+P1+P2)&M,(seed+P2)&M,seed,(seed-P1)&M]
        while i+32<=n:
            for k in range(4):
                v[k]=rnd(v[k],struct.unpack_from('<Q',b,i)[0]);i+=8
        h=(rotl(v[0],1)+rotl(v[1],7)+rotl(v[2],12)+rotl(v[3],18))&M
        for k in range(4):h=mrg(h,v[k])
    else:h=(seed+P5)&M
    h=(h+n)&M
    while i+8<=n:
        k=rnd(0,struct.unpack_from('<Q',b,i)[0]);h^=k;h=(rotl(h,27)*P1+P4)&M;i+=8
    if i+4<=n:
        h^=(struct.unpack_from('<I',b,i)[0]*P1)&M;h=(rotl(h,23)*P2+P3)&M;i+=4
    while i<n:
        h^=(b[i]*P5)&M;h=(rotl(h,11)*P1)&M;i+=1
    h^=h>>33;h=(h*P2)&M;h^=h>>29;h=(h*P3)&M;h^=h>>32
    return h
assert xxh64(b'')==0xEF46DB3751D8E999
def dxt_to_cmpr(data,w,h):
    bw,bh=w//4,h//4
    a=np.frombuffer(data[:bw*bh*8],dtype=np.uint8).reshape(bh,bw,8)
    out=bytearray()
    for ty in range(0,bh,2):
        for tx in range(0,bw,2):
            for dy in range(2):
                for dx in range(2):
                    y,x=ty+dy,tx+dx
                    if y>=bh or x>=bw: out+=bytes(8);continue
                    blk=a[y,x]
                    out+=bytes([blk[1],blk[0],blk[3],blk[2]])
                    for r in range(4):
                        bt=blk[4+r];v=0
                        for p in range(4):v|=((bt>>(2*p))&3)<<(2*(3-p))
                        out.append(v)
    return bytes(out)
