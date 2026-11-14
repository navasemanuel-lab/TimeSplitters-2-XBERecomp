import pickle,os,re,collections,numpy as np
from PIL import Image
T=pickle.load(open('xbtex.pkl','rb'));H=pickle.load(open('gchash.pkl','rb'));res=pickle.load(open('m1.pkl','rb'))
rx=re.compile(r'tex1_(\d+)x(\d+)_m?_?([0-9a-f]{16})_(\d+)(_mip\d+)?\.png')
R='C:/Users/navas/Pictures/TSXmasUSA/GTSE4F'
vis={ (x[0],x[1]):x for x in res if len(x)==5}   # (dir,file)->(dir,file,key,mae,mae2)
cat=collections.Counter();out={}
for r,ds,fs in os.walk(R):
    if 'Corel' in r: continue
    for f in fs:
        m=rx.match(f)
        if m[5]: cat['mipfile']+=1;continue
        w,h=int(m[1]),int(m[2]);key=(w,h,int(m[3],16))
        p=os.path.join(r,f)
        if key in H: out[p]=('exact',H[key]);cat['exact']+=1;continue
        apk=os.path.join(r,'Corel Auto-Preserve',f)
        v=vis.get((os.path.join(r,'Corel Auto-Preserve'),f)) 
        if v is not None and v[3]<8 and v[4]-v[3]>1: out[p]=('ap-visual',[v[2]]);cat['ap-visual']+=1;continue
        if os.path.exists(apk): cat['ap-exists-but-no-match']+=1;out[p]=('none',[]);continue
        v=vis.get((r,f))
        if v is None: cat['unknown-size/dim']+=1;out[p]=('none',[]);continue
        out[p]=('main-visual',[v[2]],v[3],v[4]);cat['main-visual-candidate']+=1
print(cat)
mv=[x for x in out.values() if x[0]=='main-visual']
a=np.array([x[2] for x in mv]);b=np.array([x[3] for x in mv])
print('main-visual MAE pct',np.percentile(a,[10,50,90]).round(1),'margin pct',np.percentile(b-a,[10,50,90]).round(1))
print('confident (mae<12 & margin>3):',((a<12)&(b-a>3)).sum())
pickle.dump(out,open('plan.pkl','wb'))
