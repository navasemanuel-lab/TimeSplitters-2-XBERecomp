"""Builds resourcepacks/xmas/xmas.bin: the Christmas Edition pictures each matched to the Xbox texture it replaces. Run in the folder
with xbtex.pkl, plan.pkl and ncc2.pkl (after xb.py, xb2.py, m1.py, h3.py, c1.py, n1.py, n2.py): what the first scripts matched by hash and
by picture, the level photos and the menu's banner pieces named by hand (the same scene, a different picture), and the pictures n2.py
found with a clear margin. A texture the game hashes over n bytes: a console format's first level (w*h*bpb/16), or 32-bit pictures' w*h*4."""
import pickle,os,struct,collections,sys
import numpy as np
from PIL import Image
T=pickle.load(open('xbtex.pkl','rb'));plan=pickle.load(open('plan.pkl','rb'));ncc2=pickle.load(open('ncc2.pkl','rb'))
MENU='C:/Users/navas/Pictures/TSXmasUSA/GTSE4F/Menu/'
def png(prefix):
    for f in os.listdir(MENU):
        if f.startswith(prefix): return MENU+f
    raise KeyError(prefix)
def xbox(name):  # every Xbox texture with this file name
    return [k for k,v in T.items() if any(s[1].split('/')[-1]==name for s in v['src'])]
HAND={  # PS2 picture -> Xbox file names (the levels' photos: the arcade's colour ones and the grey ones the story's)
 'tex1_128x128_m_013e': ['lvim_pyramid1d.xbt'], 'tex1_128x128_m_1623': ['lvim_western1d.xbt'], 'tex1_128x128_m_1e27': ['lvim_dam1d.xbt'],
 'tex1_128x128_m_33ee': ['lvim_notredame1d.xbt'], 'tex1_128x128_m_3720': ['lvim_rfactory1d.xbt'], 'tex1_128x128_m_5be1': ['lvim_gangster1d.xbt'],
 'tex1_128x128_m_79d0': ['lvim_atomsmasher1d.xbt'], 'tex1_128x128_m_b654': ['lvim_planet1d.xbt'], 'tex1_128x128_m_b698': ['lvim_mexican1d.xbt'],
 'tex1_128x128_m_ed88': ['lvim_neotokyo1d.xbt'],
 'tex1_256x256_m_1031': ['lvim_gangster_g.xbt'], 'tex1_256x256_m_363e': ['lvim_pyramid_g.xbt'], 'tex1_256x256_m_8622': ['lvim_western_g.xbt'],
 'tex1_256x256_m_88bd': ['lvim_planet_g.xbt'], 'tex1_256x256_m_ac37': ['lvim_notredame_g.xbt'], 'tex1_256x256_m_ccf5': ['lvim_rfactory_g2.xbt'],
 'tex1_256x256_m_cd68': ['lvim_neotokyo_g.xbt'], 'tex1_256x256_m_e876': ['lvim_dam_g.xbt'], 'tex1_256x256_m_e977': ['lvim_atomsmasher_g.xbt'],
 'tex1_512x64_m_8423': ['ts2smallbanner3.xbt'], 'tex1_512x64_m_a244': ['ts2smallbanner2.xbt'],
 'tex1_512x128_m_05e4': ['ts2banner3.xbt'], 'tex1_512x128_m_280f': ['ts2banner1.xbt'], 'tex1_512x128_m_9410': ['ts2banner2.xbt'],
 'tex1_512x128_m_8bd8': ['ts2banner4.xbt'],
 'tex1_512x256_m_b862': ['ts2newlogo.xbt'], 'tex1_512x256_m_1236': ['ts2glow.xbt'],
}
chosen={}  # xbox key -> (rank, path)
def put(k,rank,p):
    if k in chosen and chosen[k][0]<=rank: return
    chosen[k]=(rank,p)
for p,v in plan.items():
    if v[0] not in('exact','ap-visual','ncc'): continue
    rank={'exact':0,'ap-visual':1,'ncc':2}[v[0]]
    for k in (v[1][:1] if v[0] in('ap-visual','ncc') else v[1]): put(k,rank,p)
# the Christmas sky is not whole: its pieces are other pictures than the console's tiles (the console's sky is cut into 128x64 tiles, the
# Christmas one is five 512x256 pictures that have no console texture), so a single tile of Planet X's sky replaced shows as a cut-out
# picture floating in the console's sky: left as the console's (l_50_AR 1254, the red planet)
SKY_SKIP=set()
for k in [k for k in chosen if any(s in SKY_SKIP for s in T[k]['src'])]: del chosen[k]
n_plan=len(chosen)
for p,r in ncc2.items():
    if r and 'Menu' not in p and r[1]>0.9 and r[1]-r[2]>0.05: put(r[0],3,p)
