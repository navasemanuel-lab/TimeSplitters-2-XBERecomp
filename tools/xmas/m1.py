import pickle,os,re,collections,numpy as np
from PIL import Image
T=pickle.load(open('xbtex.pkl','rb'))
bysize=collections.defaultdict(list)
for k,v in T.items(): bysize[(v['w'],v['h'])].append(k)
stack={}
for s,ks in bysize.items():
    stack[s]=(ks,np.stack([T[k]['rgb'] for k in ks]).astype(np.int16))
R='C:/Users/navas/Pictures/TSXmasUSA/GTSE4F'
rx=re.compile(r'tex1_(\d+)x(\d+)_m?_?([0-9a-f]{16})_(\d+)\.png')
res=[]
for r,ds,fs in os.walk(R):
    for f in fs:
        m=rx.match(f)
        if not m: print('skip',f);continue
        w,h=int(m[1]),int(m[2])
        im=np.asarray(Image.open(os.path.join(r,f)).convert('RGBA')).astype(np.int16)
        if im.shape[:2]!=(h,w): res.append((r,f,'sizeMismatch',im.shape));continue
        if (w,h) not in stack: res.append((r,f,None,None));continue
        ks,st=stack[(w,h)]
        mae=np.abs(st-im[None,:,:,:3]).mean((1,2,3))
        o=np.argsort(mae)[:2]
        res.append((r,f,ks[o[0]],mae[o[0]],mae[o[1]] if len(o)>1 else 999))
pickle.dump(res,open('m1.pkl','wb'))
ap=[x for x in res if 'Corel' in x[0] and x[2] is not None and not isinstance(x[2],str)]
mn=[x for x in res if 'Corel' not in x[0] and x[2] is not None and not isinstance(x[2],str)]
for nm,l in (('autopreserve',ap),('main',mn)):
    a=np.array([x[3] for x in l]);print(nm,len(l),'MAE pct',np.percentile(a,[5,25,50,75,95]).round(1),'<3:',(a<3).sum(),'<8:',(a<8).sum())
print('nosize',sum(1 for x in res if x[2] is None),'mismatch',sum(1 for x in res if x[2]=='sizeMismatch'))
