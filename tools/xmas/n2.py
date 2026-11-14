"""Matches the Christmas pictures still without an Xbox texture to one of the same size by the picture (normalised correlation of the
brightness), now with the Xbox's uncompressed ones too; run in the folder with xbtex.pkl and plan.pkl: writes ncc2.pkl."""
import pickle,os,re,collections,sys,numpy as np
from PIL import Image
plan=pickle.load(open('plan.pkl','rb')); T=pickle.load(open('xbtex.pkl','rb'))
rx=re.compile(r'tex1_(\d+)x(\d+)_m?_?([0-9a-f]{16})_(\d+)(_mip\d+)?\.png')
bysize=collections.defaultdict(list)
for k,v in T.items(): bysize[(v['w'],v['h'])].append(k)
def lum(a): a=a.astype(np.float32); return a[...,0]*.299+a[...,1]*.587+a[...,2]*.114
stack={}
def getstack(s):
    if s not in stack:
        ks=bysize[s]; L=np.stack([lum(T[k]['rgb']) for k in ks]).reshape(len(ks),-1); L=L-L.mean(1,keepdims=True); n=np.linalg.norm(L,axis=1,keepdims=True)+1e-6
        stack[s]=(ks,L/n)
    return stack[s]
res={}
for p,v in plan.items():
    if v[0] not in('main-visual','none'): continue
    m=rx.match(os.path.basename(p)); w,h=int(m[1]),int(m[2])
    if (w,h) not in bysize: res[p]=None; continue
    im=np.asarray(Image.open(p).convert('RGBA'))
    if im.shape[:2]!=(h,w): res[p]=None; continue
    ks,S=getstack((w,h)); x=lum(im).reshape(-1); x=x-x.mean(); x/=np.linalg.norm(x)+1e-6
    c=S@x; o=np.argsort(-c)[:2]
    res[p]=(ks[o[0]],float(c[o[0]]),float(c[o[1]]) if len(o)>1 else -1.0)
pickle.dump(res,open('ncc2.pkl','wb'))
r=[v for v in res.values() if v]
a=np.array([x[1] for x in r]);b=np.array([x[2] for x in r])
print(len(res),len(r))
for c0,mg in ((0.95,0.05),(0.9,0.05),(0.85,0.1),(0.8,0.1)): print(c0,mg,((a>c0)&(a-b>mg)).sum())
for p,v in sorted(res.items()):
    if 'Menu' in p and 'Corel' not in p: print(os.path.basename(p), None if not v else (v[0][3],T[v[0]]['src'][0][1].split('/')[-1],round(v[1],3),round(v[2],3)))
