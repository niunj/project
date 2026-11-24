#!/usr/bin/env python3
"""Compare original native Earth cloud captures, without synthesizing imagery."""
import argparse
import json
from pathlib import Path

from PIL import Image


def image_pair(a, b):
    old, new = [Image.open(p).convert('RGBA') for p in (a, b)]
    assert old.size == new.size
    assert old.getextrema()[3] == new.getextrema()[3] == (255, 255), 'opaque sky coverage regressed'
    return {'before_png': str(a), 'after_png': str(b), 'resolution': list(new.size),
            'opaque_frame_preserved': True}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--root', default='build/cloud-natural')
    root = Path(parser.parse_args().root).resolve()
    report = {'scope': 'same-camera, same-UTC native Vulkan captures; stable render only, no physics step',
              'density': 'morphology changed; CPU and GPU use the new condensate, not the old numeric density',
              'ocean': {}, 'cloud_region': {}}
    read = lambda path: json.loads((root / path).read_text())
    old, new = [read(f'{side}/cloud-halo.json') for side in ('before', 'after')]
    assert old['source'] == new['source'] and old['utc'] == new['utc']
    report['coastal_source'] = new['source']
    assert len(old['views']) == len(new['views']) == 5
    for a, b in zip(old['views'], new['views']):
        for key in ('name', 'hours_utc', 'eye', 'focus'):
            assert a[key] == b[key]
        name = a['name']
        entry = image_pair(root / 'before' / (name + '.png'), root / 'after' / (name + '.png'))
        entry.update(before_render_median_ms=a['render_median_ms'],
                     after_render_median_ms=b['render_median_ms'])
        report['ocean'][name] = entry
    direct = read('after-direct/cloud-halo.json')
    assert direct['source'] == new['source'] and direct['utc'] == new['utc']
    assert len(direct['views']) == len(new['views'])
    report['direct_fallback'] = {}
    for a, b in zip(new['views'], direct['views']):
        for key in ('name', 'hours_utc', 'eye', 'focus'):
            assert a[key] == b[key]
        path = root / 'after-direct' / (b['name'] + '.png')
        image = Image.open(path).convert('RGBA')
        assert image.getextrema()[3] == (255, 255), 'direct fallback alpha regressed'
        report['direct_fallback'][b['name']] = {'png': str(path), 'resolution': list(image.size),
            'opaque_frame_preserved': True, 'render_median_ms': b['render_median_ms']}
    old, new = [read(f'objects-{side}/object-cloud-acceptance.json') for side in ('before', 'after')]
    for key in ('source', 'surface', 'utc', 'object_count', 'cover_parameter', 'wind_speed_mps'):
        assert old[key] == new[key], f'cloud region configuration changed: {key}'
    assert all(new['temporal_contract'].values())
    assert not new['surface']['source_coarse']
    assert 29 < new['surface']['dem_spacing_m'] < 32 and new['surface']['imagery_spacing_m'] == 10
    report['region_source'] = new['surface']
    report['object_count'] = new['object_count']
    report['wind_speed_mps'] = new['wind_speed_mps']
    report['motion'] = new['motion']
    assert new['motion']['clouds_displaced_over_20m'] > new['object_count'] / 2
    assert new['motion']['clouds_with_density_change'] > 0
    for name, b in new['frame_benchmark'].items():
        a = old['frame_benchmark'][name]
        for key in ('eye_local_m', 'focus_local_m', 'frames', 'temporal_raw_width', 'temporal_output_width'):
            assert a[key] == b[key]
        entry = image_pair(root / 'objects-before' / (name + '.png'),
                           root / 'objects-after' / (name + '.png'))
        entry.update(before_render_median_ms=a['median_ms'], after_render_median_ms=b['median_ms'])
        report['cloud_region'][name] = entry
    (root / 'comparison.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n')
    for group in ('ocean', 'cloud_region'):
        for name, entry in report[group].items():
            print(f"{group}/{name}: {entry['before_render_median_ms']:.2f} -> "
                  f"{entry['after_render_median_ms']:.2f} ms; native opaque RGBA passed")


if __name__ == '__main__':
    main()
