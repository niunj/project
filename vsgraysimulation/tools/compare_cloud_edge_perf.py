#!/usr/bin/env python3
"""Validate native camera/time/density pairs and report rendering/shimmer metrics."""
import argparse
import json
import math
import statistics
from pathlib import Path

import numpy as np
from PIL import Image


def timing(values):
    values = sorted(values)
    return {'frames': len(values), 'median_ms': statistics.median(values),
            'p95_ms': values[math.ceil(.95 * len(values)) - 1]}


def native(path):
    image = Image.open(path).convert('RGBA')
    assert image.getextrema()[3] == (255, 255), f'opaque coverage regressed: {path}'
    return np.asarray(image)[:, :, :3].astype(np.float32)


def water_metrics(frames):
    # Fixed lower-water ROI; no motion registration or image modifications.
    # Wave glints can be real. These are contrast/jitter indicators, not a bug count.
    lumas = []
    bright, dark, tails = [], [], []
    for image in frames:
        h, w = image.shape[:2]
        crop = image[int(h*.65):int(h*.92), int(w*.15):int(w*.85)]
        luma = crop @ np.array([.2126, .7152, .0722], np.float32)
        centre = luma[1:-1, 1:-1]
        neighbours = np.stack([luma[1+y:luma.shape[0]-1+y, 1+x:luma.shape[1]-1+x]
                               for y in (-1, 0, 1) for x in (-1, 0, 1) if x or y])
        delta = centre - np.median(neighbours, axis=0)
        bright.append(float(np.mean(delta > 32)*1e6))
        dark.append(float(np.mean(delta < -24)*1e6))
        tails.append(float(np.quantile(np.abs(delta), .999)))
        lumas.append(luma)
    jitter = [float(np.mean(np.abs(a-b))) for a, b in zip(lumas, lumas[1:])]
    return {'roi_fraction': [.15, .65, .85, .92],
            'bright_isolated_contrasts_per_megapixel': statistics.mean(bright),
            'dark_isolated_contrasts_per_megapixel': statistics.mean(dark),
            'spatial_abs_contrast_p999_8bit': statistics.mean(tails),
            'camera_jitter_mean_abs_luma_8bit': statistics.mean(jitter)}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--root', default='build/cloud-edge-perf')
    root = Path(parser.parse_args().root).resolve()
    read = lambda path: json.loads((root/path).read_text())
    report = {'scope': 'actual Vulkan frames; stable render/update/submit/GPU-wait, excludes IO, loading and physical stepping',
              'wind': {}, 'ocean': {}, 'region': {}, 'water_shimmer': {}}
    old, new = [read(f'wind-{side}/flight-acceptance.json') for side in ('before', 'after')]
    for key in ('source', 'surface', 'utc', 'wind_speed_mps', 'object_count'):
        assert old[key] == new[key], f'wind configuration changed: {key}'
    assert len(old['frames']) == len(new['frames']) == 52
    density_error = 0
    for i, (a, b) in enumerate(zip(old['frames'], new['frames'])):
        for key in ('eye', 'focus', 'seconds', 'phase'):
            assert a[key] == b[key], f'wind route/time changed at {i}'
        density_error = max(density_error, abs(a['eye_condensate']-b['eye_condensate']))
        for side in ('before', 'after'):
            assert native(root/f'wind-{side}'/f'flight-{i:03}.png').shape == (360, 640, 3)
    assert density_error == 0, 'canonical condensate changed'
    report['eye_density_max_error'] = density_error
    report['wind_speed_mps'] = new['wind_speed_mps']
    report['region_surface'] = new['surface']
    report['wind_acceptance'] = {key: new[key] for key in (
        'thin_cloud_frames', 'clear_before_frames', 'clear_after_frames', 'fixed_camera_changed_pixel_fraction')}
    report['wind_overall']={side:timing([f['frame_ms'] for f in doc['frames']]) for side,doc in (('before',old),('after',new))}
    for phase in ('approach', 'wind_hold', 'cross'):
        report['wind'][phase] = {side: timing([f['frame_ms'] for f in doc['frames'] if f['phase'] == phase])
                                for side, doc in (('before', old), ('after', new))}
    old, new = [read(f'ocean-{side}/cloud-halo.json') for side in ('before', 'after')]
    assert old['source'] == new['source'] and old['utc'] == new['utc']
    report['coastal_surface'] = new['source']
    for a, b in zip(old['views'], new['views']):
        for key in ('name', 'hours_utc', 'eye', 'focus'):
            assert a[key] == b[key]
        name = a['name']
        report['ocean'][name] = {side: {'median_ms': view['render_median_ms'],
            'p95_ms': view['render_p95_ms'], 'png': str(root/f'ocean-{side}'/(name+'.png'))}
            for side, view in (('before', a), ('after', b))}
        for side in ('before', 'after'):
            assert native(root/f'ocean-{side}'/(name+'.png')).shape == (540, 960, 3)
    old, new = [read(f'objects-{side}/object-cloud-acceptance.json') for side in ('before', 'after')]
    for key in ('source', 'surface', 'utc', 'wind_speed_mps', 'object_count'):
        assert old[key] == new[key]
    assert all(new['temporal_contract'].values())
    for name, b in new['frame_benchmark'].items():
        a = old['frame_benchmark'][name]
        for key in ('eye_local_m', 'focus_local_m', 'frames', 'temporal_raw_width', 'temporal_output_width'):
            assert a[key] == b[key]
        report['region'][name] = {side: {'median_ms': value['median_ms'], 'p90_ms': value['p90_ms']}
                                 for side, value in (('before', a), ('after', b))}
    old, new = [read(f'shimmer-{side}/shimmer.json') for side in ('before', 'after')]
    assert old['source'] == new['source'] and old['scene_seconds'] == new['scene_seconds'] == 0
    assert len(old['views']) == len(new['views']) == 48
    frames = {side: {case: [] for case in ('day', 'sunset', 'night')} for side in ('before', 'after')}
    for a, b in zip(old['views'], new['views']):
        for key in ('name', 'hours_utc', 'eye', 'focus', 'light_local'):
            assert a[key] == b[key], f'water sweep mismatch: {key}'
        for side in ('before', 'after'):
            frames[side][a['name'].split('-')[0]].append(native(root/f'shimmer-{side}'/(a['name']+'.png')))
    for case in ('day', 'sunset', 'night'):
        report['water_shimmer'][case] = {side: water_metrics(frames[side][case]) for side in ('before', 'after')}
    if (root/'upper-before/flight-acceptance.json').exists():
        a,b=[read(f'upper-{side}/flight-acceptance.json') for side in ('before','after')]
        for key in ('source','surface','utc','wind_speed_mps','object_count'):assert a[key]==b[key]
        for i,(x,y) in enumerate(zip(a['frames'],b['frames'])):
            for key in ('eye','focus','seconds','phase','eye_condensate'):assert x[key]==y[key]
            for side in ('before','after'):native(root/f'upper-{side}'/f'flight-{i:03}.png')
        report['upper_edge']={side:timing([f['frame_ms'] for f in doc['frames']]) for side,doc in (('before',a),('after',b))}
    if (root/'upper-after/dense-acceptance.json').exists():
        dense=read('upper-after/dense-acceptance.json');assert len(dense['frames'])==613
        assert dense['history_eligible_draws']>len(dense['frames'])*.85 and dense['thin_cloud_frames']>3
        for i in range(len(dense['frames'])):native(root/'upper-after/dense-flight'/f'frame-{i:04}.png')
        report['dense_after_only']={'frames':len(dense['frames']),'nominal_simulation_hz':dense['nominal_simulation_hz'],
            'history_eligible_draws':dense['history_eligible_draws'],'thin_cloud_frames':dense['thin_cloud_frames'],
            'capture_is_wallclock_realtime':False}
    (root/'comparison.json').write_text(json.dumps(report, ensure_ascii=False, indent=2)+'\n')
    for group in ('wind', 'ocean', 'region'):
        for name, pair in report[group].items():
            print(f"{group}/{name}: {pair['before']['median_ms']:.2f} -> {pair['after']['median_ms']:.2f} ms")
    print('canonical eye-density error:', density_error)
    print(json.dumps(report['water_shimmer'], indent=2))


if __name__ == '__main__':
    main()
