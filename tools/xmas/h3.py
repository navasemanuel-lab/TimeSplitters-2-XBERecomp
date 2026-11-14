import pickle,struct,re,os,collections
exec(open('h.py').read())
T=pickle.load(open('xbtex.pkl','rb'))
cache={}
def getraw(pk,nm,w,h):
    if pk not in cache: cache[pk]=open('C:/TS2-XBE/data/'+pk,'rb').read()
    d=cache[pk];do,n,no,ns=struct.unpack_from('<4I',d,4);names=d[no:no+ns]
    return d
H={}  # gc hash -> list of xbox keys
idx={}
for pk in {v['src'][0][0] for v in T.values()}:
    d=open('C:/TS2-XBE/data/'+pk,'rb').read();do,n,no,ns=struct.unpack_from('<4I',d,4);names=d[no:no+ns]
    idx[pk]=(d,{names[a:names.index(b'\0',a)].decode():(sz,off) for a,sz,off in (struct.unpack_from('<III',d,do+i*12) for i in range(n))})
cnt=0
for k,v in T.items():
    if v['bpb']!=8 or v['w']%4 or v['h']%4: continue
    pk,nm=v['src'][0];d,e=idx[pk];sz,off=e[nm]
    l0=d[off+128:off+128+v['w']*v['h']//2]
    h=xxh64(dxt_to_cmpr(l0,v['w'],v['h']))
    H.setdefault((v['w'],v['h'],h),[]).append(k)
    cnt+=1
print('hashed',cnt)
pickle.dump(H,open('gchash.pkl','wb'))
rx=re.compile(r'tex1_(\d+)x(\d+)_m?_?([0-9a-f]{16})_(\d+)(_mip\d+)?\.png')
R='C:/Users/navas/Pictures/TSXmasUSA/GTSE4F'
hit=miss=0;stat=collections.Counter();missing=[]
for r,ds,fs in os.walk(R):
    for f in fs:
        m=rx.match(f)
        ap='Corel' in r
        key=(int(m[1]),int(m[2]),int(m[3],16))
        if key in H: stat['ap' if ap else 'main','hit']+=1
        else: stat['ap' if ap else 'main','miss']+=1; missing.append((ap,os.path.join(r,f)))
print(stat)
pickle.dump(missing,open('missing.pkl','wb'))
