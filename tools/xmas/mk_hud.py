"""Builds hd_hud.bin (XMS1, as xmas.bin) from the HD fonts/HUD/crosshairs pack (bekoha.github.io/ts2, PS2 pictures at four times
the size): each picture named by the Xbox texture it is the same as (the same design and layout, found by comparing their
alpha and by eye); run in the folder with xbalpha.pkl (xb3.py): python -I mk_hud.py <pack textures/GTS folder> <out.bin>"""
import os,pickle,struct,sys,glob
import numpy as np
from PIL import Image
pack=sys.argv[1];out=sys.argv[2]
X=pickle.load(open('xbalpha.pkl','rb'))
MAP={  # picture (name start) -> the Xbox texture's file name
 'Crosshairs/tex1_32x32_m_2de7':'crosshair_circle','Crosshairs/tex1_32x32_m_8c9b':'crosshair_arrow','Crosshairs/tex1_32x32_m_cc7f':'crosshair_triangle',
 'Crosshairs/tex1_64x32_m_a8d2':'lockoninner','Crosshairs/tex1_64x64_m_4fb0':'homingcrosshair','Crosshairs/tex1_64x64_m_7733':'crosshair_shaded',
 'Crosshairs/tex1_64x64_m_9f4d':'crosshair_olde','Crosshairs/tex1_64x64_m_a5b2':'crosshair_laser',
 'Fonts/tex1_128x256_m_07b2':'booklight','Fonts/tex1_128x256_m_091b':'book','Fonts/tex1_256x256_m_68a0':'impact',
 'HUD/tex1_256x128_m_00e6':'arc_outline','HUD/tex1_256x128_m_8fa0':'arc_bar','HUD/tex1_64x64_m_d77b':'arc_glow','HUD/tex1_16x8_d73e':'radarspot',
 'HUD/tex1_32x32_m_94ed':'pagebar','HUD/tex1_64x64_m_16f3':'hudradarpulse','HUD/tex1_64x64_m_85a7':'hudradarcentre',
 'HUD/tex1_64x64_m_ae8c':'hudradar','HUD/tex1_64x64_m_4a7f':'hudback',
 # the level pictures of the level select (story and arcade; colour 128 and grey 256), matched by eye and by correlation
 'Levels/tex1_128x128_m_013e':'lvim_pyramid1d','Levels/tex1_128x128_m_1623':'lvim_western1d','Levels/tex1_128x128_m_1e27':'lvim_dam1d',
 'Levels/tex1_128x128_m_33ee':'lvim_notredame1d','Levels/tex1_128x128_m_3720':'lvim_atomsmasher1d','Levels/tex1_128x128_m_4d2e':'lvim_spacestation1d',
 'Levels/tex1_128x128_m_5be1':'lvim_gangster1d','Levels/tex1_128x128_m_79d0':'lvim_rfactory2','Levels/tex1_128x128_m_b654':'lvim_planet1d',
 'Levels/tex1_128x128_m_ed88':'lvim_neotokyo1d',
 'Levels/tex1_256x256_m_1031':'lvim_gangster_g','Levels/tex1_256x256_m_363e':'lvim_pyramid_g','Levels/tex1_256x256_m_8622':'lvim_western_g',
 'Levels/tex1_256x256_m_88bd':'lvim_planet_g','Levels/tex1_256x256_m_ac37':'lvim_notredame_g','Levels/tex1_256x256_m_b264':'lvim_spacestation_g',
 'Levels/tex1_256x256_m_ccf5':'lvim_atomsmasher_g','Levels/tex1_256x256_m_cd68':'lvim_neotokyo_g','Levels/tex1_256x256_m_e876':'lvim_dam_g',
 'Levels/tex1_256x256_m_e977':'lvim_rfactory_g2',
}
def keys(name):
    return [k for k,v in X.items() if any(s[1].split('/')[-1]==name+'.xbt' for s in v[3])]
ents=[];blobs=[];off=0
ITEMS=[]
for pre,name in MAP.items():
    f=[p for p in glob.glob(pack+'/'+pre.split('/')[0]+'/*.png') if os.path.basename(p).startswith(pre.split('/')[1])]
    assert len(f)==1,(pre,f);ITEMS.append((f[0],name))
# the ammunition icons drawn here (mk_ammo.py)
for p in sorted(glob.glob(os.path.join(os.path.dirname(os.path.abspath(__file__)),'..','hd_ammo','*.png'))):
    ITEMS.append((p,os.path.basename(p)[:-4]))
for fpath,name in ITEMS:
    f=[fpath]
    im=Image.open(f[0]).convert('RGBA');bgra=np.asarray(im)[:,:,[2,1,0,3]].tobytes()
    ks=keys(name);assert ks,name
    for k in ks:
        w,h=k[0],k[1];n=w*h*4 if k[3]=='argb' else w*h*k[3]//16
        ents.append((w,h,n,k[2],im.width,im.height,off,name))
    blobs.append(bgra);off0=off;
    # entries of this picture share its pixels
    for i in range(len(ks)): ents[-1-i]=ents[-1-i][:6]+(off,)+ents[-1-i][7:]
    off+=len(bgra)
    print(name,len(ks),'xbox textures',im.size)
head=struct.pack('<4sI',b'XMS1',len(ents))
hs=len(head)+len(ents)*struct.calcsize('<IIIQIIQ')
tab=b''.join(struct.pack('<IIIQIIQ',e[0],e[1],e[2],e[3],e[4],e[5],e[6]+hs) for e in ents)
open(out,'wb').write(head+tab+b''.join(blobs));print('entries',len(ents),'bytes',hs+off)
