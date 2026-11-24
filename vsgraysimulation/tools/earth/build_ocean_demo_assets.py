#!/usr/bin/env python3
"""Rebuild the authored ocean validation assets (not measured bathymetry).
Requires Pillow. Writes OBJ, MTL and terrain textures.
"""
from pathlib import Path
import math,random,json
from PIL import Image
root=Path(__file__).resolve().parents[2]
p=root/'models/OceanDemo'
p.mkdir(parents=True,exist_ok=True)
mtl='newmtl hull\nKd 0.15 0.24 0.31\nnewmtl deck\nKd 0.76 0.72 0.61\nnewmtl cabin\nKd 0.86 0.89 0.86\nnewmtl glass\nKd 0.07 0.22 0.28\nnewmtl orange\nKd 0.91 0.28 0.035\nnewmtl sand\nKd 0.75 0.69 0.47\nmap_Kd sand.png\n'
(p/'demo.mtl').write_text(mtl)
class Obj:
 def __init__(self):self.lines=['mtllib demo.mtl'];self.count=0;self.coordinates={}
 def v(self,x,y,z):self.lines.append(f'v {x:.7f} {y:.7f} {z:.7f}');self.count+=1;self.coordinates[self.count]=(x,y,z);return self.count
 def face(self,ids,material):self.lines.append('usemtl '+material);self.lines.append('f '+' '.join(map(str,ids)))
 def box(self,low,high,material,front=None):
  ids=[self.v(*(high[a] if i&(1<<a) else low[a] for a in range(3))) for i in range(8)]
  for i,f in enumerate([(0,4,6,2),(1,3,7,5),(0,1,5,4),(2,6,7,3),(0,2,3,1),(4,5,7,6)]):self.face([ids[k] for k in f],front if front and i==4 else material)
 def save(self,name): (p/name).write_text('\n'.join(self.lines)+'\n')
o=Obj();rings=[]
for z,b in [(-6,.12),(-5,1.05),(-3,1.8),(0,2),(3,1.9),(5,1.25),(6,.3)]:rings.append([o.v(x,y,z) for x,y in [(0,-1.15),(-b*.72,-.75),(-b,.65),(b,.65),(b*.72,-.75)]])
for i in range(len(rings)-1):
 for j in range(5):o.face([rings[i][j],rings[i+1][j],rings[i+1][(j+1)%5],rings[i][(j+1)%5]],'deck' if j==2 else 'hull')
o.face(rings[0],'hull');o.face(rings[-1][::-1],'hull');o.box((-.9,.65,-1.6),(.9,2.05,1.8),'cabin','glass');o.box((-.76,1.28,-1.65),(.76,1.85,-1.6),'glass');o.box((-.96,1.18,-.9),(-.9,1.83,1.3),'glass');o.box((.9,1.18,-.9),(.96,1.83,1.3),'glass');o.box((-.98,2.05,-1.72),(.98,2.2,1.95),'cabin');o.box((-.12,2.2,.5),(.12,3.3,.7),'hull');o.box((-.35,.7,-4.1),(.35,1.15,-3.1),'orange');o.save('test-vessel.obj')
s=Obj();rows=[]
for j in range(17):
 phi=-math.pi/2+math.pi*(j+.001)/(16.002);rows.append([s.v(math.cos(phi)*math.cos(i*2*math.pi/32),math.cos(phi)*math.sin(i*2*math.pi/32)*.85,4*math.sin(phi)) for i in range(32)])
for j in range(16):
 for i in range(32):s.face([rows[j][i],rows[j][(i+1)%32],rows[j+1][(i+1)%32],rows[j+1][i]],'orange' if j<5 else 'hull')
s.face(rows[0][::-1],'hull');s.face(rows[-1],'hull');s.box((-.15,.5,1.4),(.15,1.65,2.8),'orange');s.box((-1.8,-.1,2.5),(1.8,.1,3.15),'hull');s.save('test-submersible.obj')
b=Obj();grid=[];N=49
for z in range(N):
 row=[]
 for x in range(N):
  px=-64+128*x/(N-1);pz=-64+128*z/(N-1);h=-14+.65*math.sin(px*.095)+.4*math.cos(pz*.13)+.2*math.sin((px+pz)*.25);idx=b.v(px,h,pz);b.lines.append(f'vt {x/(N-1)} {z/(N-1)}');row.append(idx)
 grid.append(row)