n_auto=len(chosen)
for pre,names in HAND.items():
    for nm in names:
        ks=xbox(nm); assert ks,nm
        for k in ks: chosen[k]=(-1,png(pre))

# Planet X's sky (story l_40_ST, ob/skies/skyufopia.xbr): the model is a square prism of six 128x64 ARGB tiles (four walls 1369 1370 1371
# 1372 and the two halves of the roof/floor 1254 1270) with cloud layers (1373-1376), Saturn's pieces (1377-1380) and a ring (1381) over
# them. The Christmas pack's pictures replace the six tiles (the red planet is 1254 by hash) and its fully transparent pictures hide the
# overlays. The five other pictures are not seamless pieces of one panorama, so which wall shows which is only the order chosen here.
PX='C:/Users/navas/Pictures/TSXmasUSA/GTSE4F/PlanetX/'
def pxf(h):
    for f in os.listdir(PX):
        if h in f: return PX+f
    raise KeyError(h)
SKY={'1270':pxf('0878'),'1369':pxf('1894'),'1372':pxf('2878'),'1371':pxf('b103'),'1370':pxf('d532'),'1254':pxf('f5021c6e')}
for i in range(1373,1382): SKY[str(i)]=None
def skykeys(num):
    return [k for k,v in T.items() if any(s[0].endswith('l_40_ST.pak') and s[1].endswith('/%s.xbt'%num) for s in v['src'])]
for num,p in SKY.items():
    ks=skykeys(num); assert ks,num
    for k in ks: chosen[k]=(-2,p)

# Ufopia's sky (arcade l_50_AR, ob/skies/skyrealufopia.xbr): the same prism (roof and floor halves 1254 1270 are the very textures of Planet X's),
# four walls 1256 1257 1264 1265, with the pack's four blue nebula pictures (two pictures, each twice, put on opposite walls)
UF='C:/Users/navas/Pictures/TSXmasUSA/GTSE4F/ufopia/'
def uff(h):
    for f in os.listdir(UF):
        if h in f: return UF+f
    raise KeyError(h)
UFO={'1257':uff('1b85e89'),'1265':uff('579ddd'),'1264':uff('7d711de'),'1256':uff('e558916')}
for num,p in UFO.items():
    ks=[k for k,v in T.items() if any(s[0].endswith('l_50_AR.pak') and s[1].endswith('/%s.xbt'%num) for s in v['src'])]
    assert ks,num
    for k in ks: chosen[k]=(-2,p)
for k in xbox('lvim_ufopia2d.xbt'): chosen[k]=(-1,uff('129c8cc'))
print('textures',len(chosen),'(plan',n_plan,', + pictures',n_auto-n_plan,', by hand',len(chosen)-n_auto,')')
out_dir='C:/TS2-XBE/recomp/out/build/release/resourcepacks/xmas'
os.makedirs(out_dir,exist_ok=True)
ents=[];blobs=[];off=0
for k,(rank,p) in sorted(chosen.items(),key=lambda x:str(x[1][1])+str(x[0][:3])):
    v=T[k]
    im=Image.open(p).convert('RGBA') if p else Image.new('RGBA',(v['w'],v['h']),(0,0,0,0))
    if p and (p.startswith(PX) or p.startswith(UF)) and (im.width&(im.width-1) or im.height&(im.height-1)): im=im.resize((1024,512),Image.LANCZOS)  # the sky pictures that are not a power of two (the tile is 2:1 and stretches them alike)
    if im.size==(512,128): im=im.resize((1024,256),Image.LANCZOS)  # a header with its logo drawn in, as the front end draws Redux's (frame_bars.cpp)
    a=np.asarray(im)
    bgra=a[:,:,[2,1,0,3]].tobytes()
    n=v['w']*v['h']*4 if k[3]=='argb' else v['w']*v['h']*v['bpb']//16
    ents.append((v['w'],v['h'],n,k[2],im.width,im.height,off))
    blobs.append(bgra);off+=len(bgra)
head=struct.pack('<4sI',b'XMS1',len(ents))
hdr_size=len(head)+len(ents)*struct.calcsize('<IIIQIIQ')
tab=b''.join(struct.pack('<IIIQIIQ',e[0],e[1],e[2],e[3],e[4],e[5],e[6]+hdr_size) for e in ents)
open(out_dir+'/xmas.bin','wb').write(head+tab+b''.join(blobs))
print('bytes',hdr_size+off)
