#!/usr/bin/env python3
"""Audit native weather artifacts. Never modify the captured images."""
import argparse
import json
import math
import statistics
from pathlib import Path
import numpy as np
from PIL import Image

parser=argparse.ArgumentParser()
parser.add_argument('directory',nargs='?',default='build/scene-weather/native')
root=Path(parser.parse_args().directory).resolve()
report=json.loads((root/'scene-weather.json').read_text())
assert len(report['views'])==11 and report['physical_tick']==0
assert report['disable_restored_legacy_environment']
assert report['source']['dem_spacing_m']<35 and report['source']['imagery_spacing_m']<=10

def native(file):
    im=Image.open(file).convert('RGBA')
    assert im.size==(960,540) and im.getextrema()[3]==(255,255),str(file)
    return np.asarray(im)[:,:,:3].astype(np.float32)

images=[]
for view in report['views']:
    assert view['eye']==[100,16,-145] and view['focus']==[0,90,-850]
    images.append(native(root/(view['name']+'.png')))
    if 'coast_view' in view:native(root/view['coast_view']['file'])
    if 'land_view' in view:native(root/view['land_view']['file'])
    print(f"{view['name']}: median={view['render_median_ms']:.2f} P95={view['render_p95_ms']:.2f} ms")
for i in range(1,7):
    assert np.mean(np.abs(images[i]-images[0]))>1.,'weather views are indistinguishable'
for kind in ('rain','snow'):
    assert report['precipitation_visible_pixels'][kind]>20
    off=native(root/f'{kind}-utc4.000000-no-particles.png')
    on=native(root/f'{kind}-utc4.000000.png')
    assert np.sum(np.max(np.abs(on-off),axis=2)>8)==report['precipitation_visible_pixels'][kind]
    frames=[native(root/kind/f'frame-{i:03}.png') for i in range(48)]
    delta=float(np.mean(np.abs(frames[-1]-frames[0])))
    assert delta>.01,'wind/precipitation animation frozen'
    report['animation'][0 if kind=='rain' else 1]['first_last_mean_rgb_delta_8bit']=delta
transitions=report['transition_frames'];assert len(transitions)==65
for i,frame in enumerate(transitions):
    assert frame['seconds']==i/8.
    native(root/'transition'/f'frame-{i:03}.png')
assert transitions[0]['rain']==0 and transitions[0]['snow']==0
assert abs(transitions[32]['rain']-.15)<1e-6 and transitions[32]['snow']==0
assert transitions[-1]['rain']==0 and abs(transitions[-1]['snow']-.25)<1e-6
times=sorted(f['render_ms'] for f in transitions)
report['transition_render_including_profile_updates']={'median_ms':statistics.median(times),'p95_ms':times[math.ceil(.95*len(times))-1]}
print('continuous GPU transition:',report['transition_render_including_profile_updates'])
report['timing_scope']='native stable render/update/submit/GPU wait, excludes capture IO and physical stepping'
(root/'verified-summary.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n')
print('PASS: native weather states, visible precipitation, windy animation, opacity and source provenance')