for z in range(N-1):
 for x in range(N-1):
  ids=[grid[z][x],grid[z+1][x],grid[z+1][x+1],grid[z][x+1]];b.lines.append('usemtl sand');b.lines.append('f '+' '.join(f'{v}/{v}' for v in ids))
# Close the assumed test bed, retaining the real topography source separately.
bottom=[b.v(-64,-18,-64),b.v(64,-18,-64),b.v(64,-18,64),b.v(-64,-18,64)];b.face(bottom,'sand')
edges=[grid[0], [r[-1] for r in grid],grid[-1][::-1],[r[0] for r in grid[::-1]]]
for i,edge in enumerate(edges):
 for j in range(len(edge)-1):
  a,c=b.coordinates[edge[j]],b.coordinates[edge[j+1]];lo=b.v(a[0],-18,a[2]);hi=b.v(c[0],-18,c[2]);b.face([edge[j],edge[j+1],hi,lo],'sand')
b.save('assumed-seabed.obj')
rng=random.Random(987);im=Image.new('RGB',(512,512));pix=im.load()
for z in range(512):
 for x in range(512):
  noise=rng.gauss(0,3);ripple=4*math.sin(x*.32+1.3*math.sin(z*.035));pix[x,z]=tuple(max(0,min(255,int(c+noise+ripple))) for c in (181,170,135))
im.save(p/'sand.png')
scene={'format':'voxel-scene-v1','fixed_step_seconds':.02,'utc_origin':{'year':2026,'month':9,'day':10,'hours_utc':4},'geo_anchor':{'lat':25.22,'lon':121.8,'alt':19.8389225006104},'ocean':{'wave_height_m':.65,'wave_length_m':24,'swell_height_m':.25,'swell_length_m':80,'wind_speed_mps':6,'wind_azimuth_deg':70,'bottom_m':-40,'half_extent_m':4096,'cell_size_m':.5,'provenance':{'sea_level':'local mean sea surface anchored to the new Taipei DSM sample; no measured tide','bed':'synthetic validation bed; NOT measured Taipei bathymetry','body_density':'assumed bulk density including enclosed volume; no flooding compartments'}},'objects':[
 {'id':'assumed-bed','role':'terrain','source':{'type':'mesh','path':'../../models/OceanDemo/assumed-seabed.obj','max_dimension':160,'up_axis':'y','fill_interior':True},'physics':{'motion':'static'},'material':{'density_kg_m3':2200,'color_rgba':[1,1,1,1]}},
 {'id':'vessel','role':'target','position_m':[0,1,0],'velocity_mps':[-.3,0,-1.5],'rotation_deg':[2,12,3],'source':{'type':'mesh','path':'../../models/OceanDemo/test-vessel.obj','max_dimension':128,'up_axis':'y','fill_interior':True},'physics':{'motion':'dynamic','fracture':True,'support':'none','friction':.65},'material':{'density_kg_m3':400,'temperature_k':294,'color_rgba':[1,1,1,1]}},
 {'id':'submersible','role':'target','position_m':[-5,-5,-3],'rotation_deg':[0,-15,0],'source':{'type':'mesh','path':'../../models/OceanDemo/test-submersible.obj','max_dimension':96,'up_axis':'y','fill_interior':True},'physics':{'motion':'dynamic','fracture':True,'support':'none'},'material':{'density_kg_m3':1025,'temperature_k':293,'color_rgba':[1,1,1,1]}}
]}
(root/'config/earth/taipei-ocean-reference-boat.vscene.json').write_text(json.dumps(scene,ensure_ascii=False,indent=2)+'\n')
print('Created procedural validation vessels/bed and explicit ocean scene.')
