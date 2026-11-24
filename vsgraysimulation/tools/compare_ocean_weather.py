#!/usr/bin/env python3
"""Compare actual native Earth ocean captures; never synthesize screenshots."""
import argparse
import copy
import json
import math
import statistics
import subprocess
import struct
from pathlib import Path
from PIL import Image


def stats(values):
    values = sorted(values)
    assert values and all(math.isfinite(x) and x >= 0 for x in values)
    return dict(median_ms=statistics.median(values), p95_ms=values[int(.95 * (len(values) - 1))],
                samples=len(values))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--root', default='build/ocean-weather')
    args = parser.parse_args()
    root = Path(args.root).resolve()
    before_dir, after_dir = root / 'weather-before-captures', root / 'after'
    before = json.loads((before_dir / 'weather-ocean.json').read_text())
    after = json.loads((after_dir / 'weather-ocean.json').read_text())
    assert before['utc'] == after['utc'] and before['scene'] == after['scene']
    assert before['source'] == after['source']
    assert before['physical_resolution_m'] == after['physical_resolution_m']
    # SurfaceQuality also marks sources coarser than a subdivided display
    # mesh. Retain that result; check actual DSM/imagery source spacing below.
    assert before['source']['source_coarse'] == after['source']['source_coarse']
    assert 29 < after['source']['dem_spacing_m'] < 32 and after['source']['imagery_spacing_m'] == 10
    assert len(before['frames']) == len(after['frames']) == 100
    max_pose_error = max_velocity_error = 0
    for a, b in zip(before['frames'], after['frames']):
        assert a['seconds'] == b['seconds'] and a['simulation_interval_ms'] == b['simulation_interval_ms'] == 100
        assert a['wakes'] == b['wakes']
        max_pose_error = max(max_pose_error, max(abs(x-y) for x, y in zip(a['pose'], b['pose'])))
        max_velocity_error = max(max_velocity_error, max(abs(x-y) for x, y in zip(a['velocity'], b['velocity'])))
    assert max_pose_error == max_velocity_error == 0, 'display change altered canonical rigid state'
    checkpoints = [json.loads((p / 'checkpoint.json').read_text()) for p in (before_dir, after_dir)]
    # Compare all rigid matrices, angular velocities, voxel geometry, wave
    # packets and the rest of the saved state, excluding the new optical layer.
    cleaned = copy.deepcopy(checkpoints)
    for checkpoint in cleaned:
        checkpoint['ocean'].pop('foam', None)
    assert cleaned[0] == cleaned[1], 'checkpoint changed beyond the added foam layer'
    assert 0 < len(checkpoints[1]['ocean']['foam']) <= 192
    captures = {}
    for name in ('initial', 'wake-30', 'wake-60', 'wake-100', 'cloud-reflection',
                 'clear-reflection', 'sunset-reflection', 'night-reflection', 'fog-reflection'):
        sizes = []
        for directory in (before_dir, after_dir):
            with (directory / (name + '.png')).open('rb') as f:
                header = f.read(24)
                assert header[:8] == b'\x89PNG\r\n\x1a\n'
                sizes.append(struct.unpack('>II', header[16:24]))
        assert sizes[0] == sizes[1] == (960, 540)
        captures[name] = dict(before=str(before_dir / (name + '.png')) if name not in ('clear-reflection', 'fog-reflection') else None,
                              after=str(after_dir / (name + '.png')))
    paired = {}
    for key in ('buoyancy_wall_ms', 'water_total_wall_ms', 'advance_wall_ms',
                'render_submit_wait_ms', 'complete_wall_ms'):
        paired[key] = dict(before=stats([f[key] for f in before['frames']]),
                           after=stats([f[key] for f in after['frames']]))
    weather = []
    for a, b in zip(before['weather_cases'], after['weather_cases']):
        for key in ('name', 'hours_utc', 'cover', 'fog_visibility_m'):
            assert a[key] == b[key]
        assert abs(b['observed_cover']-b['cover']) < 1e-6, 'weather command did not reach live cloud module'
        paired_case = abs(b['cover']-.65) < 1e-6
        weather.append(dict(name=a['name'], hours_utc=b['hours_utc'],
                            cover=b['cover'], fog_visibility_m=b['fog_visibility_m'],
                            before=stats(a['render_ms']) if paired_case else None, after=stats(b['render_ms']),
                            baseline_comparable=paired_case, observed_cover=b['observed_cover'],
                            baseline_effective_cover=.65))
    def water_difference(directory):
        a = Image.open(directory / 'cloud-reflection.png').convert('RGB').crop((80, 360, 880, 520)).tobytes()
        b = Image.open(directory / 'clear-reflection.png').convert('RGB').crop((80, 360, 880, 520)).tobytes()
        return sum(abs(x-y) for x, y in zip(a, b)) / len(a)
    weather_difference = water_difference(after_dir)
    assert weather_difference > .05, 'live cloud change did not affect water radiance'
    try:
        gpu = subprocess.check_output(['nvidia-smi', '--query-gpu=name,driver_version,memory.total',
                                       '--format=csv,noheader'], text=True, timeout=5).strip()
    except (OSError, subprocess.SubprocessError):
        gpu = 'not queried'
    report = dict(
        gpu=gpu, reflection_cache_resolution=[128, 64], foam_cache_resolution=[512, 512],
        foam_cache_span_m=1024,
        resolution=[960, 540], utc=after['utc'], source=after['source'],
        physical_resolution_m=after['physical_resolution_m'],
        scope='stable native Vulkan rendering; 100 coupled samples, each advances 100ms fixed-step simulation; full cycle=advance+render; excludes IO/warmup; not GPU-only timestamps',
        frozen_render=dict(before=stats(before['render_frames']), after=stats(after['render_frames'])),
        coupled=paired, weather=weather, captures=captures,
        position_max_absolute_error_m=max_pose_error,
        velocity_max_absolute_error_mps=max_velocity_error,
        entire_checkpoint_equal_excluding_optical_foam=True,
        water_cloud_clear_rgb_mad_of_255=weather_difference,
        weather_baseline_note='archived baseline case-switch used configure rather than live setAtmosphereParams; baseline cover stayed .65. Clear/fog requested-cover cases are unpaired and excluded from before/after timing claims',
        saved_foam_deposits=len(checkpoints[1]['ocean']['foam']),
        ray_checks=after['range_checks'], maximum_ray_error_m=after['maximum_range_error_m'],
        realtime_ratio_before=10000 / sum(f['complete_wall_ms'] for f in before['frames']),
        realtime_ratio_after=10000 / sum(f['complete_wall_ms'] for f in after['frames']),
        limitations=['regional hemispherical environment probe, no ship/terrain reflection or exact nearby-cloud parallax',
                     'foam is optical decoration, not changed forces or calibrated sensor radiometry',
                     'existing assumed-bulk-density cruiser develops marked heel during the 10s baseline too; no naval stability claim',
                     'no bathymetry supplied; no measured shoaling/breaking waves',
                     'single viewport, no high-resolution or multi-client real-time claim'])
    (root / 'comparison.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n')
    print(json.dumps({k:report[k] for k in ('frozen_render', 'coupled', 'saved_foam_deposits',
                                          'entire_checkpoint_equal_excluding_optical_foam')}, indent=2))


if __name__ == '__main__':
    main()
