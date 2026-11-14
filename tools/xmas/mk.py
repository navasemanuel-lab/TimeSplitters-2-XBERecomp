import pickle,os,re,struct,collections,numpy as np
from PIL import Image
T=pickle.load(open('xbtex.pkl','rb'));plan=pickle.load(open('plan.pkl','rb'))
out_dir='C:/TS2-XBE/recomp/out/build/release/resourcepacks/xmas'
os.makedirs(out_dir,exist_ok=True)
chosen={}  # xbox key -> (rank, path)
conf=0
for p,v in plan.items():
    if v[0] not in('exact','ap-visual','ncc'): continue
    rank={'exact':0,'ap-visual':1,'ncc':2}[v[0]]
    for k in v[1][:1] if v[0] in('ap-visual','ncc') else v[1]:
        if k in chosen:
            conf+=1
            if chosen[k][0]<=rank: continue
        chosen[k]=(rank,p)
print('textures',len(chosen),'conflicts',conf)
ents=[];blobs=[]
off=0
for k,(rank,p) in sorted(chosen.items(),key=lambda x:x[1][1]):
    v=T[k]
    im=Image.open(p).convert('RGBA');a=np.asarray(im)
    bgra=a[:,:,[2,1,0,3]].tobytes()
    ents.append((v['w'],v['h'],v['w']*v['h']//2,k[2],im.width,im.height,off,len(bgra)))
    blobs.append(bgra);off+=len(bgra)
head=struct.pack('<4sI',b'XMS1',len(ents))
tab=b''.join(struct.pack('<IIIQIIQ',*e[:6][:3],e[3],e[4],e[5],e[6]) for e in ents)
hdr_size=len(head)+len(tab)
tab=b''.join(struct.pack('<IIIQIIQ',e[0],e[1],e[2],e[3],e[4],e[5],e[6]+hdr_size) for e in ents)
open(out_dir+'/xmas.bin','wb').write(head+tab+b''.join(blobs))
print('bytes',hdr_size+off, 'entry size',struct.calcsize('<IIIQIIQ'))
c=collections.Counter(v[0] for v in plan.values());print(c)
