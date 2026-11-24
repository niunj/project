#!/usr/bin/env python3
"""Validate matched native captures without generating or altering imagery."""
import argparse
import hashlib
import json
import math
import statistics
from pathlib import Path
from PIL import Image


def stats(values):
    values = sorted(values)
    assert values and all(math.isfinite(x) and x >= 0 for x in values)
    return dict(median_ms=statistics.median(values), p95_ms=values[int(.95*(len(values)-1))], samples=len(values))


def mass(obj):
    volume = obj['volume']
    return (sum(count for _, count in volume['occupied_runs']) * volume['cell_size_m']**3 *
            obj['material']['density_kg_m3'] + obj['physics'].get('ballast', {}).get('mass_kg', 0))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--root', default='build/ocean-stability')
    root = Path(parser.parse_args().root).resolve()
    read = lambda p: json.loads(p.read_text())
    before, after = [read(root / side / 'weather-ocean.json') for side in ('before', 'after')]
    checkpoints = [read(root / side / 'checkpoint.json') for side in ('before', 'after')]
    ships = [next(o for o in c['objects'] if o['id'] == 'vessel') for c in checkpoints]
    assert before['utc'] == after['utc'] and before['source'] == after['source']
    assert before['physical_resolution_m'] == after['physical_resolution_m']
    assert len(before['frames']) == len(after['frames']) == 100
    for a, b in zip(before['frames'], after['frames']):
        assert a['seconds'] == b['seconds'] and a['simulation_interval_ms'] == b['simulation_interval_ms'] == 100
    for key in ('cell_size_m', 'dimensions', 'origin_m', 'occupied_runs'):
        assert ships[0]['volume'][key] == ships[1]['volume'][key], 'collision occupancy or precision changed'
    assert abs(mass(ships[0])-mass(ships[1])) < 1e-6, 'ballast changed nominal vessel total mass'
    assert after['maximum_range_error_m'] < .001
    assert len(after['long_stability']) == 500 and after['long_stability'][-1]['seconds'] == 60
    assert after['long_maximum_inclination_deg'] < 15
    assert len(after['live_frames']) == 90
    coupled = {}
    for key in ('buoyancy_wall_ms', 'water_total_wall_ms', 'advance_wall_ms',
                'render_submit_wait_ms', 'complete_wall_ms'):
        coupled[key] = dict(before=stats([f[key] for f in before['frames']]),
                            after=stats([f[key] for f in after['frames']]))
    live = {key: stats([f[key] for f in after['live_frames']]) for key in
            ('buoyancy_wall_ms', 'advance_wall_ms', 'render_submit_wait_ms', 'complete_wall_ms')}
    captures = {}
    for name in ('initial', 'wake-30', 'wake-60', 'wake-100', 'cloud-reflection', 'sunset-reflection', 'night-reflection'):
        paths = [root / side / (name+'.png') for side in ('before', 'after')]
        assert all(Image.open(p).size == (960, 540) for p in paths)
        captures[name] = dict(before=str(paths[0]), after=str(paths[1]))
    extra = {name: str(root/'after'/(name+'.png')) for name in ('coast-aerial', 'stable-200', 'stable-400', 'stable-600')}
    assert all(Path(p).is_file() for p in extra.values())
    for case in after['weather_cases']:
        assert abs(case['cover']-case['observed_cover']) < 1e-6
    def water_rgb(name):
        return Image.open(root/'after'/(name+'.png')).convert('RGB').crop((80,360,880,520)).tobytes()
    cloud, clear = water_rgb('cloud-reflection'), water_rgb('clear-reflection')
    difference = sum(abs(a-b) for a,b in zip(cloud,clear))/len(cloud)
    assert difference > .05, 'clouds did not influence water'
    hydro = read(root/'hydro-profile-current.json')['vessel']
    old_hydro = read(root/'hydro-profile-old-mass.json')['vessel']
    assert old_hydro['estimated_GM_m'] < 0 < hydro['estimated_GM_m']
    assert hydro['force_relative_difference'] < 1e-4 and hydro['buoyancy_lever_difference_m'] < .0025
    report = dict(
        hydrostatic_GM_before_m=old_hydro['estimated_GM_m'], hydrostatic_GM_after_m=hydro['estimated_GM_m'],
        fixed_input_hydro=hydro,
        fixed_input_profile_scope='current executable; same initial pose/water; baseline 2m pressure blocks with query caches disabled versus optimized columns/caches; not old-software or full-cycle timings',
        utc=after['utc'], resolution=[960,540], source=after['source'],
        scope='100 paired samples, each advances 100ms in 20ms fixed steps; native render includes update/submit/deviceWaitIdle; excludes screenshots, IO and warmup; longer stability/pacing measured separately',
        physical_resolution_m=after['physical_resolution_m'], occupied_geometry_equal=True,
        occupancy_sha256=hashlib.sha256(json.dumps(ships[1]['volume']['occupied_runs']).encode()).hexdigest(),
        mass_before_kg=mass(ships[0]), mass_after_kg=mass(ships[1]),
        ballast=ships[1]['physics']['ballast'],
        before_final_inclination_deg=math.degrees(math.acos(max(-1,min(1,ships[0]['base_pose'][5])))),
        after_10s_peak_inclination_deg=after['maximum_inclination_deg'],
        after_60s_peak_inclination_deg=after['long_maximum_inclination_deg'],
        frozen_render=dict(before=stats(before['render_frames']),after=stats(after['render_frames'])),
        coupled=coupled, live_30hz=live, live_wall_seconds=after['live_wall_seconds'],
        live_realtime_ratio=after['live_simulation_seconds']/after['live_wall_seconds'],
        live_complete_over_33ms=sum(f['complete_wall_ms']>1000/30 for f in after['live_frames']),
        reflection_updates_in_10s=after['reflection_updates_during_10s'],
        maximum_collision_ray_difference_m=after['maximum_range_error_m'],
        water_cloud_clear_rgb_mad_of_255=difference, captures=captures, extra_captures=extra,
        limitations=['assumed equivalent ballast and coastal optical coefficients, not measured naval CG/GM or certified seawater optics',
                     '60s includes initial motion/COM impulse and subsequent deceleration/drift; not sustained high-speed propulsion',
                     'same wave heights, waterline tolerance and collision rule; changed mass distribution intentionally changes trajectory',
                     'regional sky probe, no ship/terrain reflections; no measured bathymetry or newly generated port geometry',
                     'single 960x540 viewport; no 1080p/4K or multi-client realtime extrapolation'])
    (root/'comparison.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n')
    print(json.dumps({k:report[k] for k in ('before_final_inclination_deg','after_60s_peak_inclination_deg',
                    'frozen_render','coupled','live_realtime_ratio','reflection_updates_in_10s')}, indent=2))


if __name__ == '__main__':
    main()
