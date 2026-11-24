#!/usr/bin/env python3
"""Inspect original Vulkan RGBA captures; never flatten or edit the images."""
import argparse
import json
from pathlib import Path
from PIL import Image


def inspect(path):
    image = Image.open(path).convert('RGBA')
    # Includes cloud silhouettes and clear sky, excludes window edges/UI.
    pixels = image.crop((20, 20, 940, 400)).tobytes()
    alpha = pixels[3::4]
    bias = sum((255-pixels[i+c])*(1-pixels[i+3]/255)
               for i in range(0,len(pixels),4) for c in range(3))/(len(alpha)*3)
    return dict(resolution=list(image.size), sky_alpha_min=min(alpha), sky_alpha_max=max(alpha),
                sky_nonopaque_pixels=sum(a<255 for a in alpha), white_matte_bias_8bit=bias)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--root',default='build/cloud-halo')
    root = Path(parser.parse_args().root).resolve()
    report = dict(scope='actual frozen Taipei Vulkan captures; same cameras, scene time 0 and UTC; stable render only, excludes IO/physics stepping',
                  alpha_contract='source alpha + background alpha * (1-source alpha); opaque sky stays opaque; reflection alpha remains cloud opacity',
                  views={})
    for mode in ('', '-direct'):
        before, after = [json.loads((root/(side+mode)/'cloud-halo.json').read_text()) for side in ('before','after')]
        assert before['utc']==after['utc'] and before['source']==after['source']
        report['source']=after['source']
        for a,b in zip(before['views'],after['views']):
            for key in ('name','hours_utc','eye','focus'):
                assert a[key]==b[key]
            name=b['name']
            paths=[root/(side+mode)/(name+'.png') for side in ('before','after')]
            old,new=[inspect(p) for p in paths]
            assert old['resolution']==new['resolution']==[960,540]
            assert old['sky_nonopaque_pixels']>0, 'baseline failed to reproduce the alpha problem'
            assert new['sky_alpha_min']==new['sky_alpha_max']==255, 'cloud/fog destroyed opaque sky coverage'
            assert new['white_matte_bias_8bit']==0
            report['views'][name+mode]=dict(before=old,after=new,before_png=str(paths[0]),after_png=str(paths[1]),
                before_render_median_ms=a['render_median_ms'],after_render_median_ms=b['render_median_ms'])
    (root/'comparison.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n')
    print(json.dumps(report['views']['sky-day'],indent=2))


if __name__=='__main__':
    main()
